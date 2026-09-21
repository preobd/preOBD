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
}

bool rulesRuleConditionAsserted(const Rule& rule, float value, bool currentlyActive) {
    // Safety: an untrustworthy (NaN) reading never asserts. An already-active
    // rule therefore de-asserts when its source goes NaN.
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
            }
            return;

        case RULE_READY:
        case RULE_ACTIVE:
            break;  // fall through to qualification
    }

    // ===== Qualification (READY / ACTIVE) =====
    const bool asserted = rulesRuleConditionAsserted(rule, value, ctx.state == RULE_ACTIVE);
    if (asserted) {
        if (ctx.faultStartTime == 0) {
            ctx.faultStartTime = now;  // start debounce timer
        }
        if (now - ctx.faultStartTime >= ctx.persistTime_ms) {
            ctx.state = RULE_ACTIVE;
        }
        // else: asserted but not yet debounced — wait
    } else {
        ctx.faultStartTime = 0;
        if (ctx.state == RULE_ACTIVE) {
            ctx.state = RULE_READY;
        }
    }
}

bool rulesRuleIsActive(const RuleContext& ctx) {
    return ctx.state == RULE_ACTIVE;
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

// True when `value` has reached either side's effective WARNING edge.
static bool ruleInWarningBand(const Rule& rule, float value) {
    return rulesRuleWarnSideHit(rule, value) != RULE_WARN_SIDE_NONE;
}

// Case-insensitive keyword compare against a PROGMEM literal. The CLI upcases
// only argv[0]; arguments arrive as typed, and every neighbouring keyword
// (MIN/MAX/WARN) is matched case-insensitively by cli/'s streq_P — which
// rules/ can't include (layering), hence this four-line local.
static bool tokenIs_P(const char* tok, const char* kw_P) {
    for (;; ++tok, ++kw_P) {
        const char k = (char)pgm_read_byte(kw_P);
        if (toupper((unsigned char)*tok) != k) return false;
        if (k == '\0') return true;
    }
}

bool rulesParseWarnToken(const char* tok, RuleWarnMode& mode, float& val) {
    if (!tok || !*tok) return false;
    if (tokenIs_P(tok, PSTR("NONE"))) { mode = RULE_WARN_NONE; val = 0.0f; return true; }
    if (tokenIs_P(tok, PSTR("AUTO"))) { mode = RULE_WARN_AUTO; val = 0.0f; return true; }
    char* end = nullptr;
    const float v = (float)strtod(tok, &end);   // strtod, not strtof: avr-libc has no strtof
    // Whole token must be consumed, and the result must be a real number —
    // strtof also accepts "nan"/"inf", which are not edges.
    if (end == tok || *end != '\0' || isnan(v) || isinf(v)) return false;
    mode = RULE_WARN_SET;
    val  = v;
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

RuleSeverity rulesRuleAnnunciatorSeverity(const Rule& rule, const RuleContext& ctx, float value) {
    if (rulesRuleIsActive(ctx)) {
        return (RuleSeverity)rule.severity;
    }
    // Approach tier: an armed ALARM rule whose source has reached a per-side
    // warning edge annunciates WARNING first. A side set to RULE_WARN_NONE
    // never matches here, so that side goes straight READY -> ACTIVE/ALARM
    // with no pre-announcement (§11.2 amendment A6).
    if (rule.severity == RULE_SEV_ALARM && ctx.state == RULE_READY &&
        !isnan(value) && ruleInWarningBand(rule, value)) {
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
    // No live demand, and at least one member can't be evaluated → a RELAY assumes
    // its failsafe state (§11.3); other action types simply de-assert.
    if (drive.anyFaulted) {
        if (action.type == ACT_RELAY) {
            return action.failsafe == ACT_FAILSAFE_ON;
        }
        return false;
    }
    return false;
}
