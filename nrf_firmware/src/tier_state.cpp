#include "tier_state.h"

#include <Arduino.h>

#include "frame_codec.h"
#include "uart_bridge.h"

#if DEBUG_SERIAL
#define TIER_LOG(msg) Serial.println(F("[tier] " msg))
#else
#define TIER_LOG(msg) do {} while (0)
#endif

namespace {

enum Mode : uint8_t {
    MODE_DISCONNECTED = 0,
    MODE_PINGING,
    MODE_TRANSPARENT,    // historically MODE_TIER1
    MODE_FRAMED,         // historically MODE_TIER2
};

volatile Mode g_mode = MODE_DISCONNECTED;
uint32_t      g_pingStartMs = 0;
constexpr uint32_t kPingTimeoutMs = 2000;

// Resync request (#435): set from the BLE event task (CCCD callbacks), consumed
// by tierStatePoll() on the loop task. Debounced so one client's back-to-back
// subscribes to Alarm / Action / Live Values produce a single PING.
volatile bool     g_resyncRequested = false;
volatile uint32_t g_resyncRequestMs = 0;
constexpr uint32_t kResyncDebounceMs = 250;

void sendPing() {
    uint8_t frame[FRAME_HEADER_BYTES + 1];   // zero-length payload
    const size_t n = frameEncode(FRAME_TYPE_PING, nullptr, 0, frame);
    if (n > 0) {
        Serial1.write(frame, n);
    }
}

} // namespace

void tierStateInit() {
    g_mode = MODE_DISCONNECTED;
}

void tierStateOnConnect() {
    uartBridgeDecoderReset();
    g_mode = MODE_PINGING;
    g_pingStartMs = millis();
    g_resyncRequested = false;   // negotiation PING already resyncs the host
    sendPing();
}

void tierStateOnDisconnect() {
    g_mode = MODE_DISCONNECTED;
    g_resyncRequested = false;
    uartBridgeDecoderReset();
}

void tierStateRequestResync() {
    g_resyncRequestMs = millis();
    g_resyncRequested = true;
    TIER_LOG("resync requested (CCCD subscribe)");
}

void tierStateOnPingFrame() {
    if (g_mode == MODE_PINGING) {
        g_mode = MODE_FRAMED;
        TIER_LOG("FRAMED");
    } else if (g_mode == MODE_FRAMED) {
        TIER_LOG("PING echo (resync round-trip)");
    }
    // Pings outside the negotiation window are ignored -- the host echoes
    // every PING it receives, so each resync PING (#435) comes back here in
    // FRAMED mode. We don't renegotiate mid-session today.
}

void tierStatePoll() {
    if (g_mode == MODE_PINGING &&
        (millis() - g_pingStartMs) >= kPingTimeoutMs) {
        g_mode = MODE_TRANSPARENT;
        TIER_LOG("TRANSPARENT (ping timeout)");
    }

    if (!g_resyncRequested) return;
    switch (g_mode) {
    case MODE_PINGING:
        // Subscribed during negotiation: hold the request until the tier
        // resolves. If it lands FRAMED the host's negotiation-edge resync
        // most likely ran before this central subscribed, so the extra PING
        // is still worth it.
        break;
    case MODE_FRAMED: {
        // Signed elapsed: g_resyncRequestMs is stamped on the BLE event task
        // and can read one tick later than a millis() cached here.
        const int32_t sinceMs = (int32_t)(millis() - g_resyncRequestMs);
        if (sinceMs < (int32_t)kResyncDebounceMs) break;
        // Through the TX ring (atomic, loop-task drain) so the PING can never
        // land inside a frame already on the wire. A full ring just retries
        // next poll.
        if (uartBridgeSendFrame(FRAME_TYPE_PING, nullptr, 0)) {
            g_resyncRequested = false;
            TIER_LOG("resync PING sent");
        }
        break;
    }
    default:
        // TRANSPARENT / DISCONNECTED: nothing to resync against.
        g_resyncRequested = false;
        break;
    }
}

bool tierStateIsFramed() {
    return g_mode == MODE_FRAMED;
}

bool tierStateIsPinging() {
    return g_mode == MODE_PINGING;
}
