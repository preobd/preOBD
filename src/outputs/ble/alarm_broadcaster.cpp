#include "alarm_broadcaster.h"

#include "../../config.h"
#include "../../rules/rules_types.h"  // defines INCLUDE_RULES_ENGINE

#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_RULES_ENGINE

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "../../comms/ble_gatt_defs.h"
#include "../../comms/frame_codec.h"
#include "../../comms/transport_mux.h"
#include "../../storage/system_config.h"
#include "slotted_broadcaster.h"
#include "../../inputs/input.h"
#include "../../inputs/input_manager.h"
#include "../../rules/rules_manager.h"
#include "../../rules/rules_engine.h"

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
    case RULE_SEV_INFO:    return PREOBD_ALARM_SEVERITY_INFO;
    case RULE_SEV_WARNING: return PREOBD_ALARM_SEVERITY_WARNING;
    case RULE_SEV_ALARM:   return PREOBD_ALARM_SEVERITY_ALARM;
    default:               return PREOBD_ALARM_SEVERITY_NORMAL;
    }
}

// Which edge of the rule the value is at/approaching → HIGH / LOW.
// `approaching` selects the WARNING approach tier (asev == WARNING while
// ctx.state == RULE_READY — the derived/explicit warn edge, which the plan
// terms the approach tier, §11.2/#425): the effective warn edges can now sit
// asymmetrically off the band midpoint (a SET edge near one limit), so the
// midpoint heuristic below picks the WRONG side once that happens
// (OUTSIDE 0..100 WARN 80 90, value 75 -> midpoint says HIGH, but 75 is past
// the lo edge 80, not the hi edge 90). Once the rule is actually ACTIVE/ALARM
// (the trip case), there is no derived warn edge to consult — keep the
// midpoint rule, which is exactly which LIMIT (not warn edge) was crossed.
uint8_t kindFor(const Rule& r, float value, bool approaching) {
    switch (r.condition) {
    case RULE_ABOVE: return PREOBD_ALARM_KIND_HIGH;
    case RULE_BELOW: return PREOBD_ALARM_KIND_LOW;
    case RULE_OUTSIDE_BAND: {
        if (approaching) {
            const RuleWarnSide side = rulesRuleWarnSideHit(r, value);
            if (side == RULE_WARN_SIDE_HI) return PREOBD_ALARM_KIND_HIGH;
            if (side == RULE_WARN_SIDE_LO) return PREOBD_ALARM_KIND_LOW;
            // Shouldn't happen (evalSlot only asks when the rule IS in a
            // warning band) — fall through to the midpoint default below.
        }
        if (isnan(value)) return PREOBD_ALARM_KIND_HIGH;
        // Nearer side of the trip band (ACTIVE/ALARM case: which limit tripped).
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
    ASSERT_INPUTS_TABLE_LIVE();  // rules[]/inputs[] aren't ground truth until here (#369)
    const Rule& r = rules[i];
    AlarmKey k{ PREOBD_ALARM_SEVERITY_NORMAL, PREOBD_ALARM_KIND_CLEAR };
    value = 0.0f; threshold = 0.0f; inputIdx = 0xFF;

    // Notification path (not annunciation): includes INFO, which notifies via
    // the log timeline but never lights the LED/buzzer (#308). INFO<WARNING in
    // the RuleSeverity ordinal domain, so this gate is `>= RULE_SEV_INFO`.
    const bool isNotifyRule = (r.id != RULES_ID_NONE) &&
                              (r.severity >= RULE_SEV_INFO) &&
                              (r.flags & RULE_FLAG_ENABLED);
    if (isNotifyRule) {
        Input* in = getInputByPin(r.sourceInput);
        value    = in ? in->value : NAN;
        inputIdx = getInputIndex(r.sourceInput);
        // Includes the derived WARNING tier (§11.2): WARNING as the value
        // approaches the limit, ALARM once it trips.
        RuleSeverity asev = rulesRuleAnnunciatorSeverity(r, g_ruleContexts[i], value);
        if (asev != RULE_SEV_NONE) {
            k.severity = toPreobdSeverity(asev);
            // READY here only happens via the approach-tier path in
            // rulesRuleAnnunciatorSeverity (ACTIVE rules return early above
            // it with ctx.state == RULE_ACTIVE) — so READY unambiguously
            // means "use the warn edge, not the midpoint".
            const bool approaching = (g_ruleContexts[i].state == RULE_READY);
            k.kind     = kindFor(r, value, approaching);
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
            ev.ruleId          = rules[i].id;  // stable rule identity (#310):
                                               // lets a client tell apart two
                                               // rules sharing one sourceInput
                                               // (inputIndex alone can't)
            return hwSerial1.sendFrame(FRAME_TYPE_ALARM,
                                       reinterpret_cast<const uint8_t*>(&ev),
                                       sizeof(ev));
        });
}

} // namespace AlarmBroadcaster

#else // !(INCLUDE_FRAMED_TRANSPORT && INCLUDE_RULES_ENGINE)

namespace AlarmBroadcaster {
void init() {}
void tick() {}
}

#endif // INCLUDE_FRAMED_TRANSPORT && INCLUDE_RULES_ENGINE
