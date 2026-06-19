// ble_services.cpp -- Control Service registration + System Status snapshot.

#include "ble_services.h"

#include <Arduino.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "uart_bridge.h"
#include "uuid_util.h"

namespace {

// BLEService(uint8_t*) reads 16 bytes for the 128-bit UUID. At global-init
// time we don't have the parsed UUID yet, so seed with a zero buffer and
// overwrite via copy-assignment in bleServicesInit().
uint8_t g_zeroUuid[16] = {};

BLEService    g_controlService(g_zeroUuid);
BLECharacteristic g_cmdTx;
BLECharacteristic g_cmdRx;
BLECharacteristic g_status;

// NUS (text terminal channel) + HM-10 (ELM327 / OBD-app channel). HM-10 uses
// the 16-bit 0xFFE0/0xFFE1 UUIDs real adapters expose, which OBD apps probe
// for; Bluefruit's uint16_t ctors register them directly.
BLEService    g_nusService(g_zeroUuid);
BLECharacteristic g_nusRx;
BLECharacteristic g_nusTx;
BLEService    g_hm10Service(static_cast<uint16_t>(0xFFE0));
BLECharacteristic g_hm10Char(static_cast<uint16_t>(0xFFE1));

// Liveness keepalive sentinel (#266). A sleeping host keeps its BLE link alive
// (the OS BT controller acks at the link layer) but the frozen tab stops
// consuming and stops writing -- indistinguishable from a live-idle client
// without an app-level signal. The webapp writes this single byte to CMD_TX on
// a timer; the nRF records it as activity but does NOT forward it to the host
// (it's liveness only, never a CLI command). 0x00 is safe: real CLI traffic is
// printable ASCII + newline.
constexpr uint8_t kKeepaliveByte = 0x00;

// Last inbound-write time per peripheral slot, for zombie eviction (#266).
// Written from the BLE event-task write callbacks, read from the loop poll;
// loop-context tolerant (a stale read at worst delays an eviction one poll).
uint32_t g_lastActivityMs[BLE_SERVICES_MAX_CONN_SLOTS] = {};

inline void recordActivity(uint16_t conn_handle) {
    if (conn_handle < BLE_SERVICES_MAX_CONN_SLOTS) {
        g_lastActivityMs[conn_handle] = millis();
    }
}

void cmdTxWriteCallback(uint16_t conn_handle, BLECharacteristic* chr,
                        uint8_t* data, uint16_t len) {
    (void)chr;
    recordActivity(conn_handle);   // any write proves the central is alive
    // Swallow the keepalive sentinel: liveness only, never a host command.
    if (len == 1 && data && data[0] == kKeepaliveByte) return;
    if (data && len > 0) {
        uartBridgeWrite(data, len);   // text CLI -> framed type 0x01
    }
}

// NUS RX shares the text-command path with CMD_TX: both are CLI channels, so
// both wrap to framed type 0x01 (or pass raw in transparent mode).
void nusRxWriteCallback(uint16_t /*conn_handle*/, BLECharacteristic* /*chr*/,
                        uint8_t* data, uint16_t len) {
    if (data && len > 0) {
        uartBridgeWrite(data, len);
    }
}

// HM-10 FFE1 is the ELM327 channel: client writes (ATxx / OBD hex) wrap to
// framed type 0x30 so the host runs them through the ELM327 parser.
void hm10WriteCallback(uint16_t /*conn_handle*/, BLECharacteristic* /*chr*/,
                       uint8_t* data, uint16_t len) {
    if (data && len > 0) {
        uartBridgeWriteElm(data, len);
    }
}

// Maximum simultaneous peripheral links -- must match Bluefruit.begin(2) in
// main.cpp. With two peripheral connections the SoftDevice assigns the low
// handle indices [0, kMaxConnSlots), so we use the connection handle directly
// as a compact slot index for the fan-out retry mask below.
constexpr uint16_t kMaxConnSlots = BLE_SERVICES_MAX_CONN_SLOTS;

// Slot-generation tracking (see header): loop-context only, no atomics
// needed -- bumped and read solely from the main loop's poll chain.
uint8_t g_slotGen[kMaxConnSlots] = {};
bool    g_slotConnected[kMaxConnSlots] = {};
// Single-shot eviction guard: conn->disconnect() is async, so the slot stays
// connected() for the ~tens of ms the LL teardown takes. Without this, every
// poll in that window re-issues disconnect() (harmless no-ops, but spammy).
// Set when we request eviction, cleared when a fresh central takes the slot.
bool    g_evictRequested[kMaxConnSlots] = {};

// Notify one chunk to every (characteristic, connected-central) pair where the
// central has subscribed to that characteristic.
//
// Per-connection on purpose: Bluefruit's plain notify(data,len) and
// notifyEnabled() default to Bluefruit.connHandle() -- the *most-recently*
// connected central only -- so a naive fan-out silently stops reaching the
// first central the instant a second one connects (the bug that froze the
// webapp when an OBD app joined). We therefore notify each handle explicitly.
//
// `doneMask` carries per-(char,slot) progress across retries: bit
// (i*kMaxConnSlots + h) is set once char i has been accepted by, or is
// absent/unsubscribed on, the central at handle h. A retry after a partial
// back-pressure re-sends only to the still-pending pairs -- never duplicating a
// chunk to a link that already took it. Caller seeds doneMask to 0 per chunk and
// passes the same variable back on each retry. Bit budget: n<=3 chars *
// kMaxConnSlots(2) = 6 bits, fits the uint8_t mask. Returns true once the chunk
// is fully delivered (every pair settled); false while a subscribed link's HVN
// queue is still full (caller retries the same chunk + mask).
bool notifyFanout(BLECharacteristic* const chrs[], size_t n,
                  const uint8_t* data, size_t len, uint8_t& doneMask) {
    bool allDone = true;
    for (size_t i = 0; i < n; ++i) {
        for (uint16_t h = 0; h < kMaxConnSlots; ++h) {
            const uint8_t bit = static_cast<uint8_t>(1u << (i * kMaxConnSlots + h));
            if (doneMask & bit) continue;            // already settled on a prior pass
            BLEConnection* conn = Bluefruit.Connection(h);
            if (!conn || !conn->connected()) {       // slot empty
                doneMask |= bit;
                continue;
            }
            if (!chrs[i]->notifyEnabled(h)) {        // this central not subscribed
                doneMask |= bit;
                continue;
            }
            if (chrs[i]->notify(h, data, len)) {
                doneMask |= bit;                     // accepted
            } else {
                allDone = false;                     // HVN full; retry this pair next pass
            }
        }
    }
    return allDone;
}

// Snapshot which (char, central) pairs are currently INELIGIBLE (slot empty
// or not subscribed) as a pre-seeded doneMask for notifyFanout: pre-set bits
// are skipped, so a payload drained with this seed never targets a central
// that joined after the payload was stashed -- it gets the next clean payload
// instead of a late, possibly fragmented tail. Same bit layout as
// notifyFanout (bit i*kMaxConnSlots + h).
uint8_t targetSeed(BLECharacteristic* const chrs[], size_t n) {
    uint8_t seed = 0;
    for (size_t i = 0; i < n; ++i) {
        for (uint16_t h = 0; h < kMaxConnSlots; ++h) {
            const uint8_t bit = static_cast<uint8_t>(1u << (i * kMaxConnSlots + h));
            BLEConnection* conn = Bluefruit.Connection(h);
            if (!conn || !conn->connected() || !chrs[i]->notifyEnabled(h)) {
                seed |= bit;
            }
        }
    }
    return seed;
}

} // namespace

void bleServicesInit() {
    // Bluefruit's BLEUuid stores a pointer to the 16-byte UUID buffer, not a
    // copy (see BLEUuid.h: `uint8_t const* _uuid128`). Each service /
    // characteristic needs its own persistent backing array -- a single
    // stack-local buffer reused across calls leaves dangling pointers once
    // the function returns, and advertising emits garbage UUIDs.
    static uint8_t kServiceUuid[16];
    static uint8_t kCmdTxUuid[16];
    static uint8_t kCmdRxUuid[16];
    static uint8_t kStatusUuid[16];

    if (parseUuid128(PREOBD_SERVICE_UUID, kServiceUuid)) {
        g_controlService = BLEService(kServiceUuid);
    }
    g_controlService.begin();

    if (parseUuid128(PREOBD_CHAR_CMD_TX_UUID, kCmdTxUuid)) {
        g_cmdTx = BLECharacteristic(kCmdTxUuid);
    }
    g_cmdTx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    g_cmdTx.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_cmdTx.setMaxLen(244);
    g_cmdTx.setWriteCallback(cmdTxWriteCallback);
    g_cmdTx.begin();

    if (parseUuid128(PREOBD_CHAR_CMD_RX_UUID, kCmdRxUuid)) {
        g_cmdRx = BLECharacteristic(kCmdRxUuid);
    }
    g_cmdRx.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    g_cmdRx.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_cmdRx.setMaxLen(244);
    g_cmdRx.begin();

    if (parseUuid128(PREOBD_CHAR_STATUS_UUID, kStatusUuid)) {
        g_status = BLECharacteristic(kStatusUuid);
    }
    g_status.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    g_status.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_status.setFixedLen(PREOBD_STATUS_SIZE);
    g_status.begin();

    // Initialize the cache to 32 zero bytes so reads before the host's first
    // push return a well-defined payload. Clients detect "no data yet" via
    // magic != PREOBD_STATUS_MAGIC.
    uint8_t zeros[PREOBD_STATUS_SIZE] = {};
    g_status.write(zeros, sizeof(zeros));

    // --- NUS: generic BLE terminal channel (text CLI, framed type 0x01) ---
    // Self-contained service.begin() -> char.begin() block; Bluefruit attaches
    // each characteristic to the most-recently-begun service, so this must not
    // be interleaved with another service's begin (see main.cpp ordering note).
    static uint8_t kNusServiceUuid[16];
    static uint8_t kNusRxUuid[16];
    static uint8_t kNusTxUuid[16];

    if (parseUuid128(NUS_SERVICE_UUID, kNusServiceUuid)) {
        g_nusService = BLEService(kNusServiceUuid);
    }
    g_nusService.begin();

    if (parseUuid128(NUS_CHAR_RX_UUID, kNusRxUuid)) {
        g_nusRx = BLECharacteristic(kNusRxUuid);
    }
    g_nusRx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    g_nusRx.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_nusRx.setMaxLen(244);
    g_nusRx.setWriteCallback(nusRxWriteCallback);
    g_nusRx.begin();

    if (parseUuid128(NUS_CHAR_TX_UUID, kNusTxUuid)) {
        g_nusTx = BLECharacteristic(kNusTxUuid);
    }
    g_nusTx.setProperties(CHR_PROPS_NOTIFY);
    g_nusTx.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_nusTx.setMaxLen(244);
    g_nusTx.begin();

    // --- HM-10 FFE0/FFE1: ELM327 channel for phone OBD apps (framed 0x30) ---
    // Single FFE1 characteristic (Write + Notify), the HM-10 clone convention
    // that OBD apps expect. 16-bit UUIDs set via the global ctors above.
    g_hm10Service.begin();
    g_hm10Char.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP |
                             CHR_PROPS_NOTIFY);
    g_hm10Char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_hm10Char.setMaxLen(244);
    g_hm10Char.setWriteCallback(hm10WriteCallback);
    g_hm10Char.begin();
}

bool bleServicesNotifyText(const uint8_t* data, size_t len, uint8_t& doneMask) {
    BLECharacteristic* const chrs[] = { &g_cmdRx, &g_nusTx };
    return notifyFanout(chrs, 2, data, len, doneMask);
}

bool bleServicesNotifyHm10(const uint8_t* data, size_t len, uint8_t& doneMask) {
    BLECharacteristic* const chrs[] = { &g_hm10Char };
    return notifyFanout(chrs, 1, data, len, doneMask);
}

bool bleServicesNotifyTransparent(const uint8_t* data, size_t len,
                                  uint8_t& doneMask) {
    BLECharacteristic* const chrs[] = { &g_cmdRx, &g_nusTx, &g_hm10Char };
    return notifyFanout(chrs, 3, data, len, doneMask);
}

uint8_t bleServicesTextTargetSeed() {
    BLECharacteristic* const chrs[] = { &g_cmdRx, &g_nusTx };
    return targetSeed(chrs, 2);
}

uint8_t bleServicesHm10TargetSeed() {
    BLECharacteristic* const chrs[] = { &g_hm10Char };
    return targetSeed(chrs, 1);
}

bool bleServicesNotifyRetry(BLECharacteristic& chr, const uint8_t* data,
                            uint16_t len, uint8_t& doneMask) {
    BLECharacteristic* const chrs[] = { &chr };
    return notifyFanout(chrs, 1, data, len, doneMask);
}

// Evict a zombie control central that hasn't written in this long (#266). A
// frozen/slept tab stops its keepalive timer, so it crosses this threshold; a
// throttled-but-alive backgrounded tab (timers still fire, <=60s) does not.
constexpr uint32_t kZombieTimeoutMs = 120000;  // 2 min

void bleServicesPollSlots() {
    const uint32_t now = millis();
    for (uint16_t h = 0; h < kMaxConnSlots; ++h) {
        BLEConnection* conn = Bluefruit.Connection(h);
        const bool connected = (conn != nullptr) && conn->connected();
        if (connected && !g_slotConnected[h]) {
            ++g_slotGen[h];          // a (possibly different) central took this slot
            g_lastActivityMs[h] = now;  // full grace before eviction can fire
            g_evictRequested[h] = false;  // fresh connection, arm eviction again
        }
        g_slotConnected[h] = connected;

        // Zombie eviction: only control clients subscribed to Status (the
        // webapp) are in scope -- OBD apps on FFE1 and generic NUS terminals
        // don't subscribe to Status and keep today's behavior (no eviction).
        // The webapp sends a keepalive on a timer, so a Status-subscribed link
        // silent past the timeout is a frozen/slept tab squatting a slot.
        // Disconnecting frees it; bleBridgePoll() then re-arms advertising.
        //
        // Signed elapsed (NOT `now - last > timeout`): g_lastActivityMs[h] is
        // written by cmdTxWriteCallback on the SoftDevice event task, which can
        // stamp a millis() value one tick LATER than the `now` this loop cached
        // a moment earlier. The unsigned difference would then underflow to
        // ~UINT32_MAX and trip an instant spurious eviction (bench: idle showed
        // 4294967295ms). Casting to int32_t makes a just-in-the-future stamp a
        // small negative -> not idle; it also handles millis() rollover.
        const int32_t idleMs = (int32_t)(now - g_lastActivityMs[h]);
        if (connected && !g_evictRequested[h] && g_status.notifyEnabled(h) &&
            idleMs > (int32_t)kZombieTimeoutMs) {
            conn->disconnect();
            g_evictRequested[h] = true;   // fire once; don't spam during teardown
        }
    }
}

uint8_t bleServicesSlotGeneration(uint16_t connHandle) {
    return (connHandle < kMaxConnSlots) ? g_slotGen[connHandle] : 0;
}

size_t bleServicesMaxNotifyChunk() {
    // A notify's data payload is the negotiated ATT MTU minus the 3-byte ATT
    // header (opcode + attribute handle). On a default 23-byte MTU link that
    // floors at 20; after BANDWIDTH_MAX negotiation it's 247 - 3 = 244.
    // Clamp to the CMD_RX characteristic's 244-byte max len either way.
    constexpr size_t kAttHeaderBytes = 3;
    constexpr size_t kDefaultChunk   = BLE_GATT_ATT_MTU_DEFAULT - kAttHeaderBytes; // 20
    constexpr size_t kMaxChunk       = 244;  // matches g_cmdRx.setMaxLen(244)

    // With two centrals the negotiated MTUs can differ. A notify is fanned out
    // to every subscriber, so size the chunk to the SMALLEST active link --
    // a chunk sized for the larger MTU would be rejected on the smaller one.
    // Scan the SAME handle range the fan-out uses (kMaxConnSlots, the begin(2)
    // peripheral handles 0/1) so the chunk floor and the notify targets stay
    // consistent -- never shrink everyone's chunk for a link we don't notify.
    uint16_t minMtu = 0;
    for (uint16_t h = 0; h < kMaxConnSlots; ++h) {
        BLEConnection* conn = Bluefruit.Connection(h);
        if (!conn || !conn->connected()) continue;
        const uint16_t mtu = conn->getMtu();
        if (minMtu == 0 || mtu < minMtu) minMtu = mtu;
    }
    if (minMtu <= kAttHeaderBytes) return kDefaultChunk;  // none connected / tiny

    size_t payload = minMtu - kAttHeaderBytes;
    if (payload < kDefaultChunk) payload = kDefaultChunk;
    if (payload > kMaxChunk)     payload = kMaxChunk;
    return payload;
}

void bleServicesNotifyAll(BLECharacteristic& chr, const uint8_t* data,
                          uint16_t len) {
    // Best-effort fan-out of one fixed-size notify to every connected central
    // subscribed to `chr`. Same per-handle reason as notifyFanout: a plain
    // chr.notify(data,len) would only reach the most-recently connected central.
    // Drops silently if a link's HVN queue is full -- acceptable for Status
    // (5s heartbeat re-pushes) and Relay/OTA (re-push on the next change).
    // Alarm events are edge-triggered with no re-push, so alarm_handler uses
    // bleServicesNotifyRetry instead (#263).
    for (uint16_t h = 0; h < kMaxConnSlots; ++h) {
        BLEConnection* conn = Bluefruit.Connection(h);
        if (!conn || !conn->connected()) continue;
        if (chr.notifyEnabled(h)) chr.notify(h, data, len);
    }
}

void bleServicesUpdateStatus(const uint8_t* payload, uint16_t len) {
    if (len != PREOBD_STATUS_SIZE) return;
    g_status.write(payload, len);
    bleServicesNotifyAll(g_status, payload, len);
}

BLEService& bleServicesControlService() {
    return g_controlService;
}

BLEService& bleServicesNusService() {
    return g_nusService;
}

BLEService& bleServicesHm10Service() {
    return g_hm10Service;
}
