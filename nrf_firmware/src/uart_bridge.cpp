// uart_bridge.cpp -- transparent relay + framed dispatch.
//
// Mode is owned by tier_state. This module just routes bytes based on the
// current mode:
//
//   TRANSPARENT (Phase 1 behavior): raw passthrough both directions. RX
//     flush on newline, 20-byte fill, or 50ms idle.
//   FRAMED: inbound Serial1 bytes feed the streaming framer; complete
//     frames are dispatched by type. Outbound BLE writes are wrapped in
//     type 0x01 frames before they hit the wire.
//   PINGING: feed inbound bytes to the framer only (to catch a 0xFF echo);
//     any non-framed chatter the host emits during the 2s window is
//     dropped. Outbound BLE writes are buffered raw -- forwarding either
//     framed or unframed before we know the mode risks corrupting the
//     handshake on a framed-capable host. After resolution they're flushed
//     in the mode-appropriate form.
//
// TX path is still a ring drained from loop() so BLE callbacks never block
// on Serial1. Phase 3 adds opt-in software-paced flow control via two GPIOs
// (PIN_NRF_RTS_OUT / PIN_NRF_CTS_IN, see board_pins.h):
//   - Inbound: assert PIN_NRF_RTS_OUT when Serial1.available() crosses a
//     high watermark; deassert at a low watermark. Hysteresis avoids
//     toggle storms.
//   - Outbound: yield in drainTx while PIN_NRF_CTS_IN is HIGH (~100ms
//     timeout failsafe so a stuck line can't hang the loop).

#include "uart_bridge.h"

#include <Arduino.h>
#include <string.h>

#include "alarm_handler.h"
#include "ble_bridge.h"
#include "ble_gatt_defs.h"
#include "ble_services.h"
#include "board_pins.h"
#include "frame_codec.h"
#include "live_handler.h"
#include "relay_handler.h"
#include "tier_state.h"

namespace {

constexpr size_t kRxFlushBytes  = 20;       // BLE 4.2 minimum payload
constexpr uint32_t kRxStallMs   = 50;
constexpr size_t kTxRingSize    = 1024;

#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
// Hysteresis thresholds on Serial1.available(). Sized so the worst-case
// BLE notify stall (Bluefruit's getHvnPacket blocks up to 100ms on the
// HVN semaphore = ~1440 bytes inflight at 115200 baud) plus the host's
// chunked reaction window (~32 bytes) leaves comfortable headroom in the
// SERIAL_BUFFER_SIZE RX ring before any drops. At the current 8192-byte
// buffer, high-water=256 leaves ~6464 bytes of margin even during the
// worst case -- well over-engineered, but cheap.
constexpr size_t kInboundHighWaterBytes = 256;
constexpr size_t kInboundLowWaterBytes  = 64;
constexpr uint32_t kOutboundStallMs     = 100;
static bool g_rtsAsserted = false;
#endif

// --- Tier 1 RX accumulator ---
uint8_t  g_rx[kRxFlushBytes];
size_t   g_rxLen = 0;
uint32_t g_lastByteMs = 0;

// --- TX ring drained from loop() ---
uint8_t  g_tx[kTxRingSize];
volatile size_t g_txHead = 0;
volatile size_t g_txTail = 0;

// --- Framed-mode receive scratch + per-instance decoder state ---
// Per-instance (not file-scope global) so the codec stays reentrant; only
// one instance is needed since uart_bridge is the sole inbound feeder.
Frame g_frame;
FrameDecoderState g_decoder;

// --- Framed-mode pending-notify stashes for back-pressure ---
// When a type 0x02 (text) or 0x30 (ELM327) response frame arrives, its
// payload is chunked into MTU-sized BLE notifies. If notify back-pressures
// partway through, the remainder stays stashed and is drained cooperatively
// on later polls. One stash per destination (#263): the two channels notify
// different characteristics on independent HVN queues, so a slow webapp text
// drain must not block an ELM327 round-trip to a streaming OBD app (or vice
// versa). Each stash holds exactly one frame's payload.
struct NotifyStash {
    uint8_t  payload[FRAME_MAX_PAYLOAD];
    uint16_t len    = 0;
    uint16_t offset = 0;
    // Per-chunk fan-out progress mask (see bleServicesNotify* contract):
    // records which (char, central) pairs accepted the chunk currently in
    // flight so a retry after a partial back-pressure doesn't re-deliver.
    // Re-seeded from targetSeed (not 0) at each fresh chunk.
    uint8_t  mask = 0;
    // Chunk size frozen for the duration of a partially-acked chunk: 0 means
    // "recompute at the next chunk boundary". A chunk some links already
    // accepted must keep its size so the offset advances by exactly what
    // those links received -- see drainStash.
    uint16_t chunk = 0;
    // Eligibility snapshot taken when the payload was stashed: bits pre-set
    // for (char, central) pairs that were absent/unsubscribed at stash time.
    // A central that connects and subscribes mid-drain is therefore excluded
    // until the next clean payload instead of receiving a late, possibly
    // fragmented tail (#262 review follow-up).
    uint8_t  targetSeed = 0;
    // Per-slot connection generations at stash time. The seed alone can't
    // catch a same-handle disconnect + reconnect mid-drain (the eligibility
    // probes key on the handle, and the newcomer looks identical to the
    // snapshotted central); a generation mismatch marks the slot stale so
    // drainStash retires it from this payload's target set.
    uint8_t  slotGen[BLE_SERVICES_MAX_CONN_SLOTS] = {};

    bool empty() const { return len == 0; }
    void clear() {
        len = 0; offset = 0; mask = 0; chunk = 0; targetSeed = 0;
    }
};
NotifyStash g_textStash;   // 0x02 -> CMD_RX + NUS TX
NotifyStash g_elmStash;    // 0x30 -> HM-10 FFE1

// Held frame: a decoded frame whose destination stash was still occupied
// after one drain attempt. One slot suffices -- while a frame is held the
// Serial1 read loop stops entirely (RX ring fills, Phase 3 flow control
// asserts RTS to the host), and the hold is promoted into its stash at the
// top of a later poll once that stash frees.
bool     g_holdValid = false;
uint8_t  g_holdType  = 0;
uint16_t g_holdLen   = 0;
uint8_t  g_holdPayload[FRAME_MAX_PAYLOAD];

// --- CMD_TX bytes queued during PINGING, replayed post-resolution ---
// Sized to match the TX ring so a single PINGING-window burst can fit in
// principle; partial flushes (when the ring is also tight) leave a residual
// that's retried on subsequent polls.
constexpr size_t kPendingTxSize = 1024;
uint8_t  g_pendingTx[kPendingTxSize];
size_t   g_pendingTxLen = 0;

// Sibling buffer for ELM327 (HM-10 FFE1) bytes queued during PINGING. Kept
// separate from g_pendingTx because on resolution the two replay as different
// frame types (text 0x01 vs ELM327 0x30). OBD apps rarely write inside the 2s
// ping window, but a stray "ATZ" mustn't be misframed as a CLI command.
uint8_t  g_pendingElm[kPendingTxSize];
size_t   g_pendingElmLen = 0;

// Notify destination selector for drainStash: DEST_TEXT -> CMD_RX + NUS TX;
// DEST_ELM -> HM-10 FFE1.
enum NotifyDest : uint8_t { DEST_TEXT = 0, DEST_ELM };

// Per-chunk fan-out progress mask for the transparent g_rx flush (see
// bleServicesNotify* contract). Framed-mode equivalents live in NotifyStash.
uint8_t g_rxNotifyMask = 0;

// Returns true if the buffer was successfully flushed (or was already
// empty). Returns false if BLE notify is back-pressuring; caller MUST hold
// off reading more Serial1 bytes until a subsequent retry succeeds, or
// notifications will be silently dropped on the floor.
bool flushRx() {
    if (g_rxLen == 0) return true;
    // Transparent mode is a single-client raw bridge: fan the response out to
    // every subscribed serial-notify char (CMD_RX / NUS TX / FFE1) so whichever
    // channel the lone client is using receives it. g_rxNotifyMask persists the
    // fan-out progress across retries of this same g_rx content.
    if (!bleServicesNotifyTransparent(g_rx, g_rxLen, g_rxNotifyMask)) return false;
    g_rxLen = 0;
    g_rxNotifyMask = 0;
    return true;
}

inline size_t txCapacity() {
    const size_t head = g_txHead;
    const size_t tail = g_txTail;
    if (head >= tail) return kTxRingSize - (head - tail) - 1;
    return tail - head - 1;
}

size_t enqueueTx(const uint8_t* data, size_t len) {
    const size_t room = txCapacity();
    const size_t n = (len < room) ? len : room;
    for (size_t i = 0; i < n; ++i) {
        g_tx[g_txHead] = data[i];
        g_txHead = (g_txHead + 1) % kTxRingSize;
    }
    return n;
}

// All-or-nothing enqueue for Tier 2 frames. A partial frame on the wire is
// protocol garbage, so if there's not enough room we refuse the write
// outright rather than truncate.
bool enqueueTxAtomic(const uint8_t* data, size_t len) {
    if (txCapacity() < len) return false;
    for (size_t i = 0; i < len; ++i) {
        g_tx[g_txHead] = data[i];
        g_txHead = (g_txHead + 1) % kTxRingSize;
    }
    return true;
}

#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
// Returns true once the host's RTS line (our CTS_IN) is clear. Returns
// false if the line is still asserted after kOutboundStallMs -- caller
// must abort the drain and retry on the next poll iteration. A naive
// per-byte timeout (block, advance, repeat) would multiply the stall by
// the drain length: at 100ms each, a full 1KB TX ring would stall the
// task for up to 100 seconds on a stuck line, starving everything else.
bool awaitOutboundClear() {
    if (digitalRead(PIN_NRF_CTS_IN) == LOW) return true;
    const uint32_t start = millis();
    while (digitalRead(PIN_NRF_CTS_IN) == HIGH) {
        if (millis() - start >= kOutboundStallMs) return false;
        yield();
    }
    return true;
}

// Drive our RTS line based on Serial1 RX ring fill with hysteresis. Called
// from uartBridgePoll on every iteration.
void updateInboundRts() {
    const int avail = Serial1.available();
    if (!g_rtsAsserted && avail >= static_cast<int>(kInboundHighWaterBytes)) {
        g_rtsAsserted = true;
        digitalWrite(PIN_NRF_RTS_OUT, HIGH);
    } else if (g_rtsAsserted && avail <= static_cast<int>(kInboundLowWaterBytes)) {
        g_rtsAsserted = false;
        digitalWrite(PIN_NRF_RTS_OUT, LOW);
    }
}
#endif

void drainTx() {
    while (g_txTail != g_txHead) {
#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
        if (!awaitOutboundClear()) break;   // bail whole drain; retry next poll
#endif
        const int wrote = Serial1.write(g_tx[g_txTail]);
        if (wrote <= 0) break;
        g_txTail = (g_txTail + 1) % kTxRingSize;
    }
}

// Wrap `data`/`len` as frames of the given type and push into the TX ring.
// Frames are queued atomically -- if a frame doesn't fit, we stop and report
// how many caller bytes were successfully framed. Used for both text (0x01)
// and ELM327 (0x30) outbound channels.
size_t enqueueAsFrames(uint8_t type, const uint8_t* data, size_t len) {
    constexpr size_t kFrameChunkPayload = 256;
    uint8_t frame[FRAME_HEADER_BYTES + kFrameChunkPayload + 1];
    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = ((len - offset) > kFrameChunkPayload)
                                 ? kFrameChunkPayload : (len - offset);
        const size_t n = frameEncode(type,
                                     data + offset,
                                     static_cast<uint16_t>(chunk),
                                     frame);
        if (n == 0) break;
        if (!enqueueTxAtomic(frame, n)) break;
        offset += chunk;
    }
    return offset;
}

// Drain one stash to its destination characteristics in MTU-sized chunks
// (up to 244B on a BANDWIDTH_MAX link). Returns true if the stash was fully
// drained (or was empty); returns false if BLE notify back-pressured
// (returned false within its own 100ms timeout) -- the remainder stays
// stashed and is retried on a later poll.
//
// Bluefruit's notify() blocks each call for ~one BLE connection interval
// (15-30ms) waiting for an HVN packet slot, so a full multi-chunk drain
// spends most of its time blocked there. We re-evaluate RTS between
// chunks so the LED tracks reality and the host gets throttled during
// the drain, not just at the top of the next uartBridgePoll iteration.
// All (char, slot) mask bits belonging to one connection slot for a given
// destination's fan-out set. Bit layout matches notifyFanout: bit =
// charIndex * BLE_SERVICES_MAX_CONN_SLOTS + h. Text fans out to two chars
// (CMD_RX, NUS TX); ELM to one (FFE1).
uint8_t slotBits(NotifyDest dest, uint16_t h) {
    const uint8_t first = static_cast<uint8_t>(1u << h);
    if (dest == DEST_ELM) return first;
    return static_cast<uint8_t>(
        first | (1u << (BLE_SERVICES_MAX_CONN_SLOTS + h)));
}

bool drainStash(NotifyStash& s, NotifyDest dest) {
    while (s.offset < s.len) {
        // Retire any slot whose connection generation moved since stash time:
        // that handle now belongs to a different central (same-handle
        // reconnect mid-drain), which must not receive this payload's tail.
        // Checked every pass -- notifyFanout probes eligibility live, so a
        // back-pressured chunk's retry would otherwise pick the newcomer up.
        for (uint16_t h = 0; h < BLE_SERVICES_MAX_CONN_SLOTS; ++h) {
            if (bleServicesSlotGeneration(h) != s.slotGen[h]) {
                s.targetSeed |= slotBits(dest, h);
            }
        }
        s.mask |= s.targetSeed;
        // Recompute the chunk size only at a clean chunk boundary (chunk == 0).
        // Freezing the size while a chunk is partially acked keeps the offset
        // advancing by exactly what the already-served links got -- otherwise
        // the stream to an acked central duplicates or drops bytes when the
        // next poll retries the same offset at a new size. The target set
        // can't grow mid-payload (targetSeed excludes late joiners), so a
        // frozen chunk can never exceed a *new* link's MTU; at worst it stays
        // smaller than necessary after the limiting link drops.
        if (s.chunk == 0) {
            s.chunk = static_cast<uint16_t>(bleServicesMaxNotifyChunk());
        }
        const uint16_t remaining = s.len - s.offset;
        const uint16_t chunk = (remaining > s.chunk) ? s.chunk : remaining;
        const uint8_t* p = s.payload + s.offset;
        // s.mask carries this chunk's fan-out progress so a back-pressure
        // retry doesn't double-send to an already-served (char, central) pair.
        const bool sent = (dest == DEST_ELM)
                              ? bleServicesNotifyHm10(p, chunk, s.mask)
                              : bleServicesNotifyText(p, chunk, s.mask);
        if (!sent) {
            return false;
        }
        s.offset += chunk;
        s.mask  = s.targetSeed;  // fresh fan-out, same per-payload target set
        s.chunk = 0;             // re-size at the next chunk boundary
#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
        // Each notify call blocked ~15-30ms in BLE wait; Serial1 has been
        // receiving the whole time. Re-check fill so RTS asserts mid-drain
        // when needed.
        updateInboundRts();
#endif
    }
    s.clear();
    return true;
}

// Load one frame's payload into a stash and snapshot the eligible notify
// targets. Caller guarantees the stash is empty and len <= FRAME_MAX_PAYLOAD.
void stashPayload(NotifyStash& s, NotifyDest dest,
                  const uint8_t* payload, uint16_t len) {
    memcpy(s.payload, payload, len);
    s.len    = len;
    s.offset = 0;
    s.chunk  = 0;
    s.targetSeed = (dest == DEST_ELM) ? bleServicesHm10TargetSeed()
                                      : bleServicesTextTargetSeed();
    s.mask = s.targetSeed;
    for (uint16_t h = 0; h < BLE_SERVICES_MAX_CONN_SLOTS; ++h) {
        s.slotGen[h] = bleServicesSlotGeneration(h);
    }
}

// Returns true if the caller may continue reading Serial1; false if this
// frame had to be parked in the hold slot (destination stash still draining)
// and inbound polling must stop until the hold is promoted.
//
// Note a *partially* drained fresh stash returns true: back-pressure on one
// destination must not stop the read loop, or a slow webapp text drain
// starves ELM327 round-trips again (#263). Reading only stops when a frame
// collides with its own destination's occupied stash.
bool handleFrame(const Frame& f) {
    switch (f.type) {
    case FRAME_TYPE_PING:
        tierStateOnPingFrame();
        return true;
    case FRAME_TYPE_TEXT_RESP:
    case FRAME_TYPE_ELM327: {
        // Text responses (0x02) -> CMD_RX + NUS TX; ELM327 responses (0x30)
        // -> HM-10 FFE1. Same stash + back-pressure path, per-type stash.
        if (f.len > FRAME_MAX_PAYLOAD) return true;  // defensive; shouldn't happen
        const bool isElm = (f.type == FRAME_TYPE_ELM327);
        NotifyStash& s = isElm ? g_elmStash : g_textStash;
        const NotifyDest dest = isElm ? DEST_ELM : DEST_TEXT;
        if (!s.empty()) {
            // Destination still draining a prior payload. Try once to finish
            // it; if still back-pressured, park this frame in the hold slot
            // and stop reading Serial1 (RX ring fills, RTS throttles the
            // host) until the stash frees.
            if (!drainStash(s, dest)) {
                memcpy(g_holdPayload, f.payload, f.len);
                g_holdType  = f.type;
                g_holdLen   = f.len;
                g_holdValid = true;
                return false;
            }
        }
        stashPayload(s, dest, f.payload, f.len);
        drainStash(s, dest);  // best-effort; leftover drains on later polls
        return true;
    }
    case FRAME_TYPE_STATUS:
        // Host MCU pushed a fresh 32-byte PreobdSystemStatus snapshot. Cache
        // it into the GATT characteristic and notify subscribers. Drop any
        // wrong-sized payload defensively.
        if (f.len == PREOBD_STATUS_SIZE) {
            bleServicesUpdateStatus(f.payload, f.len);
        }
        return true;
    case FRAME_TYPE_ALARM:
        // Host MCU pushed a 19-byte PreobdAlarmEvent. Best-effort notify;
        // wrong-sized payloads are dropped silently.
        alarmHandlerPush(f.payload, f.len);
        return true;
    case FRAME_TYPE_RELAY:
        // Host MCU pushed a 16-byte PreobdRelayState snapshot. Cache + notify.
        relayHandlerPush(f.payload, f.len);
        return true;
    case FRAME_TYPE_LIVE:
        // Host MCU pushed a 16-byte PreobdLiveValue. Cache + best-effort
        // notify; a dropped notify is refreshed by the host's 5s heartbeat.
        liveHandlerPush(f.payload, f.len);
        return true;
    default:
        // OTA/etc deferred past MVP scope.
        return true;
    }
}

// Try to push one pending buffer's bytes into the TX ring in the resolved
// tier: framed wraps them in `frameType` frames, transparent passes raw. Any
// residual that didn't fit is compacted to the front and retried next poll.
void flushPendingBuf(uint8_t* buf, size_t& len, uint8_t frameType) {
    if (len == 0) return;
    const size_t consumed = tierStateIsFramed()
        ? enqueueAsFrames(frameType, buf, len)
        : enqueueTx(buf, len);
    if (consumed == 0) return;
    const size_t remaining = len - consumed;
    if (remaining > 0) {
        memmove(buf, buf + consumed, remaining);
    }
    len = remaining;
}

// Flush both PINGING-era buffers (CMD_TX/NUS text and HM-10 ELM327) once the
// tier resolves, each in its own frame type.
void flushPendingTx() {
    flushPendingBuf(g_pendingTx, g_pendingTxLen, FRAME_TYPE_TEXT_CMD);
    flushPendingBuf(g_pendingElm, g_pendingElmLen, FRAME_TYPE_ELM327);
}

} // namespace

void uartBridgeInit() {
    // 115200 fixed by contract with the host MCU; same as Phase 1.
    Serial1.begin(115200);
#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
    pinMode(PIN_NRF_RTS_OUT, OUTPUT);
    digitalWrite(PIN_NRF_RTS_OUT, LOW);     // start deasserted (clear-to-send)
    pinMode(PIN_NRF_CTS_IN, INPUT_PULLDOWN); // unconnected = LOW = always-OK
    g_rtsAsserted = false;
#endif
}

void uartBridgePoll() {
    const bool connected = bleBridgeConnected();
    const bool pinging = tierStateIsPinging();
    const bool framed  = tierStateIsFramed();

#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
    // Inbound flow control: drive our RTS based on Serial1 RX ring fill.
    updateInboundRts();
#endif

    // Once tier is resolved, drain any CMD_TX bytes that were buffered during
    // PINGING. Retried every poll because the TX ring may have been tight
    // and a previous flush could have left a residual.
    if (!pinging && (g_pendingTxLen > 0 || g_pendingElmLen > 0)) {
        flushPendingTx();
    }

    // Retry any RX bytes left over from a prior poll's failed flush BEFORE
    // we read more from Serial1. If notify is still back-pressured, bail
    // out -- don't keep draining the UART into a buffer we can't empty.
    if (!framed && !pinging && g_rxLen > 0) {
        if (!flushRx()) {
            drainTx();
            return;
        }
    }

    // Framed-mode equivalent: cooperatively drain both notify stashes left
    // from prior polls. ELM first -- its payloads are small and latency-
    // sensitive (a streaming OBD app times out if round-trips stall), while
    // text payloads can be bulk dumps that take many polls to clear. Back-
    // pressure on one stash no longer bails the whole poll (#263); reading
    // only stops while a frame is parked in the hold slot.
    if (framed) {
        if (!g_elmStash.empty())  drainStash(g_elmStash, DEST_ELM);
        if (!g_textStash.empty()) drainStash(g_textStash, DEST_TEXT);

        if (g_holdValid) {
            // Promote the held frame once its destination stash has freed;
            // until then keep the read loop stopped so the RX ring fills and
            // Phase 3 flow control asserts RTS to the host.
            const bool isElm = (g_holdType == FRAME_TYPE_ELM327);
            NotifyStash& s = isElm ? g_elmStash : g_textStash;
            if (!s.empty()) {
                drainTx();
                return;
            }
            const NotifyDest dest = isElm ? DEST_ELM : DEST_TEXT;
            stashPayload(s, dest, g_holdPayload, g_holdLen);
            g_holdValid = false;
            drainStash(s, dest);  // best-effort; leftover drains next poll
        }
    }

    while (Serial1.available()) {
        const int b = Serial1.read();
        if (b < 0) break;

        if (!connected) continue;

        if (framed || pinging) {
            const FrameFeedResult r = frameFeedByte(
                g_decoder, static_cast<uint8_t>(b), g_frame);
            if (r == FRAME_READY) {
                if (!handleFrame(g_frame)) {
                    // Frame parked in the hold slot (its destination stash
                    // is still draining). Stop reading so Serial1 fills and
                    // Phase 3 flow control asserts RTS to throttle the host;
                    // the hold is promoted at the top of a later poll.
                    break;
                }
            }
            // NEED_MORE / RESYNCING: continue silently. Bytes outside a
            // frame boundary are protocol noise and intentionally dropped.
            continue;
        }

        // Tier 1 path (Phase 1 behavior).
        g_rx[g_rxLen++] = static_cast<uint8_t>(b);
        g_lastByteMs = millis();
        if (b == '\n' || g_rxLen >= kRxFlushBytes) {
            if (!flushRx()) {
                // BLE TX queue full. Stop pulling from Serial1 -- the
                // rx accumulator holds what we already drained, and the
                // next poll cycle will retry the notify.
                break;
            }
#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
            // Re-evaluate the RX ring fill after each flush. Without this
            // call, a fast-arriving stream can keep Serial1.available()
            // non-zero indefinitely, never letting us return to the top of
            // uartBridgePoll to assert RTS -- the buffer would fill until
            // the BSP RingBuffer drops bytes.
            updateInboundRts();
#endif
        }
    }

    if (connected && !framed && !pinging && g_rxLen > 0 &&
        (millis() - g_lastByteMs) >= kRxStallMs) {
        flushRx();   // best-effort stall flush; retry in next poll if it fails
    }

    drainTx();
}

size_t uartBridgeWrite(const uint8_t* data, size_t len) {
    // CMD_TX callback path. Mode-dependent:
    //   PINGING:     buffer; flushed on mode resolution.
    //   FRAMED:      wrap in 0x01 frames.
    //   TRANSPARENT: pass raw.
    if (tierStateIsPinging()) {
        const size_t room = kPendingTxSize - g_pendingTxLen;
        const size_t n = (len < room) ? len : room;
        for (size_t i = 0; i < n; ++i) {
            g_pendingTx[g_pendingTxLen++] = data[i];
        }
        return n;
    }
    if (tierStateIsFramed()) {
        return enqueueAsFrames(FRAME_TYPE_TEXT_CMD, data, len);
    }
    return enqueueTx(data, len);
}

size_t uartBridgeWriteElm(const uint8_t* data, size_t len) {
    // HM-10 FFE1 callback path. Mirrors uartBridgeWrite but for the ELM327
    // channel:
    //   PINGING:     buffer in g_pendingElm; replayed as 0x30 on resolution.
    //   FRAMED:      wrap in 0x30 frames so the host runs the ELM327 parser.
    //   TRANSPARENT: pass raw (single-client bridge; the host's ELM327 port
    //                binding decides meaning).
    if (tierStateIsPinging()) {
        const size_t room = kPendingTxSize - g_pendingElmLen;
        const size_t n = (len < room) ? len : room;
        for (size_t i = 0; i < n; ++i) {
            g_pendingElm[g_pendingElmLen++] = data[i];
        }
        return n;
    }
    if (tierStateIsFramed()) {
        return enqueueAsFrames(FRAME_TYPE_ELM327, data, len);
    }
    return enqueueTx(data, len);
}

bool uartBridgeSendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (len > FRAME_MAX_PAYLOAD) return false;
    // Only meaningful once framed mode has resolved; queueing a binary
    // frame during PINGING or in transparent mode would put protocol
    // garbage on the wire.
    if (!tierStateIsFramed()) return false;
    // Static scratch buffer rather than ~517 bytes of stack on the BLE
    // event-task context. Safe because all callers are serialized on the
    // single SoftDevice event task (BLE write callbacks don't preempt each
    // other), and the encoded frame is fully consumed by enqueueTxAtomic
    // before this function returns.
    static uint8_t s_frameBuf[FRAME_HEADER_BYTES + FRAME_MAX_PAYLOAD + 1];
    const size_t n = frameEncode(type, payload, len, s_frameBuf);
    if (n == 0) return false;
    return enqueueTxAtomic(s_frameBuf, n);
}

void uartBridgeResetRx() {
    noInterrupts();
    g_rxLen = 0;
    g_rxNotifyMask = 0;
    g_pendingTxLen = 0;
    g_pendingElmLen = 0;
    g_textStash.clear();
    g_elmStash.clear();
    g_holdValid = false;
    interrupts();
    frameDecoderReset(g_decoder);
}

void uartBridgeDecoderReset() {
    frameDecoderReset(g_decoder);
}
