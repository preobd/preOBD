/*
 * rules_engine.h - Rule evaluation (pure, deterministic, host-testable)
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

#ifndef RULES_ENGINE_H
#define RULES_ENGINE_H

#include "rules_types.h"

// Post-arm stabilization before warmup begins. Mirrors the alarm INIT period.
#define RULES_INIT_MS 1000

/**
 * Arm a rule's runtime context. This is BOTH the create-from-template and the
 * boot-load path (§11.5): an enabled rule arms (INIT → WARMUP → READY); a
 * disabled rule sits in RULE_DISABLED. Effective warmup/debounce are seeded
 * from the Rule.
 *
 * Callers MUST NOT re-init an already-armed rule on a plain re-enable (the #254
 * idempotency lesson) — call this only on create / boot-load / RULE RESET.
 */
void rulesRuleInit(RuleContext& ctx, const Rule& rule, uint32_t now);

/** Advance one rule's state machine against the current source value. */
void rulesRuleUpdate(const Rule& rule, RuleContext& ctx, float value, uint32_t now);

/**
 * Pure condition test with hysteresis. `currentlyActive` selects the assert vs.
 * clear threshold band (clear band is inset by `hysteresis` from each limit).
 * NaN never asserts — an untrustworthy reading must not trip a rule, and an
 * already-active rule de-asserts (failsafe intent, §11.3).
 */
bool rulesRuleConditionAsserted(const Rule& rule, float value, bool currentlyActive);

/** True while the rule's condition is qualified (debounced): state == RULE_ACTIVE. */
bool rulesRuleIsActive(const RuleContext& ctx);

/** Configured severity if active, else RULE_SEV_NONE. */
RuleSeverity rulesRuleSeverity(const Rule& rule, const RuleContext& ctx);

/**
 * Effective WARNING edge on one side of `r`, in base units, or NaN if that
 * side has no approach tier right now (§11.2, #425):
 *   - RULE_WARN_NONE: always NaN.
 *   - RULE_WARN_AUTO: derived from WARNING_THRESHOLD_PERCENT of the limit's
 *     magnitude (today's single-threshold behaviour, per side).
 *   - RULE_WARN_SET: the stored warnLo/warnHi value, verbatim.
 * `hiSide` selects the OUTSIDE_BAND high edge; for ABOVE/BELOW (no hi side)
 * this always returns NaN when hiSide is true, regardless of mode.
 * Pure/host-testable; exported for the CLI row and JSON facade to show an
 * operator where an AUTO edge currently sits.
 */
float rulesRuleWarnEdge(const Rule& r, bool hiSide);

/**
 * Which effective WARNING edge (if either) `value` has reached, or
 * RULE_WARN_SIDE_NONE. For OUTSIDE_BAND the hi edge is checked first (matches
 * the pre-#425 midpoint tie-break when both would somehow hit). Used both to
 * decide "is this rule in its warning band" and, by the AlarmBroadcaster, to
 * pick HIGH/LOW for a WARNING event without re-deriving the edges itself.
 */
RuleWarnSide rulesRuleWarnSideHit(const Rule& r, float value);

/**
 * Is the SET edge on `hiSide` of `r` legal for its condition? AUTO/NONE sides
 * are always legal (nothing to validate). Strict inequalities — an edge equal
 * to a limit or to the opposite edge is a zero-width band and rejected.
 * Hysteresis is NOT considered (the approach tier is a pre-trip zone, not a
 * clear point). A hi-side SET on ABOVE/BELOW is never legal (no hi side).
 * Pure/host-testable; shared by every write path (CLI, JSON import, facade)
 * so firmware can never persist/export a record it would refuse to import.
 */
bool rulesWarnSideValid(const Rule& r, bool hiSide);

/**
 * Degrade every SET side of `r` that `rulesWarnSideValid` rejects to AUTO
 * (zeroing the stored value), leaving legal sides untouched. Both sides are
 * evaluated against `r` as passed in — one side degrading never changes
 * whether the other was legal. Returns a bitmask of degraded sides (bit0 =
 * lo, bit1 = hi) so a caller can print a NOTE. Used by limit-moving paths
 * (applyAlarmShape, template reseed) that must not simply refuse the edit —
 * mirrors the existing HYST-drop precedent.
 */
uint8_t rulesNormalizeWarn(Rule& r);

/**
 * Annunciator severity for a rule, including the WARNING approach tier
 * (§11.2): an ALARM rule annunciates ALARM once ACTIVE, but also WARNING
 * while armed (READY) and its source has reached a per-side warning edge
 * (see rulesRuleWarnEdge — AUTO/NONE/SET per side; RULE_WARN_NONE on the hit
 * side suppresses this pre-announcement entirely, going straight to ALARM
 * on trip). WARNING-severity rules have no sub-tier (WARNING when active,
 * else NONE). This drives annunciation (LED/buzzer/AlarmBroadcaster) only —
 * NOT Action triggering, which keys on RULE_ACTIVE.
 */
RuleSeverity rulesRuleAnnunciatorSeverity(const Rule& rule, const RuleContext& ctx, float value);

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
    bool    anyFaulted;   // any member's CONFIGURED source reads NaN (§11.3; #345)
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
bool rulesActionDesired(const Action& action, const ActionDrive& drive,
                          bool manualState, ActionOverride override);

#endif // RULES_ENGINE_H
