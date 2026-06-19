#include "tier_state.h"

#include <Arduino.h>

#include "frame_codec.h"

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
    frameDecoderReset();
    g_mode = MODE_PINGING;
    g_pingStartMs = millis();
    sendPing();
}

void tierStateOnDisconnect() {
    g_mode = MODE_DISCONNECTED;
    frameDecoderReset();
}

void tierStateOnPingFrame() {
    if (g_mode == MODE_PINGING) {
        g_mode = MODE_FRAMED;
    }
    // Pings outside the negotiation window are ignored. A host could
    // re-ping later but we don't renegotiate mid-session today.
}

void tierStatePoll() {
    if (g_mode == MODE_PINGING &&
        (millis() - g_pingStartMs) >= kPingTimeoutMs) {
        g_mode = MODE_TRANSPARENT;
    }
}

bool tierStateIsFramed() {
    return g_mode == MODE_FRAMED;
}

bool tierStateIsPinging() {
    return g_mode == MODE_PINGING;
}
