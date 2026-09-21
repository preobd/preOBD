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

// Change key = the (severity, kind) tuple plus the identity the frame will
// carry (ruleId, inputIndex). Frames fire on transitions of severity/kind, not
// on every value wiggle — the payload's value/threshold are rebuilt at send
// time and are NOT part of the change comparison. Identity is in the key so
// the CLEAR that follows a deleted/replaced rule can name the rule the client
// is actually holding (#429): once RULE DEL / CLEAR <pin> / ALARM DISABLE has
// zeroed the slot, rules[i].id reads 0xFF and there is nowhere else to get it.
struct AlarmKey { uint8_t severity; uint8_t kind; uint8_t ruleId; uint8_t inputIdx; };
inline bool isQuiet(const AlarmKey& k) { return k.severity == PREOBD_ALARM_SEVERITY_NORMAL; }
inline bool sameKey(const AlarmKey& a, const AlarmKey& b) {
    // Two quiet keys are the same regardless of identity: a slot that goes
    // from "empty" to "holds rule 7, not firing" has nothing to tell a client,
    // and a quiet slot whose rule is deleted must NOT emit a CLEAR for 0xFF.
    if (isQuiet(a) && isQuiet(b)) return true;
    return a.severity == b.severity && a.kind == b.kind &&
           a.ruleId == b.ruleId && a.inputIdx == b.inputIdx;
}
constexpr AlarmKey kQuietKey{ PREOBD_ALARM_SEVERITY_NORMAL, PREOBD_ALARM_KIND_CLEAR,
                              RULES_ID_NONE, 0xFF };

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

// Hold the previous ACCEPTED key across two windows where evalSlot()'s
// instantaneous truth would otherwise flash a misleading transient onto the
// wire (#443 review). Called only when a real Input is configured on the
// rule's source pin (an orphan rule is inert and never faults — #345 parity,
// checked by the caller) and only after the caller has already ruled out
// "faulted right now" (so ctx.nanSince != 0 here always means PENDING, not
// past the debounce):
//
//   Window 1 — fault PENDING: the reading is NAN and ctx.nanSince is
//   stamped, but the FAULT debounce hasn't elapsed. rulesManagerUpdate()
//   runs every 50ms and de-asserts an ACTIVE rule to READY on the FIRST NaN
//   tick (NaN never asserts, §11.3), so evalSlot()'s truthful key right now
//   is NONE/CLEAR. Without this, a rule's source dying goes
//   ALARM -> CLEAR (<=50ms) -> FAULT (+2s) instead of the correct
//   ALARM -> FAULT.
//
//   Window 2 — fault CLEARING while re-tripping: the reading has returned
//   and is (still) past the limit, so the rule sits READY with its
//   assertion re-debouncing toward ACTIVE (rulesRuleTripPending()). The last
//   accepted key is the held FAULT; without this it flashes CLEAR for the
//   persist window before reasserting ALARM.
bool holdAcrossFaultTransition(const AlarmKey& last, const RuleContext& ctx) {
    if (ctx.nanSince != 0) return true;
    if (last.kind == PREOBD_ALARM_KIND_FAULT && rulesRuleTripPending(ctx)) return true;
    return false;
}

// Evaluate slot i from the rules table: its (severity, kind, identity) key and
// the payload fields (value/threshold) needed to build a frame. Quiet
// (NORMAL/CLEAR) for an empty/non-alarm/not-firing rule. `last` is the last
// key this slot put on the wire — needed to decide the fault-transition hold
// above; it is NOT otherwise part of the per-tick evaluation.
AlarmKey evalSlot(int i, const AlarmKey& last, float& value, float& threshold) {
    ASSERT_INPUTS_TABLE_LIVE();  // rules[]/inputs[] aren't ground truth until here (#369)
    const Rule& r = rules[i];
    AlarmKey k = kQuietKey;
    value = 0.0f; threshold = 0.0f;

    // Notification path (not annunciation): includes INFO, which notifies via
    // the log timeline but never lights the LED/buzzer (#308). INFO<WARNING in
    // the RuleSeverity ordinal domain, so this gate is `>= RULE_SEV_INFO`.
    const bool isNotifyRule = (r.id != RULES_ID_NONE) &&
                              (r.severity >= RULE_SEV_INFO) &&
                              (r.flags & RULE_FLAG_ENABLED);
    if (isNotifyRule) {
        Input* in = getInputByPin(r.sourceInput);
        RuleContext& ctx = g_ruleContexts[i];
        value      = in ? in->value : NAN;
        k.ruleId   = r.id;
        k.inputIdx = getInputIndex(r.sourceInput);

        // FAULT (and the hold windows around it) only apply to a rule with a
        // real configured input on its source pin. An ORPHAN rule (no input
        // on the pin — CLEAR <pin> can leave these behind) is INERT, exactly
        // like sourceFaultedForRuleSlot() already treats it for Actions
        // (#345): it can never assert, so it must not announce a fault
        // either. Without this gate, CLEAR <pin> on a live faulted input
        // would leave the rule announcing a PERMANENT FAULT with
        // inputIndex 0xFF forever (getInputIndex returns 0xFF for an unowned
        // pin) instead of the truthful CLEAR nextKey() already produces for
        // a vacated identity.
        if (in != nullptr) {
            // A source reading that's been NAN past the debounce is announced
            // as FAULT/INFO instead of whatever rulesRuleAnnunciatorSeverity
            // would say (#443). This matters most for an ACTIVE rule: NAN
            // de-asserts it (rulesRuleConditionAsserted never asserts on NAN,
            // by design — a failsafe intent, §11.3), so without this branch
            // the broadcaster would emit NORMAL/CLEAR for a sensor that just
            // died mid-alarm — the opposite of what happened, and a false
            // "all clear" for the driver. INFO on purpose: a fault must
            // never sound the buzzer or light the LED (that's what an
            // Action's FAILSAFE is for) and is never acknowledged (#433) —
            // there is nothing to silence, only to log. FAULT is held
            // (non-quiet) like WARNING/ALARM so it survives the PING resync
            // (#430).
            if (rulesRuleSourceFaulted(ctx, millis())) {
                k.severity = PREOBD_ALARM_SEVERITY_INFO;
                k.kind     = PREOBD_ALARM_KIND_FAULT;
                value = 0.0f; threshold = 0.0f;
                return k;
            }
            if (holdAcrossFaultTransition(last, ctx)) {
                value = 0.0f; threshold = 0.0f;
                return last;
            }
        }
        // Includes the derived WARNING tier (§11.2): WARNING as the value
        // approaches the limit, ALARM once it trips.
        RuleSeverity asev = rulesRuleAnnunciatorSeverity(r, ctx, value);
        if (asev != RULE_SEV_NONE) {
            k.severity = toPreobdSeverity(asev);
            // READY here only happens via the approach-tier path in
            // rulesRuleAnnunciatorSeverity (ACTIVE rules return early above
            // it with ctx.state == RULE_ACTIVE) — so READY unambiguously
            // means "use the warn edge, not the midpoint".
            const bool approaching = (ctx.state == RULE_READY);
            k.kind     = kindFor(r, value, approaching);
            threshold  = thresholdFor(r, k.kind);
        }
    }
    return k;
}

// What slot i should announce NEXT. Normally evalSlot()'s truth — except when
// the rule the client is holding as firing is no longer the rule in the slot
// (deleted, or replaced by a new record in the same slot before this tick):
// then the next frame is a CLEAR for the identity we last sent, so the client
// can drop its held entry (#429). A replacement rule that is itself firing is
// announced on the following tick, once that CLEAR is cached.
AlarmKey nextKey(int i, float& value, float& threshold) {
    // lastKey() is defined from init()'s prime() on and survives a resync
    // invalidate, so this holds on the tick right after a PING too.
    const AlarmKey& last = g_bc.lastKey(i);
    AlarmKey k = evalSlot(i, last, value, threshold);
    if (!isQuiet(last) && last.ruleId != k.ruleId) {
        value = 0.0f; threshold = 0.0f;
        return AlarmKey{ PREOBD_ALARM_SEVERITY_NORMAL, PREOBD_ALARM_KIND_CLEAR,
                         last.ruleId, last.inputIdx };
    }
    return k;
}

} // namespace

void init() {
    // Prime every slot to NORMAL/CLEAR so the first tick stays quiet until a
    // rule actually transitions (no heartbeat — alarms are edge events).
    g_bc.prime(kQuietKey);
}

void tick() {
    g_bc.tick(millis(), /*heartbeatMs=*/0,
        [](int i) {
            float value, threshold;
            return nextKey(i, value, threshold);
        },
        sameKey,
        [](int i, uint16_t seq) {
            float value, threshold;
            AlarmKey k = nextKey(i, value, threshold);
            // Never put an anonymous frame on the wire: a quiet key with no
            // identity (empty slot, or a resync racing a delete) has no client
            // entry to clear. Report it sent so the cache settles to quiet.
            if (k.ruleId == RULES_ID_NONE) return true;
            PreobdAlarmEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
            ev.inputIndex      = k.inputIdx;   // source input of the rule
            ev.severity        = k.severity;
            ev.kind            = k.kind;
            ev.sequence        = seq;
            ev.value           = value;
            ev.threshold       = threshold;
            ev.timestampMs     = millis();
            ev.ruleId          = k.ruleId;     // stable rule identity (#310):
                                               // lets a client tell apart two
                                               // rules sharing one sourceInput
                                               // (inputIndex alone can't). From
                                               // the KEY, not rules[i], so a
                                               // post-delete CLEAR still names
                                               // the rule the client holds (#429)
            return hwSerial1.sendFrame(FRAME_TYPE_ALARM,
                                       reinterpret_cast<const uint8_t*>(&ev),
                                       sizeof(ev));
        });
}

void resync() {
    // A central that (re)connects while a rule is held sees nothing until the
    // rule next transitions — alarms have no heartbeat (#430). The nRF raises
    // the PING on the 0->1 central edge (negotiation) and again whenever a
    // central subscribes to Alarm/Action/Live Values (#435), so a late or
    // second central gets the held slots once it can receive them. Re-announce only
    // the slots whose last accepted key is a held WARNING/ALARM/INFO; a quiet
    // slot has no tile for the client to rebuild, and invalidating all
    // MAX_RULES would push a CLEAR per slot down a link that just came up.
    // Ordering with the other resync handlers doesn't matter: each owns its
    // own cache.
    g_bc.invalidateActive([](int i) { return !isQuiet(g_bc.lastKey(i)); });
}

} // namespace AlarmBroadcaster

#else // !(INCLUDE_FRAMED_TRANSPORT && INCLUDE_RULES_ENGINE)

namespace AlarmBroadcaster {
void init() {}
void tick() {}
void resync() {}
}

#endif // INCLUDE_FRAMED_TRANSPORT && INCLUDE_RULES_ENGINE
