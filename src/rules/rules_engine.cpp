/*
 * rules_engine.cpp - Rule evaluation state machine (see rules_engine.h).
 */

#include "rules_engine.h"
#include "../config.h"
#include <math.h>
#include <stdlib.h>   // strtod (rulesParseWarnToken, #428)
#include <string.h>
#include <ctype.h>    // toupper

// Derived-warning band: percent of the alarm limit at which the WARNING tier
// engages (§11.2). config.h owns it; fall back if a profile omits it.
#ifndef WARNING_THRESHOLD_PERCENT
#define WARNING_THRESHOLD_PERCENT 90
#endif

void rulesRuleInit(RuleContext& ctx, const Rule& rule, uint32_t now) {
    const bool enabled = (rule.flags & RULE_FLAG_ENABLED) != 0;
    ctx.state = enabled ? RULE_INIT : RULE_DISABLED;
    ctx.stateEntryTime = now;
    ctx.faultStartTime = 0;
    ctx.nanSince = 0;
    ctx.warmupTime_ms = rule.warmupMs;
    ctx.persistTime_ms = rule.debounceMs;
    ctx.verdictPending = enabled ? 1 : 0;   // armed: no verdict yet (#669)
    ctx.clearSince = 0;
    ctx.warnSince = 0;
    ctx.warnFlags = 0;
}

namespace {

// Timer anchors use 0 for "not counting", so a stamp taken at now == 0 is 1
// (as nanSince does). elapsed() reads such a stamp as 0 ms instead of
// underflowing to ~UINT32_MAX and firing at once.
inline uint32_t stamp(uint32_t now) { return now ? now : 1; }
inline uint32_t elapsed(uint32_t since, uint32_t now) {
    const int32_t d = (int32_t)(now - since);
    return d > 0 ? (uint32_t)d : 0;
}

inline RuleWarnSide ctxSide(const RuleContext& ctx, uint8_t shift) {
    return (RuleWarnSide)((ctx.warnFlags >> shift) & RULE_CTX_SIDE_MASK);
}
inline void setCtxSide(RuleContext& ctx, uint8_t shift, RuleWarnSide side) {
    ctx.warnFlags = (uint8_t)((ctx.warnFlags & ~(RULE_CTX_SIDE_MASK << shift)) |
                              (((uint8_t)side & RULE_CTX_SIDE_MASK) << shift));
}
// Latch (or drop) the approach tier at `side` (NONE drops it).
inline void setWarnLatch(RuleContext& ctx, RuleWarnSide side) {
    if (side == RULE_WARN_SIDE_NONE) {
        ctx.warnFlags &= (uint8_t)~RULE_CTX_WARN_LATCHED;
    } else {
        ctx.warnFlags |= RULE_CTX_WARN_LATCHED;
    }
    setCtxSide(ctx, RULE_CTX_WARN_SIDE_SHIFT, side);
}

// Which limit an assertion is on: the trip side the broadcaster reports while
// ACTIVE, latched at the trip so a value wandering through the clear window
// can't flip HIGH/LOW (#668).
RuleWarnSide tripSide(const Rule& r, float value) {
    switch (r.condition) {
        case RULE_BELOW: return RULE_WARN_SIDE_LO;
        case RULE_OUTSIDE_BAND:
            return (value >= (r.limitLo + r.limitHi) * 0.5f) ? RULE_WARN_SIDE_HI
                                                             : RULE_WARN_SIDE_LO;
        default: return RULE_WARN_SIDE_HI;
    }
}

// The approach tier (§11.2, #668): a READY ALARM rule's WARNING changes only
// once the reading has held on the new side of a warn edge for debounceMs, in
// both directions. Moving to the other side's band while latched refreshes the
// side at once (an edge edit or a coarse sample can jump lo to hi).
void updateWarnLatch(const Rule& rule, RuleContext& ctx, float value, uint32_t now) {
    if (rule.severity != RULE_SEV_ALARM) {
        setWarnLatch(ctx, RULE_WARN_SIDE_NONE);
        ctx.warnSince = 0;
        return;
    }
    const RuleWarnSide side = rulesRuleWarnSideHit(rule, value);
    const bool in = side != RULE_WARN_SIDE_NONE;
    const bool latched = (ctx.warnFlags & RULE_CTX_WARN_LATCHED) != 0;
    if (in == latched) {
        ctx.warnSince = 0;
        if (in) setWarnLatch(ctx, side);
        return;
    }
    if (ctx.warnSince == 0) ctx.warnSince = stamp(now);
    if (elapsed(ctx.warnSince, now) >= ctx.persistTime_ms) {
        setWarnLatch(ctx, side);
        ctx.warnSince = 0;
    }
}

}  // namespace

bool rulesRuleConditionAsserted(const Rule& rule, float value, bool currentlyActive) {
    // Safety: an untrustworthy (NaN) reading never asserts. rulesRuleUpdate()
    // keeps an ACTIVE rule through a NaN gap shorter than the fault debounce
    // (#650) and only then lets this de-assert it.
    if (isnan(value)) {
        return false;
    }

    const float hyst = rule.hysteresis;

    switch (rule.condition) {
        case RULE_ABOVE:
            // assert above limitLo; once active, hold until value falls a full
            // hysteresis band below the limit.
            return currentlyActive ? (value > rule.limitLo - hyst)
                                   : (value > rule.limitLo);

        case RULE_BELOW:
            // assert below limitLo; once active, hold until value rises a full
            // hysteresis band above the limit.
            return currentlyActive ? (value < rule.limitLo + hyst)
                                   : (value < rule.limitLo);

        case RULE_OUTSIDE_BAND:
            // assert outside [limitLo, limitHi]; clear band insets each edge.
            return currentlyActive
                       ? (value < rule.limitLo + hyst || value > rule.limitHi - hyst)
                       : (value < rule.limitLo || value > rule.limitHi);

        case RULE_INSIDE_BAND:  // deferred (§11.2) — never asserts in v1
        default:
            return false;
    }
}

void rulesRuleUpdate(const Rule& rule, RuleContext& ctx, float value, uint32_t now) {
    const bool enabled = (rule.flags & RULE_FLAG_ENABLED) != 0;
    if (!enabled) {
        ctx.state = RULE_DISABLED;
        ctx.faultStartTime = 0;
        ctx.nanSince = 0;
        ctx.verdictPending = 0;
        ctx.clearSince = 0;
        ctx.warnSince = 0;
        ctx.warnFlags = 0;
        return;
    }

    // Maintain nanSince BEFORE the state switch so WARMUP/INIT stamp it too
    // (#443) — a thermocouple unplugged from boot never leaves WARMUP, and
    // that is exactly a case rulesRuleSourceFaulted() must be able to report.
    // `now ? now : 1` keeps 0 meaning "finite": a test harness or a boot-time
    // tick can legitimately pass now == 0, and 0 must never be mistaken for
    // "not faulted" once nanSince is deliberately stamped.
    if (isnan(value)) {
        if (ctx.nanSince == 0) ctx.nanSince = now ? now : 1;
    } else {
        ctx.nanSince = 0;
    }

    switch (ctx.state) {
        case RULE_DISABLED:
            // Enabled but unarmed (e.g. enabled without an explicit init) — arm.
            rulesRuleInit(ctx, rule, now);
            return;

        case RULE_INIT:
            // Brief stabilization so an uninitialized reading can't trip a rule.
            if (now - ctx.stateEntryTime > RULES_INIT_MS) {
                ctx.state = RULE_WARMUP;
                ctx.stateEntryTime = now;
            }
            return;

        case RULE_WARMUP:
            // Block evaluation until the sensor settles and warmup expires.
            if (!isnan(value) && (now - ctx.stateEntryTime > ctx.warmupTime_ms)) {
                ctx.state = RULE_READY;
                ctx.stateEntryTime = now;
                ctx.clearSince = 0;
                ctx.warnSince = 0;
                ctx.warnFlags = 0;
            }
            return;

        case RULE_READY:
        case RULE_ACTIVE:
            break;  // fall through to qualification
    }

    // ===== Qualification (READY / ACTIVE) =====
    // Every state change waits debounceMs of continuous readings on the new
    // side, in both directions (#668, §11.2): entering ACTIVE (faultStartTime),
    // leaving it (clearSince), the first verdict (#669, clearSince) and the
    // approach tier (warnSince). A sample on the old side restarts the count.
    //
    // A NaN gap restarts every running timer and the rule keeps its state and
    // its warning (#650: one dropped conversion or CAN frame must not drop a
    // running fan). Past the fault debounce the source is untrusted: an ACTIVE
    // rule de-asserts at once (no clear timer: a NaN is no clear reading) and
    // the warning drops (a dead sensor is approaching nothing).
    if (isnan(value)) {
        ctx.faultStartTime = 0;
        ctx.clearSince = 0;
        ctx.warnSince = 0;
        if (rulesRuleSourceUntrusted(ctx, now)) {
            ctx.state = RULE_READY;
            ctx.warnFlags = 0;
        }
        return;
    }

    const bool active = ctx.state == RULE_ACTIVE;
    // Hysteresis-inset test while ACTIVE, the limit while READY (unchanged).
    const bool asserted = rulesRuleConditionAsserted(rule, value, active);

    if (active) {
        if (asserted) {
            ctx.clearSince = 0;
            // A reading past the OTHER limit (after a held NaN gap or a coarse
            // jump) re-latches the trip side, so the frame never reports HIGH
            // for a LOW alarm. Inside the band (the clear window) it stays put.
            if (rule.condition == RULE_OUTSIDE_BAND &&
                (value < rule.limitLo || value > rule.limitHi)) {
                setCtxSide(ctx, RULE_CTX_TRIP_SIDE_SHIFT,
                           value > rule.limitHi ? RULE_WARN_SIDE_HI : RULE_WARN_SIDE_LO);
            }
            return;
        }
        if (ctx.clearSince == 0) ctx.clearSince = stamp(now);
        if (elapsed(ctx.clearSince, now) >= ctx.persistTime_ms) {
            ctx.state = RULE_READY;
            ctx.faultStartTime = 0;
            ctx.clearSince = 0;
            ctx.warnSince = 0;
            // Coming down from ALARM through the warn band shows WARNING at
            // once: the clear itself was debounced, and waiting another
            // debounceMs would show a false NORMAL in between.
            setWarnLatch(ctx, rule.severity == RULE_SEV_ALARM
                                  ? rulesRuleWarnSideHit(rule, value)
                                  : RULE_WARN_SIDE_NONE);
        }
        return;
    }

    // READY
    if (asserted) {
        ctx.clearSince = 0;
        if (ctx.faultStartTime == 0) ctx.faultStartTime = stamp(now);
        if (elapsed(ctx.faultStartTime, now) >= ctx.persistTime_ms) {
            ctx.state = RULE_ACTIVE;
            ctx.verdictPending = 0;    // qualified: a verdict (#669)
            ctx.warnSince = 0;
            setWarnLatch(ctx, RULE_WARN_SIDE_NONE);
            setCtxSide(ctx, RULE_CTX_TRIP_SIDE_SHIFT, tripSide(rule, value));
            return;
        }
        // else: asserted but not yet debounced — wait
    } else {
        ctx.faultStartTime = 0;
        // The first trusted verdict since arming (#669, §11.3): "not demanding"
        // needs the reading to clear the ACTIVE (hysteresis-inset) test for
        // debounceMs, as if the rule had been ACTIVE: after a reset it can't
        // know an actuator was running, and a reading inside the band is what
        // a running fan leaves behind.
        if (ctx.verdictPending) {
            if (rulesRuleConditionAsserted(rule, value, true)) {
                ctx.clearSince = 0;    // in the hysteresis band: no verdict
            } else {
                if (ctx.clearSince == 0) ctx.clearSince = stamp(now);
                if (elapsed(ctx.clearSince, now) >= ctx.persistTime_ms) {
                    ctx.verdictPending = 0;
                    ctx.clearSince = 0;
                }
            }
        }
    }
    updateWarnLatch(rule, ctx, value, now);
}

bool rulesRuleIsActive(const RuleContext& ctx) {
    return ctx.state == RULE_ACTIVE;
}

bool rulesRuleUnsettled(const RuleContext& ctx) {
    // ACTIVE is a verdict by definition (qualifying clears the flag too); the
    // state test keeps a demanding rule from ever reading as unsettled.
    return ctx.state != RULE_DISABLED && ctx.state != RULE_ACTIVE &&
           ctx.verdictPending != 0;
}

bool rulesRuleSourceUntrusted(const RuleContext& ctx, uint32_t now) {
    if (ctx.state == RULE_DISABLED) return false;
    if (ctx.nanSince == 0) return false;
    return (now - ctx.nanSince) >= RULES_FAULT_DEBOUNCE_MS;
}

bool rulesRuleSourceFaulted(const RuleContext& ctx, uint32_t now) {
    if (ctx.state == RULE_DISABLED || ctx.state == RULE_INIT) return false;
    if (ctx.nanSince == 0) return false;
    // WARMUP gets the longer of the two: a CAN-sourced input reads NaN until
    // its first frame, which can be tens of seconds after key-on, and the
    // rule's own warmupMs already models that startup latency (#443 review).
    uint32_t required = RULES_FAULT_DEBOUNCE_MS;
    if (ctx.state == RULE_WARMUP && ctx.warmupTime_ms > required) {
        required = ctx.warmupTime_ms;
    }
    return (now - ctx.nanSince) >= required;
}

bool rulesRuleTripPending(const RuleContext& ctx) {
    return ctx.state == RULE_READY && ctx.faultStartTime != 0;
}

bool rulesRuleWarnPending(const RuleContext& ctx) {
    return ctx.state == RULE_READY && ctx.warnSince != 0 &&
           (ctx.warnFlags & RULE_CTX_WARN_LATCHED) == 0;
}

RuleWarnSide rulesRuleWarnSide(const RuleContext& ctx) {
    if (ctx.state != RULE_READY || (ctx.warnFlags & RULE_CTX_WARN_LATCHED) == 0) {
        return RULE_WARN_SIDE_NONE;
    }
    return ctxSide(ctx, RULE_CTX_WARN_SIDE_SHIFT);
}

RuleWarnSide rulesRuleTripSide(const RuleContext& ctx) {
    if (ctx.state != RULE_ACTIVE) return RULE_WARN_SIDE_NONE;
    return ctxSide(ctx, RULE_CTX_TRIP_SIDE_SHIFT);
}

RuleSeverity rulesRuleSeverity(const Rule& rule, const RuleContext& ctx) {
    return rulesRuleIsActive(ctx) ? (RuleSeverity)rule.severity : RULE_SEV_NONE;
}

float rulesRuleWarnEdge(const Rule& r, bool hiSide) {
    // ABOVE/BELOW are one-sided: there is no hi edge regardless of mode.
    if (hiSide && (r.condition == RULE_ABOVE || r.condition == RULE_BELOW)) {
        return NAN;
    }
    const RuleWarnMode mode = ruleWarnMode(r, hiSide);
    if (mode == RULE_WARN_NONE) {
        return NAN;
    }
    if (mode == RULE_WARN_SET) {
        return hiSide ? r.warnHi : r.warnLo;
    }
    // RULE_WARN_AUTO: derive from WARNING_THRESHOLD_PERCENT of the limit's
    // MAGNITUDE, applied toward the limit. Using fabsf keeps the band on the
    // correct (pre-limit) side for non-positive limits — a plain limit*pct
    // flips sides once the limit is <= 0 (e.g. a sub-zero low edge), making
    // WARNING fire only AFTER the ALARM threshold. For positive limits this is
    // identical to the old limit*pct / limit*(2-pct) form.
    const float pct = WARNING_THRESHOLD_PERCENT / 100.0f;
    switch (r.condition) {
        case RULE_ABOVE:
            return r.limitLo - (1.0f - pct) * fabsf(r.limitLo);   // approaching from below
        case RULE_BELOW:
            return r.limitLo + (1.0f - pct) * fabsf(r.limitLo);   // approaching from above
        case RULE_OUTSIDE_BAND:
            return hiSide ? (r.limitHi - (1.0f - pct) * fabsf(r.limitHi))
                          : (r.limitLo + (1.0f - pct) * fabsf(r.limitLo));
        default:  // INSIDE_BAND (deferred) / unknown
            return NAN;
    }
}

RuleWarnSide rulesRuleWarnSideHit(const Rule& r, float value) {
    if (isnan(value)) {
        return RULE_WARN_SIDE_NONE;
    }
    switch (r.condition) {
        case RULE_ABOVE: {
            const float edge = rulesRuleWarnEdge(r, false);
            return (!isnan(edge) && value >= edge) ? RULE_WARN_SIDE_LO : RULE_WARN_SIDE_NONE;
        }
        case RULE_BELOW: {
            const float edge = rulesRuleWarnEdge(r, false);
            return (!isnan(edge) && value <= edge) ? RULE_WARN_SIDE_LO : RULE_WARN_SIDE_NONE;
        }
        case RULE_OUTSIDE_BAND: {
            const float hiEdge = rulesRuleWarnEdge(r, true);
            const float loEdge = rulesRuleWarnEdge(r, false);
            if (!isnan(hiEdge) && value >= hiEdge) return RULE_WARN_SIDE_HI;
            if (!isnan(loEdge) && value <= loEdge) return RULE_WARN_SIDE_LO;
            return RULE_WARN_SIDE_NONE;
        }
        default:  // INSIDE_BAND (deferred) / unknown
            return RULE_WARN_SIDE_NONE;
    }
}

// Case-insensitive keyword compare against a PROGMEM literal. The CLI upcases
// only argv[0]; arguments arrive as typed, and every neighbouring keyword
// (MIN/MAX/WARN) is matched case-insensitively by console/'s streq_P — which
// rules/ can't include (layering), hence this four-line local.
static bool tokenIs_P(const char* tok, const char* kw_P) {
    for (;; ++tok, ++kw_P) {
        const char k = (char)pgm_read_byte(kw_P);
        if (toupper((unsigned char)*tok) != k) return false;
        if (k == '\0') return true;
    }
}

bool rulesParseNumber(const char* tok, float& val) {
    if (!tok || !*tok) return false;
    char* end = nullptr;
    const float v = (float)strtod(tok, &end);   // strtod, not strtof: avr-libc has no strtof
    // Whole token must be consumed, and the result must be a real number —
    // strtof also accepts "nan"/"inf", which are not limits or edges.
    if (end == tok || *end != '\0' || isnan(v) || isinf(v)) return false;
    val = v;
    return true;
}

bool rulesParseWarnToken(const char* tok, RuleWarnMode& mode, float& val) {
    if (!tok || !*tok) return false;
    if (tokenIs_P(tok, PSTR("NONE"))) { mode = RULE_WARN_NONE; val = 0.0f; return true; }
    if (tokenIs_P(tok, PSTR("AUTO"))) { mode = RULE_WARN_AUTO; val = 0.0f; return true; }
    if (!rulesParseNumber(tok, val)) return false;
    mode = RULE_WARN_SET;
    return true;
}

bool rulesWarnSideValid(const Rule& r, bool hiSide) {
    if (ruleWarnMode(r, hiSide) != RULE_WARN_SET) {
        return true;  // AUTO/NONE are always legal — nothing to validate
    }
    if (hiSide && (r.condition == RULE_ABOVE || r.condition == RULE_BELOW)) {
        return false;  // one-sided rule has no hi side
    }
    switch (r.condition) {
        case RULE_ABOVE:
            return r.warnLo < r.limitLo;
        case RULE_BELOW:
            return r.warnLo > r.limitLo;
        case RULE_OUTSIDE_BAND: {
            const bool otherSet = ruleWarnMode(r, !hiSide) == RULE_WARN_SET;
            if (hiSide) {
                if (!(r.limitLo < r.warnHi && r.warnHi < r.limitHi)) return false;
                return !otherSet || (r.warnLo < r.warnHi);
            }
            if (!(r.limitLo < r.warnLo && r.warnLo < r.limitHi)) return false;
            return !otherSet || (r.warnLo < r.warnHi);
        }
        default:  // INSIDE_BAND (deferred) — no defined warn shape
            return false;
    }
}

uint8_t rulesNormalizeWarn(Rule& r) {
    // Evaluate both sides against `r` as passed in BEFORE mutating either —
    // degrading the lo side first must not change what the hi-side check saw
    // (the OUTSIDE_BAND both-SET cross-check reads the other side's value).
    const bool loBad = !rulesWarnSideValid(r, false);
    const bool hiBad = !rulesWarnSideValid(r, true);
    uint8_t mask = 0;
    if (loBad) {
        ruleSetWarnMode(r, false, RULE_WARN_AUTO);
        r.warnLo = 0.0f;
        mask |= 0x01;
    }
    if (hiBad) {
        ruleSetWarnMode(r, true, RULE_WARN_AUTO);
        r.warnHi = 0.0f;
        mask |= 0x02;
    }
    return mask;
}

RuleSeverity rulesRuleAnnunciatorSeverity(const Rule& rule, const RuleContext& ctx) {
    if (rulesRuleIsActive(ctx)) {
        return (RuleSeverity)rule.severity;
    }
    // Approach tier: an armed ALARM rule annunciates WARNING while its latched
    // warning is set (rulesRuleUpdate() debounces it both ways, #668). A side
    // set to RULE_WARN_NONE never latches, so that side goes straight READY ->
    // ACTIVE/ALARM with no pre-announcement (§11.2 amendment A6).
    if (rule.severity == RULE_SEV_ALARM && rulesRuleWarnSide(ctx) != RULE_WARN_SIDE_NONE) {
        return RULE_SEV_WARNING;
    }
    return RULE_SEV_NONE;
}

bool rulesActionDesired(const Action& action, const ActionDrive& drive,
                          bool manualState, ActionOverride override) {
    // Operator override outranks everything, including the failsafe (#329): a
    // deliberate FORCE is the tool of last resort when the sensor is exactly what
    // died. The manager reports the held-against-a-faulted-source case loudly on
    // both the CLI and the BLE frame so it is never silent.
    if (override == ACT_OVR_FORCE_ON)  return true;
    if (override == ACT_OVR_FORCE_OFF) return false;
    // Manual-only: driven solely by explicit ON/OFF.
    if (drive.memberCount == 0) {
        return manualState;
    }
    // OR over members (§11.6): any live rule demanding the actuator wins — and it
    // is evaluated BEFORE the failsafe, so a second, healthy sensor still holds a
    // cooling fan on when its sibling's sensor dies.
    if (drive.anyActive) {
        return true;
    }
    // No live demand, and at least one member can't be evaluated (its source is
    // faulted) or hasn't given its first verdict yet (#669: not sure yet counts
    // as failed) → a RELAY assumes its failsafe state (§11.3); other action
    // types simply de-assert.
    if (drive.anyFaulted || drive.anyUnsettled) {
        if (action.type == ACT_RELAY) {
            return action.failsafe == ACT_FAILSAFE_ON;
        }
        return false;
    }
    return false;
}
