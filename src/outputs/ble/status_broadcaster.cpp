#include "status_broadcaster.h"

#include "../../config.h"

#if INCLUDE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <string.h>

#include "../../comms/ble_gatt_defs.h"
#include "../../comms/frame_codec.h"
#include "../../storage/system_config.h"
#include "../../core/system_mode.h"
#include "../../comms/transport_mux.h"
#include "../../inputs/input_manager.h"
#include "../../rules/rules_manager.h"
#include "../../core/config_generation.h"
#include "../output_alarm.h"
#include "../../version.h"


// Declared in main.cpp; the single MuxTransport instance bound to Serial1.
extern MuxTransport hwSerial1;

namespace StatusBroadcaster {

namespace {

constexpr uint32_t kHeartbeatMs = 5000;

PreobdSystemStatus g_lastSent;
PreobdStatusExt    g_lastSentExt;
uint32_t g_lastSendMs = 0;
bool g_haveSentOnce = false;

void packStatus(PreobdSystemStatus& s) {
    ASSERT_INPUTS_TABLE_LIVE();  // numActiveInputs isn't ground truth until here (#369)
    memset(&s, 0, sizeof(s));
    s.magic = PREOBD_STATUS_MAGIC;
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    s.systemMode = (getCurrentMode() == MODE_RUN)
                       ? PREOBD_MODE_RUN
                       : PREOBD_MODE_CONFIG;
    s.activeInputCount = numActiveInputs;

    // relayCount byte now reports configured control-engine Actions
    // (a relay is an Action of type RELAY — §11). Field name kept for
    // wire compatibility with PreobdSystemStatus.
    uint8_t actionCount = 0;
#if INCLUDE_RULES_ENGINE
    actionCount = rulesActionCount();
#endif
    s.relayCount = actionCount;

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

// The extension rides as its own frame (0x07) just before each 0x03 — see
// ble_gatt_defs.h PreobdStatusExt for why it is not simply a longer 0x03.
void packExt(PreobdStatusExt& e) {
    memset(&e, 0, sizeof(e));
    e.extLayout = PREOBD_STATUS_EXT_LAYOUT;
#if INCLUDE_RULES_ENGINE
    e.ackedCount    = rulesAckCount();
    e.ackGeneration = rulesAckGeneration();
    if (e.ackedCount)              e.flags |= PREOBD_STATUS_FLAG_ACKED;
    if (alarmSilenceIncludesLed()) e.flags |= PREOBD_STATUS_FLAG_SILENCE_LED;
#endif
    e.configGeneration = configGeneration();
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
    memset(&g_lastSentExt, 0, sizeof(g_lastSentExt));
    g_lastSendMs = 0;
    g_haveSentOnce = false;
}

void tick() {
    PreobdSystemStatus snap;
    PreobdStatusExt    ext;
    packStatus(snap);
    packExt(ext);

    const uint32_t now = millis();
    const bool heartbeatDue = !g_haveSentOnce ||
                              (now - g_lastSendMs) >= kHeartbeatMs;
    const bool changed = g_haveSentOnce
                             ? (diffIgnoringUptime(snap, g_lastSent) ||
                                memcmp(&ext, &g_lastSentExt, sizeof(ext)) != 0)
                             : true;

    if (!changed && !heartbeatDue) return;

    // Both frames must fit the TX buffer at once, or the pair is refused
    // forever (see kMuxMaxSendableFrame). Growing the extension toward its
    // 32 B ceiling would cross this, so it fails the build instead.
    static_assert(sizeof(ext) + sizeof(snap) + 2 * (FRAME_HEADER_BYTES + 1) <= kMuxMaxSendableFrame,
                  "status + extension frames must fit the UART TX buffer together");
    // EXT first: the nRF holds it one-shot and appends it to the next 0x03.
    // All-or-nothing — a lone EXT per tick would never let the 0x03 through
    // on a TX buffer that is merely busy (see sendFramePair).
    if (hwSerial1.sendFramePair(FRAME_TYPE_STATUS_EXT,
                                reinterpret_cast<const uint8_t*>(&ext),
                                sizeof(ext),
                                FRAME_TYPE_STATUS,
                                reinterpret_cast<const uint8_t*>(&snap),
                                sizeof(snap))) {
        g_lastSent = snap;
        g_lastSentExt = ext;
        g_lastSendMs = now;
        g_haveSentOnce = true;
    }
    // On failure (not framed, backpressure, or flow-control gate closed),
    // leave g_lastSent / g_lastSendMs alone -- next tick retries.
}

void resync() {
    // A (re)connecting central gets the snapshot now rather than on the next
    // heartbeat (up to 5 s) — it carries the ack/config generations (#455).
    g_haveSentOnce = false;
}

} // namespace StatusBroadcaster

#else // !INCLUDE_FRAMED_TRANSPORT

namespace StatusBroadcaster {
void init() {}
void tick() {}
void resync() {}
}

#endif // INCLUDE_FRAMED_TRANSPORT
