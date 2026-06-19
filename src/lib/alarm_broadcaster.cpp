#include "alarm_broadcaster.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "transport_mux.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"

extern MuxTransport hwSerial1;

namespace AlarmBroadcaster {

namespace {

// Match alarm_logic.cpp's "disabled threshold" sentinels.
constexpr float kMaxDisabledSentinel = 999.0f;
constexpr float kMinDisabledSentinel = -999.0f;

uint8_t g_lastSeverity[MAX_INPUTS];
uint8_t g_lastKind[MAX_INPUTS];
uint16_t g_sequence = 0;

uint8_t severityFor(const Input& in) {
    switch (in.currentSeverity) {
    case SEVERITY_WARNING: return PREOBD_ALARM_SEVERITY_WARNING;
    case SEVERITY_ALARM:   return PREOBD_ALARM_SEVERITY_ALARM;
    default:               return PREOBD_ALARM_SEVERITY_NORMAL;
    }
}

// Pick HIGH vs LOW based on which threshold the value is closer to (or
// violating). Returns CLEAR when severity is NORMAL or no threshold is set.
uint8_t kindFor(const Input& in, uint8_t severity) {
    if (severity == PREOBD_ALARM_SEVERITY_NORMAL) return PREOBD_ALARM_KIND_CLEAR;

    const bool hasMax = in.maxValue < kMaxDisabledSentinel;
    const bool hasMin = in.minValue > kMinDisabledSentinel;
    if (hasMax && !hasMin) return PREOBD_ALARM_KIND_HIGH;
    if (hasMin && !hasMax) return PREOBD_ALARM_KIND_LOW;
    if (!hasMax && !hasMin) return PREOBD_ALARM_KIND_HIGH;  // shouldn't happen

    // Both thresholds set: decide by proximity. NaN value falls back to HIGH.
    if (isnan(in.value)) return PREOBD_ALARM_KIND_HIGH;
    const float midpoint = (in.maxValue + in.minValue) * 0.5f;
    return (in.value >= midpoint) ? PREOBD_ALARM_KIND_HIGH
                                  : PREOBD_ALARM_KIND_LOW;
}

float thresholdFor(const Input& in, uint8_t kind) {
    switch (kind) {
    case PREOBD_ALARM_KIND_HIGH: return in.maxValue;
    case PREOBD_ALARM_KIND_LOW:  return in.minValue;
    default:                     return 0.0f;
    }
}

} // namespace

void init() {
    memset(g_lastSeverity, PREOBD_ALARM_SEVERITY_NORMAL, sizeof(g_lastSeverity));
    memset(g_lastKind,     PREOBD_ALARM_KIND_CLEAR,      sizeof(g_lastKind));
    g_sequence = 0;
}

void tick() {
    const uint8_t count = (numActiveInputs <= MAX_INPUTS) ? numActiveInputs
                                                          : MAX_INPUTS;
    for (uint8_t i = 0; i < count; ++i) {
        const Input& in = inputs[i];
        if (!in.flags.isEnabled) continue;

        const uint8_t sev  = severityFor(in);
        const uint8_t kind = kindFor(in, sev);

        if (sev == g_lastSeverity[i] && kind == g_lastKind[i]) continue;

        PreobdAlarmEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
        ev.inputIndex      = i;
        ev.severity        = sev;
        ev.kind            = kind;
        ev.sequence        = g_sequence;
        ev.value           = in.value;
        ev.threshold       = thresholdFor(in, kind);
        ev.timestampMs     = millis();

        if (hwSerial1.sendFrame(FRAME_TYPE_ALARM,
                                reinterpret_cast<const uint8_t*>(&ev),
                                sizeof(ev))) {
            g_lastSeverity[i] = sev;
            g_lastKind[i]     = kind;
            ++g_sequence;
        }
        // On failure: leave cache alone; next tick retries with a fresh
        // sequence number stamped at emit time.
    }
}

} // namespace AlarmBroadcaster
