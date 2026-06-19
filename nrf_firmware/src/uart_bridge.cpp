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
#include "relay_handler.h"
#include "tier_state.h"

namespace {

constexpr size_t kRxFlushBytes  = 20;       // BLE 4.2 minimum payload
constexpr uint32_t kRxStallMs   = 50;
constexpr size_t kTxRingSize    = 1024;
constexpr size_t kBleNotifyChunk = 20;      // chunk Tier 2 text resp to 20B

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

// --- Framed-mode pending-notify stash for back-pressure ---
// When a type 0x02 text-response frame arrives, its payload is chunked into
// 20-byte BLE notifies. If notify back-pressures partway through, we stash
// the remaining payload here and stop reading Serial1 -- letting the RX
// ring fill so Phase 3 flow control asserts RTS to the host. Without this,
// over-fast inbound frames would be silently truncated at the BLE layer.
uint8_t  g_pendingNotifyPayload[FRAME_MAX_PAYLOAD];
uint16_t g_pendingNotifyLen    = 0;
uint16_t g_pendingNotifyOffset = 0;

// --- CMD_TX bytes queued during PINGING, replayed post-resolution ---
// Sized to match the TX ring so a single PINGING-window burst can fit in
// principle; partial flushes (when the ring is also tight) leave a residual
// that's retried on subsequent polls.
constexpr size_t kPendingTxSize = 1024;
uint8_t  g_pendingTx[kPendingTxSize];
size_t   g_pendingTxLen = 0;

// Returns true if the buffer was successfully flushed (or was already
// empty). Returns false if BLE notify is back-pressuring; caller MUST hold
// off reading more Serial1 bytes until a subsequent retry succeeds, or
// notifications will be silently dropped on the floor.
bool flushRx() {
    if (g_rxLen == 0) return true;
    if (!bleServicesNotifyCmdRx(g_rx, g_rxLen)) return false;
    g_rxLen = 0;
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

// Wrap `data`/`len` as type 0x01 frames and push into the TX ring. Frames
// are queued atomically -- if a frame doesn't fit, we stop and report how
// many caller bytes were successfully framed.
size_t enqueueAsTextFrames(const uint8_t* data, size_t len) {
    constexpr size_t kFrameChunkPayload = 256;
    uint8_t frame[FRAME_HEADER_BYTES + kFrameChunkPayload + 1];
    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = ((len - offset) > kFrameChunkPayload)
                                 ? kFrameChunkPayload : (len - offset);
        const size_t n = frameEncode(FRAME_TYPE_TEXT_CMD,
                                     data + offset,
                                     static_cast<uint16_t>(chunk),
                                     frame);
        if (n == 0) break;
        if (!enqueueTxAtomic(frame, n)) break;
        offset += chunk;
    }
    return offset;
}

// Drain the pending-notify stash to CMD_RX in 20-byte chunks. Returns true
// if the stash was fully drained (or was empty); returns false if BLE
// notify back-pressured (returned false within its own 100ms timeout) --
// caller must stop reading new Serial1 bytes until the next poll iteration
// retries, so the RX ring fills and Phase 3 flow control can assert RTS.
//
// Bluefruit's notify() blocks each call for ~one BLE connection interval
// (15-30ms) waiting for an HVN packet slot, so a full multi-chunk drain
// spends most of its time blocked there. We re-evaluate RTS between
// chunks so the LED tracks reality and the host gets throttled during
// the drain, not just at the top of the next uartBridgePoll iteration.
bool drainPendingNotify() {
    while (g_pendingNotifyOffset < g_pendingNotifyLen) {
        const uint16_t remaining = g_pendingNotifyLen - g_pendingNotifyOffset;
        const uint16_t chunk = (remaining > kBleNotifyChunk)
                                   ? kBleNotifyChunk : remaining;
        if (!bleServicesNotifyCmdRx(g_pendingNotifyPayload + g_pendingNotifyOffset,
                                    chunk)) {
            return false;
        }
        g_pendingNotifyOffset += chunk;
#if defined(PIN_NRF_RTS_OUT) && defined(PIN_NRF_CTS_IN)
        // Each notify call blocked ~15-30ms in BLE wait; Serial1 has been
        // receiving the whole time. Re-check fill so RTS asserts mid-drain
        // when needed.
        updateInboundRts();
#endif
    }
    g_pendingNotifyLen = 0;
    g_pendingNotifyOffset = 0;
    return true;
}

// Returns true if the caller may continue reading Serial1 (frame fully
// dispatched or no notify needed); false if a pending notify is stuck
// and inbound polling must back off.
bool handleFrame(const Frame& f) {
    switch (f.type) {
    case FRAME_TYPE_PING:
        tierStateOnPingFrame();
        return true;
    case FRAME_TYPE_TEXT_RESP:
        // Stash the payload and try to drain immediately. If notify back-
        // pressures, the caller breaks out of the Serial1 read loop and
        // we retry on the next uartBridgePoll iteration.
        if (f.len > FRAME_MAX_PAYLOAD) return true;  // defensive; shouldn't happen
        memcpy(g_pendingNotifyPayload, f.payload, f.len);
        g_pendingNotifyLen = f.len;
        g_pendingNotifyOffset = 0;
        return drainPendingNotify();
    case FRAME_TYPE_STATUS:
        // Host MCU pushed a fresh 32-byte PreobdSystemStatus snapshot. Cache
        // it into the GATT characteristic and notify subscribers. Drop any
        // wrong-sized payload defensively.
        if (f.len == PREOBD_STATUS_SIZE) {
            bleServicesUpdateStatus(f.payload, f.len);
        }
        return true;
    case FRAME_TYPE_ALARM:
        // Host MCU pushed an 18-byte PreobdAlarmEvent. Best-effort notify;
        // wrong-sized payloads are dropped silently.
        alarmHandlerPush(f.payload, f.len);
        return true;
    case FRAME_TYPE_RELAY:
        // Host MCU pushed a 16-byte PreobdRelayState snapshot. Cache + notify.
        relayHandlerPush(f.payload, f.len);
        return true;
    default:
        // OTA/etc deferred past MVP scope.
        return true;
    }
}

// Try to push pending CMD_TX bytes into the TX ring in the resolved tier.
// Any residual that didn't fit is compacted to the front of the buffer and
// will be retried on the next poll.
void flushPendingTx() {
    if (g_pendingTxLen == 0) return;
    const size_t consumed = tierStateIsFramed()
        ? enqueueAsTextFrames(g_pendingTx, g_pendingTxLen)
        : enqueueTx(g_pendingTx, g_pendingTxLen);
    if (consumed == 0) return;
    const size_t remaining = g_pendingTxLen - consumed;
    if (remaining > 0) {
        memmove(g_pendingTx, g_pendingTx + consumed, remaining);
    }
    g_pendingTxLen = remaining;
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
    if (!pinging && g_pendingTxLen > 0) {
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

    // Framed-mode equivalent: a prior poll may have left a partially-notified
    // text-response payload stashed. Drain it before reading more from
    // Serial1; bail out if still back-pressured so the RX ring fills and
    // Phase 3 flow control asserts RTS to the host.
    if (framed && g_pendingNotifyLen > 0) {
        if (!drainPendingNotify()) {
            drainTx();
            return;
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
                    // BLE notify back-pressured mid-frame; the rest is in
                    // the pending stash. Stop reading so Serial1 fills and
                    // Phase 3 flow control asserts RTS to throttle the host.
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
        return enqueueAsTextFrames(data, len);
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
    g_pendingTxLen = 0;
    g_pendingNotifyLen = 0;
    g_pendingNotifyOffset = 0;
    interrupts();
    frameDecoderReset(g_decoder);
}

void uartBridgeDecoderReset() {
    frameDecoderReset(g_decoder);
}
