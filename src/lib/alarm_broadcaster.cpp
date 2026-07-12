#include "alarm_broadcaster.h"

#include "../config.h"
#include "../control/control_types.h"  // defines ENABLE_CONTROL_ENGINE

#if ENABLE_FRAMED_TRANSPORT && ENABLE_CONTROL_ENGINE

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "transport_mux.h"
#include "system_config.h"
#include "slotted_broadcaster.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../control/control_manager.h"
#include "../control/control_engine.h"

extern MuxTransport hwSerial1;

namespace AlarmBroadcaster {

namespace {

// Change key = the (severity, kind) tuple, so alarm frames fire on transitions
// rather than on every value wiggle. The payload's value/threshold are rebuilt
// at send time and are NOT part of the change comparison.
struct AlarmKey { uint8_t severity; uint8_t kind; };
inline bool sameKey(const AlarmKey& a, const AlarmKey& b) {
    return a.severity == b.severity && a.kind == b.kind;
}

// Alarms have no heartbeat — they are edge-triggered transition events.
SlottedBroadcaster<AlarmKey, MAX_RULES> g_bc;

uint8_t toPreobdSeverity(RuleSeverity s) {
    switch (s) {
    case RULE_SEV_WARNING: return PREOBD_ALARM_SEVERITY_WARNING;
    case RULE_SEV_ALARM:   return PREOBD_ALARM_SEVERITY_ALARM;
    default:               return PREOBD_ALARM_SEVERITY_NORMAL;
    }
}

// Which edge of the rule the value is at/approaching → HIGH / LOW.
uint8_t kindFor(const Rule& r, float value) {
    switch (r.condition) {
    case RULE_ABOVE: return PREOBD_ALARM_KIND_HIGH;
    case RULE_BELOW: return PREOBD_ALARM_KIND_LOW;
    case RULE_OUTSIDE_BAND: {
        if (isnan(value)) return PREOBD_ALARM_KIND_HIGH;
        // Nearer side (works for both the alarm and the warning band).
        const float mid = (r.limitLo + r.limitHi) * 0.5f;
        return (value >= mid) ? PREOBD_ALARM_KIND_HIGH : PREOBD_ALARM_KIND_LOW;
    }
    default: return PREOBD_ALARM_KIND_HIGH;
    }
}

float thresholdFor(const Rule& r, uint8_t kind) {
    if (r.condition == RULE_OUTSIDE_BAND) {
        return (kind == PREOBD_ALARM_KIND_LOW) ? r.limitLo : r.limitHi;
    }
    return r.limitLo;  // ABOVE / BELOW use the single limit
}

// Evaluate slot i: return its (severity, kind) change key and fill the payload
// fields (value/threshold/inputIdx) needed to build the frame. NORMAL/CLEAR for
// an empty/non-alarm/quiet rule.
AlarmKey evalSlot(int i, float& value, float& threshold, uint8_t& inputIdx) {
    const Rule& r = rules[i];
    AlarmKey k{ PREOBD_ALARM_SEVERITY_NORMAL, PREOBD_ALARM_KIND_CLEAR };
    value = 0.0f; threshold = 0.0f; inputIdx = 0xFF;

    const bool isAlarmRule = (r.id != CONTROL_ID_NONE) &&
                             (r.severity >= RULE_SEV_WARNING) &&
                             (r.flags & RULE_FLAG_ENABLED);
    if (isAlarmRule) {
        Input* in = getInputByPin(r.sourceInput);
        value    = in ? in->value : NAN;
        inputIdx = getInputIndex(r.sourceInput);
        // Includes the derived WARNING tier (§11.2): WARNING as the value
        // approaches the limit, ALARM once it trips.
        RuleSeverity asev = controlRuleAnnunciatorSeverity(r, g_ruleContexts[i], value);
        if (asev != RULE_SEV_NONE) {
            k.severity = toPreobdSeverity(asev);
            k.kind     = kindFor(r, value);
            threshold  = thresholdFor(r, k.kind);
        }
    }
    return k;
}

} // namespace

void init() {
    // Prime every slot to NORMAL/CLEAR so the first tick stays quiet until a
    // rule actually transitions (no heartbeat — alarms are edge events).
    g_bc.prime(AlarmKey{ PREOBD_ALARM_SEVERITY_NORMAL, PREOBD_ALARM_KIND_CLEAR });
}

void tick() {
    g_bc.tick(millis(), /*heartbeatMs=*/0,
        [](int i) {
            float value, threshold; uint8_t inputIdx;
            return evalSlot(i, value, threshold, inputIdx);
        },
        sameKey,
        [](int i, uint16_t seq) {
            float value, threshold; uint8_t inputIdx;
            AlarmKey k = evalSlot(i, value, threshold, inputIdx);
            PreobdAlarmEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
            ev.inputIndex      = inputIdx;     // source input of the rule
            ev.severity        = k.severity;
            ev.kind            = k.kind;
            ev.sequence        = seq;
            ev.value           = value;
            ev.threshold       = threshold;
            ev.timestampMs     = millis();
            return hwSerial1.sendFrame(FRAME_TYPE_ALARM,
                                       reinterpret_cast<const uint8_t*>(&ev),
                                       sizeof(ev));
        });
}

} // namespace AlarmBroadcaster

#else // !(ENABLE_FRAMED_TRANSPORT && ENABLE_CONTROL_ENGINE)

namespace AlarmBroadcaster {
void init() {}
void tick() {}
}

#endif // ENABLE_FRAMED_TRANSPORT && ENABLE_CONTROL_ENGINE
