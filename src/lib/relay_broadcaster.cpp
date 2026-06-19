#include "relay_broadcaster.h"

#include "../config.h"

// Relay broadcasting needs both the relay subsystem and the framed transport
// it pushes notifies over. Either off → empty stubs below.
#if ENABLE_RELAY_OUTPUT && ENABLE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "system_config.h"
#include "transport_mux.h"
#include "../outputs/output_relay.h"

extern MuxTransport hwSerial1;

namespace RelayBroadcaster {

namespace {

PreobdRelayState g_lastSent[MAX_RELAYS];
bool             g_haveLast[MAX_RELAYS];
uint16_t         g_sequence = 0;

void packState(uint8_t i, PreobdRelayState& s) {
    const RelayConfig& cfg = systemConfig.relays[i];
    memset(&s, 0, sizeof(s));
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    s.relayIndex      = i;
    s.mode            = cfg.mode;
    s.state           = getRelayState(i) ? PREOBD_RELAY_STATE_ON
                                         : PREOBD_RELAY_STATE_OFF;
    s.inputIndex      = cfg.inputIndex;
    s.outputPin       = cfg.outputPin;
    s.thresholdOn     = cfg.thresholdOn;
    s.thresholdOff    = cfg.thresholdOff;
    // sequence stamped at emit time
}

// Compare every field except sequence (which the broadcaster owns).
bool sameIgnoringSequence(const PreobdRelayState& a, const PreobdRelayState& b) {
    return a.protocolVersion == b.protocolVersion &&
           a.relayIndex      == b.relayIndex &&
           a.mode            == b.mode &&
           a.state           == b.state &&
           a.inputIndex      == b.inputIndex &&
           a.outputPin       == b.outputPin &&
           a.thresholdOn     == b.thresholdOn &&
           a.thresholdOff    == b.thresholdOff;
}

} // namespace

void init() {
    memset(g_lastSent, 0, sizeof(g_lastSent));
    memset(g_haveLast, 0, sizeof(g_haveLast));
    g_sequence = 0;
}

void tick() {
    for (uint8_t i = 0; i < MAX_RELAYS; ++i) {
        PreobdRelayState snap;
        packState(i, snap);

        if (g_haveLast[i] && sameIgnoringSequence(snap, g_lastSent[i])) continue;

        snap.sequence = g_sequence;

        if (hwSerial1.sendFrame(FRAME_TYPE_RELAY,
                                reinterpret_cast<const uint8_t*>(&snap),
                                sizeof(snap))) {
            g_lastSent[i] = snap;
            g_haveLast[i] = true;
            ++g_sequence;
        }
        // On failure: leave cache alone; retry next tick.
    }
}

void handleInbound(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_RELAY_STATE_SIZE) return;
    PreobdRelayState req;
    memcpy(&req, payload, sizeof(req));

    if (req.relayIndex >= MAX_RELAYS) return;

    // Apply writable fields. setRelayMode / setRelayThresholds are the
    // existing CLI plumbing; they print user-facing warnings on threshold
    // ordering but do not return false for that case.
    const uint8_t mode = req.mode;
    if (mode <= PREOBD_RELAY_MODE_MANUAL_OFF) {
        setRelayMode(req.relayIndex, static_cast<RelayMode>(mode));
    }
    // Thresholds only meaningful for AUTO modes; harmless to apply otherwise.
    setRelayThresholds(req.relayIndex, req.thresholdOn, req.thresholdOff);

    // Invalidate the cache so tick() emits a confirmation notify with the
    // new sequence number on the next loop iteration.
    g_haveLast[req.relayIndex] = false;
}

} // namespace RelayBroadcaster

#else // !(ENABLE_RELAY_OUTPUT && ENABLE_FRAMED_TRANSPORT)

namespace RelayBroadcaster {
void init() {}
void tick() {}
void handleInbound(const uint8_t*, uint16_t) {}
}

#endif // ENABLE_RELAY_OUTPUT && ENABLE_FRAMED_TRANSPORT
