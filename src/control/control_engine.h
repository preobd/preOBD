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
 * Aggregate of an Action's Rule membership, resolved by the manager (which owns
 * the rule contexts and the link table) and handed to the pure resolver below.
 * With M:N membership (#343) an Action can have several member Rules, so "is my
 * rule active" and "is my source trustworthy" are no longer single booleans.
 */
struct ActionDrive {
    uint8_t memberCount;  // configured member Rules (0 = manual-only). Counts a
                          //   member whose Rule record is missing (dangling ref),
                          //   so losing a rule can never fall back to the latch.
    bool    anyActive;    // OR over members whose source is trustworthy AND active
    bool    anyFaulted;   // any member's source is missing / NaN (§11.3)
};

/**
 * Resolve an Action's desired LOGICAL output (true = asserted), pre-invert and
 * pre-config-gate. Pure/testable; the manager applies inversion, the CONFIG
 * actuation gate, and the GPIO write.
 *
 *  - Operator override (`override != ACT_OVR_AUTO`): wins outright — ahead of
 *    the Rules AND ahead of the failsafe (#329). A forced state is the tool of
 *    last resort for the case where the sensor itself is what failed, so the
 *    failsafe must not be able to countermand it. The override is RAM-only and
 *    is surfaced (with a source-faulted flag) on the CLI and the BLE frame.
 *  - Manual action (`memberCount == 0`): follows `manualState`.
 *  - Any member Rule active on a trustworthy source: asserted (OR, §11.6).
 *  - Otherwise, with a faulted member source: a RELAY assumes its `failsafe`
 *    state — the wire/sensor is gone, so the reading is meaningless (§11.3).
 *    Only RELAY has a failsafe; other types de-assert.
 *
 * ORDER MATTERS with several members: a live member's ACTIVE verdict is checked
 * BEFORE the failsafe, so a valid demand outranks the unknown from a dead
 * sibling sensor — one broken sensor must not cut a fan that a healthy one is
 * legitimately calling for. At one member this is identical to the pre-#343
 * behavior (a faulted member is never `anyActive`).
 */
bool controlActionDesired(const Action& action, const ActionDrive& drive,
                          bool manualState, ActionOverride override);

#endif // CONTROL_ENGINE_H
