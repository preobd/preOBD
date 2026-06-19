#include "status_broadcaster.h"

#include "../config.h"

#if ENABLE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "system_config.h"
#include "system_mode.h"
#include "transport_mux.h"
#include "../inputs/input_manager.h"
#include "../version.h"

#if ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif

// Declared in main.cpp; the single MuxTransport instance bound to Serial1.
extern MuxTransport hwSerial1;

namespace StatusBroadcaster {

namespace {

constexpr uint32_t kHeartbeatMs = 5000;

PreobdSystemStatus g_lastSent;
uint32_t g_lastSendMs = 0;
bool g_haveSentOnce = false;

void packStatus(PreobdSystemStatus& s) {
    memset(&s, 0, sizeof(s));
    s.magic = PREOBD_STATUS_MAGIC;
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    s.systemMode = (getCurrentMode() == MODE_RUN)
                       ? PREOBD_MODE_RUN
                       : PREOBD_MODE_CONFIG;
    s.activeInputCount = numActiveInputs;

    uint8_t relayCount = 0;
#if ENABLE_RELAY_OUTPUT
    for (uint8_t i = 0; i < MAX_RELAYS; ++i) {
        if (systemConfig.relays[i].outputPin != 0xFF) ++relayCount;
    }
#endif
    s.relayCount = relayCount;

    s.uptimeSeconds = static_cast<uint32_t>(millis() / 1000UL);
    s.versionMajor  = FW_MAJOR;
    s.versionMinor  = FW_MINOR;
    s.versionPatch  = FW_PATCH;
    s.versionBuild  = static_cast<uint8_t>(FW_BUILD_NUMBER & 0xFF);

    // Identify the hardware build via the PlatformIO environment name
    // (e.g. "teensy41", "esp32s3dev", "mega2560"). Injected by
    // scripts/version_inject.py; falls back to "unknown" if absent.
#ifndef PIO_BUILD_ENV
#define PIO_BUILD_ENV "unknown"
#endif
    strncpy(s.deviceName, PIO_BUILD_ENV, sizeof(s.deviceName) - 1);
}

// Compare two packed structs ignoring uptimeSeconds (bytes 8..11). Returns
// true if any other field differs.
bool diffIgnoringUptime(const PreobdSystemStatus& a,
                        const PreobdSystemStatus& b) {
    const uint8_t* pa = reinterpret_cast<const uint8_t*>(&a);
    const uint8_t* pb = reinterpret_cast<const uint8_t*>(&b);
    if (memcmp(pa, pb, 8) != 0) return true;                 // magic..relayCount
    return memcmp(pa + 12, pb + 12, PREOBD_STATUS_SIZE - 12) != 0;  // version + deviceName
}

} // namespace

void init() {
    memset(&g_lastSent, 0, sizeof(g_lastSent));
    g_lastSendMs = 0;
    g_haveSentOnce = false;
}

void tick() {
    PreobdSystemStatus snap;
    packStatus(snap);

    const uint32_t now = millis();
    const bool heartbeatDue = !g_haveSentOnce ||
                              (now - g_lastSendMs) >= kHeartbeatMs;
    const bool changed = g_haveSentOnce
                             ? diffIgnoringUptime(snap, g_lastSent)
                             : true;

    if (!changed && !heartbeatDue) return;

    if (hwSerial1.sendFrame(FRAME_TYPE_STATUS,
                            reinterpret_cast<const uint8_t*>(&snap),
                            sizeof(snap))) {
        g_lastSent = snap;
        g_lastSendMs = now;
        g_haveSentOnce = true;
    }
    // On failure (not framed, backpressure, or flow-control gate closed),
    // leave g_lastSent / g_lastSendMs alone -- next tick retries.
}

} // namespace StatusBroadcaster

#else // !ENABLE_FRAMED_TRANSPORT

namespace StatusBroadcaster {
void init() {}
void tick() {}
}

#endif // ENABLE_FRAMED_TRANSPORT
