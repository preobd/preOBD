/*
 * control_engine.cpp - Rule evaluation state machine (see control_engine.h).
 */

#include "control_engine.h"
#include "../config.h"
#include <math.h>

// Derived-warning band: percent of the alarm limit at which the WARNING tier
// engages (§11.2). config.h owns it; fall back if a profile omits it.
#ifndef WARNING_THRESHOLD_PERCENT
#define WARNING_THRESHOLD_PERCENT 90
#endif

void controlRuleInit(RuleContext& ctx, const Rule& rule, uint32_t now) {
    const bool enabled = (rule.flags & RULE_FLAG_ENABLED) != 0;
    ctx.state = enabled ? RULE_INIT : RULE_DISABLED;
    ctx.stateEntryTime = now;
    ctx.faultStartTime = 0;
    ctx.warmupTime_ms = rule.warmupMs;
    ctx.persistTime_ms = rule.debounceMs;
}

bool controlRuleConditionAsserted(const Rule& rule, float value, bool currentlyActive) {
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

void controlRuleUpdate(const Rule& rule, RuleContext& ctx, float value, uint32_t now) {
    const bool enabled = (rule.flags & RULE_FLAG_ENABLED) != 0;
    if (!enabled) {
        ctx.state = RULE_DISABLED;
        ctx.faultStartTime = 0;
        return;
    }

    switch (ctx.state) {
        case RULE_DISABLED:
            // Enabled but unarmed (e.g. enabled without an explicit init) — arm.
            controlRuleInit(ctx, rule, now);
            return;

        case RULE_INIT:
            // Brief stabilization so an uninitialized reading can't trip a rule.
            if (now - ctx.stateEntryTime > CONTROL_INIT_MS) {
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
    const bool asserted = controlRuleConditionAsserted(rule, value, ctx.state == RULE_ACTIVE);
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

bool controlRuleIsActive(const RuleContext& ctx) {
    return ctx.state == RULE_ACTIVE;
}

RuleSeverity controlRuleSeverity(const Rule& rule, const RuleContext& ctx) {
    return controlRuleIsActive(ctx) ? (RuleSeverity)rule.severity : RULE_SEV_NONE;
}

// True when `value` has entered the warning band approaching the rule's limit
// (mirrors the legacy evaluateSeverity percentages, per side).
static bool ruleInWarningBand(const Rule& rule, float value) {
    // Inset the warning edge by (1 - pct) of the limit's MAGNITUDE, applied
    // toward the limit. Using fabsf keeps the band on the correct (pre-limit)
    // side for non-positive limits — a plain limit*pct flips sides once the
    // limit is <= 0 (e.g. a sub-zero low edge), making WARNING fire only AFTER
    // the ALARM threshold. For positive limits this is identical to the old
    // limit*pct / limit*(2-pct) form.
    const float pct = WARNING_THRESHOLD_PERCENT / 100.0f;
    const float marginLo = (1.0f - pct) * fabsf(rule.limitLo);
    const float marginHi = (1.0f - pct) * fabsf(rule.limitHi);
    switch (rule.condition) {
        case RULE_ABOVE:
            return value >= rule.limitLo - marginLo;   // approaching from below
        case RULE_BELOW:
            return value <= rule.limitLo + marginLo;   // approaching from above
        case RULE_OUTSIDE_BAND:
            return (value >= rule.limitHi - marginHi) ||
                   (value <= rule.limitLo + marginLo);
        default:  // INSIDE_BAND (deferred) / unknown
            return false;
    }
}

RuleSeverity controlRuleAnnunciatorSeverity(const Rule& rule, const RuleContext& ctx, float value) {
    if (controlRuleIsActive(ctx)) {
        return (RuleSeverity)rule.severity;
    }
    // Derived WARNING: an armed ALARM rule whose source is in the warning band
    // (or past the limit but not yet debounced) annunciates WARNING first.
    if (rule.severity == RULE_SEV_ALARM && ctx.state == RULE_READY &&
        !isnan(value) && ruleInWarningBand(rule, value)) {
        return RULE_SEV_WARNING;
    }
    return RULE_SEV_NONE;
}

bool controlActionDesired(const Action& action, bool ruleActive,
                          bool sourceTrustworthy, bool manualState) {
    // Manual-only: driven solely by explicit ON/OFF.
    if (action.ruleRef == CONTROL_ID_NONE) {
        return manualState;
    }
    // Untrustworthy source → a RELAY assumes its failsafe state (§11.3); other
    // action types simply de-assert when their rule can't be evaluated.
    if (!sourceTrustworthy) {
        if (action.type == ACT_RELAY) {
            return action.failsafe == ACT_FAILSAFE_ON;
        }
        return false;
    }
    return ruleActive;
}
