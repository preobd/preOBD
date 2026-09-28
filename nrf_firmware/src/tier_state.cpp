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

// #449: once the host has answered a PING this power cycle we know it speaks
// framed, so an unanswered connect PING means the host's decoder was busy
// (mid-frame after an overrun, rebooting) -- not that it is a transparent
// host. Falling to TRANSPARENT then passes the host's binary frames raw into
// the client's terminal for the rest of the session. Retry instead: the
// host abandons a stuck frame after a 100 ms gap, so the second PING lands.
// A host that never answered still gets the one-shot fallback.
bool    g_hostWasFramed = false;
uint8_t g_pingRetries   = 0;
constexpr uint8_t kPingRetriesForFramedHost = 4;   // 2 s each → 10 s total

// Resync request (#435): set from the BLE event task (CCCD callbacks), consumed
// by tierStatePoll() on the loop task. Debounced so one client's back-to-back
// subscribes to Alarm / Action / Live Values produce a single PING.
volatile bool     g_resyncRequested = false;
volatile uint32_t g_resyncRequestMs = 0;
constexpr uint32_t kResyncDebounceMs = 250;

// Through the TX ring like every other frame (#491 review): whole or not at
// all, and never inside a frame already queued. A full ring at connect is
// covered by the PING timeout/retry below.
void sendPing() {
    uartBridgeSendPing();
}

} // namespace

void tierStateInit() {
    g_mode = MODE_DISCONNECTED;
    // "Has the host ever answered" is scoped to this power cycle, so it is
    // cleared here and nowhere else -- a boot is the only thing that makes a
    // framed-capable host unknown again.
    g_hostWasFramed = false;
    g_pingRetries = 0;
}

void tierStateOnConnect() {
    uartBridgeDecoderReset();
    g_mode = MODE_PINGING;
    g_pingRetries = 0;
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
        g_hostWasFramed = true;
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
        if (g_hostWasFramed && g_pingRetries < kPingRetriesForFramedHost) {
            ++g_pingRetries;
            g_pingStartMs = millis();
            uartBridgeDecoderReset();
            sendPing();
            TIER_LOG("ping timeout on a framed host -- retrying");
        } else {
            g_mode = MODE_TRANSPARENT;
            // Forget that the host was ever framed: it has now missed a full
            // retry budget, so it is a transparent host as far as we are
            // concerned. Without this the 10 s penalty is paid again on
            // EVERY reconnect -- 10 s of dead terminal each time -- when the
            // user has simply reflashed the host without framed support.
            g_hostWasFramed = false;
            TIER_LOG("TRANSPARENT (ping timeout)");
        }
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
