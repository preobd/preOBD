/*
 * control_engine.h - Rule evaluation (pure, deterministic, host-testable)
 *
 * The state machine and qualification logic are lifted from the per-input
 * alarm engine (alarm_logic.cpp) and generalized to a Rule's condition shape
 * (ABOVE / BELOW / OUTSIDE_BAND) with hysteresis + debounce + warmup
 * (ARCHITECTURE §11.2). No globals, no millis() — `now` is passed in, so the
 * whole machine is deterministic on the host.
 *
 * RuleContext is LOOP-ONLY runtime state (updated from the main loop's sensor
 * pass, never from an ISR or BLE callback), so the unsigned millis() diffs here
 * are safe — cf. the cross-context-underflow rule that bit the hall sensor.
 */

#ifndef CONTROL_ENGINE_H
#define CONTROL_ENGINE_H

#include "control_types.h"

// Post-arm stabilization before warmup begins. Mirrors the alarm INIT period.
#define CONTROL_INIT_MS 1000

/**
 * Arm a rule's runtime context. This is BOTH the create-from-template and the
 * boot-load path (§11.5): an enabled rule arms (INIT → WARMUP → READY); a
 * disabled rule sits in RULE_DISABLED. Effective warmup/debounce are seeded
 * from the Rule.
 *
 * Callers MUST NOT re-init an already-armed rule on a plain re-enable (the #254
 * idempotency lesson) — call this only on create / boot-load / RULE RESET.
 */
void controlRuleInit(RuleContext& ctx, const Rule& rule, uint32_t now);

/** Advance one rule's state machine against the current source value. */
void controlRuleUpdate(const Rule& rule, RuleContext& ctx, float value, uint32_t now);

/**
 * Pure condition test with hysteresis. `currentlyActive` selects the assert vs.
 * clear threshold band (clear band is inset by `hysteresis` from each limit).
 * NaN never asserts — an untrustworthy reading must not trip a rule, and an
 * already-active rule de-asserts (failsafe intent, §11.3).
 */
bool controlRuleConditionAsserted(const Rule& rule, float value, bool currentlyActive);

/** True while the rule's condition is qualified (debounced): state == RULE_ACTIVE. */
bool controlRuleIsActive(const RuleContext& ctx);

/** Configured severity if active, else RULE_SEV_NONE. */
RuleSeverity controlRuleSeverity(const Rule& rule, const RuleContext& ctx);

/**
 * Annunciator severity for a rule, including the DERIVED WARNING tier (§11.2):
 * an ALARM rule annunciates ALARM once ACTIVE, but also WARNING while armed
 * (READY) and its source has entered the warning band (>= WARNING_THRESHOLD_PERCENT
 * of the limit) — reproducing the old two-tier warn/alarm behavior from a
 * single stored threshold. WARNING-severity rules have no sub-tier (WARNING
 * when active, else NONE). This drives annunciation (LED/buzzer/AlarmBroadcaster)
 * only — NOT Action triggering, which keys on RULE_ACTIVE.
 */
RuleSeverity controlRuleAnnunciatorSeverity(const Rule& rule, const RuleContext& ctx, float value);

/**
 * Resolve an Action's desired LOGICAL output (true = asserted), pre-invert and
 * pre-config-gate. Pure/testable; the manager applies inversion, the CONFIG
 * actuation gate, and the GPIO write.
 *
 *  - Manual action (ruleRef == CONTROL_ID_NONE): follows `manualState`.
 *  - Rule-driven RELAY with an untrustworthy source (`!sourceTrustworthy`):
 *    assumes its `failsafe` state — the wire/sensor is gone, so the reading is
 *    meaningless (§11.3). Only RELAY has a failsafe; other types de-assert.
 *  - Otherwise: follows `ruleActive`.
 */
bool controlActionDesired(const Action& action, bool ruleActive,
                          bool sourceTrustworthy, bool manualState);

#endif // CONTROL_ENGINE_H
