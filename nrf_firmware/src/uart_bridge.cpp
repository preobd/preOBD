// uart_bridge.cpp -- Tier 1 transparent relay + Tier 2 framed dispatch.
//
// Mode is owned by tier_state. This module just routes bytes based on the
// current tier:
//
//   TIER1 (Phase 1 behavior): raw passthrough both directions. RX flush on
//     newline, 20-byte fill, or 50ms idle.
//   TIER2: inbound Serial1 bytes feed the streaming framer; complete frames
//     are dispatched by type. Outbound BLE writes are wrapped in type 0x01
//     frames before they hit the wire.
//   PINGING: feed inbound bytes to the framer only (to catch a 0xFF echo);
//     any non-framed Tier 1 chatter the host emits during the 2s window is
//     dropped. Outbound BLE writes are buffered raw -- forwarding either
//     framed or unframed before we know the tier risks corrupting the
//     handshake on a Tier 2 host. After resolution they're flushed in the
//     mode-appropriate form.
//
// TX path is still a ring drained from loop() so BLE callbacks never block
// on Serial1. No flow control in Phase 2; CTS/RTS deferred to Phase 3.

#include "uart_bridge.h"

#include <Arduino.h>
#include <string.h>

#include "ble_bridge.h"
#include "ble_services.h"
#include "tier2_framer.h"
#include "tier_state.h"

namespace {

constexpr size_t kRxFlushBytes  = 20;       // BLE 4.2 minimum payload
constexpr uint32_t kRxStallMs   = 50;
constexpr size_t kTxRingSize    = 1024;
constexpr size_t kBleNotifyChunk = 20;      // chunk Tier 2 text resp to 20B

// --- Tier 1 RX accumulator ---
uint8_t  g_rx[kRxFlushBytes];
size_t   g_rxLen = 0;
uint32_t g_lastByteMs = 0;

// --- TX ring drained from loop() ---
uint8_t  g_tx[kTxRingSize];
volatile size_t g_txHead = 0;
volatile size_t g_txTail = 0;

// --- Tier 2 receive scratch ---
Tier2Frame g_frame;

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

void drainTx() {
    while (g_txTail != g_txHead) {
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
    uint8_t frame[TIER2_HEADER_BYTES + kFrameChunkPayload + 1];
    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = ((len - offset) > kFrameChunkPayload)
                                 ? kFrameChunkPayload : (len - offset);
        const size_t n = tier2Encode(TIER2_TYPE_TEXT_CMD,
                                     data + offset,
                                     static_cast<uint16_t>(chunk),
                                     frame);
        if (n == 0) break;
        if (!enqueueTxAtomic(frame, n)) break;
        offset += chunk;
    }
    return offset;
}

void notifyCmdRxChunked(const uint8_t* data, uint16_t len) {
    // Tier 2 path -- no host speaks Tier 2 in Phase 2 so this is effectively
    // dead code at runtime. If/when it goes live and the TX queue is tight,
    // we'll need a per-frame retry stash similar to the Tier 1 path; for
    // now, swallow the failure and rely on the host-side retry/ACK logic
    // that PHASE3_TIER2_TRANSPORT_PLAN.md will bring.
    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = ((len - offset) > kBleNotifyChunk)
                                 ? kBleNotifyChunk : (len - offset);
        if (!bleServicesNotifyCmdRx(data + offset, chunk)) return;
        offset += chunk;
    }
}

void handleFrame(const Tier2Frame& f) {
    switch (f.type) {
    case TIER2_TYPE_PING:
        tierStateOnPingFrame();
        break;
    case TIER2_TYPE_TEXT_RESP:
        notifyCmdRxChunked(f.payload, f.len);
        break;
    default:
        // Status/Alarm/Relay/OTA/etc deferred past Phase 2.
        break;
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
}

void uartBridgePoll() {
    const bool connected = bleBridgeConnected();
    const bool pinging = tierStateIsPinging();
    const bool framed  = tierStateIsFramed();

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

    while (Serial1.available()) {
        const int b = Serial1.read();
        if (b < 0) break;

        if (!connected) continue;

        if (framed || pinging) {
            const Tier2FeedResult r = tier2FeedByte(
                static_cast<uint8_t>(b), g_frame);
            if (r == TIER2_FRAME_READY) {
                handleFrame(g_frame);
            }
            // NEED_MORE / RESYNCING: continue silently. Bytes outside a frame
            // boundary in Tier 2 mode are protocol noise and intentionally
            // dropped.
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
    //   PINGING: buffer; flushed on tier resolution.
    //   TIER2:   wrap in 0x01 frames.
    //   TIER1:   pass raw.
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

void uartBridgeResetRx() {
    noInterrupts();
    g_rxLen = 0;
    g_pendingTxLen = 0;
    interrupts();
    tier2DecoderReset();
}
