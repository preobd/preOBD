/*
 * rules_manager.h - Runtime owner of the control engine's Rule contexts.
 *
 * Bridges the persisted rule table (rules[]) to live input values:
 * holds the parallel RuleContext[] runtime state, evaluates every configured
 * rule each cycle against its source input, and rolls up system severity.
 *
 * Also exposes the rule-table helpers the RULE CLI uses (lookup / allocate by
 * stable id). Glue layer — not host-unit-tested (the pure engine is); see
 * rules_engine.{h,cpp} for the tested logic.
 */

#ifndef RULES_MANAGER_H
#define RULES_MANAGER_H

#include "rules_types.h"
#include "rules_engine.h"

#if INCLUDE_RULES_ENGINE

// Runtime contexts, indexed 1:1 with rules[].
extern RuleContext g_ruleContexts[MAX_RULES];

// Stamp every Rule/Action slot empty (id = RULES_ID_NONE) and disable every
// rule context. The tables are BSS-zeroed standalone globals where id==0 is a
// VALID id, so this MUST run before they are trusted on any path that does NOT
// load a valid store: boot before load, every load-failure/early-return, and
// factory reset. Mirrors the inputs[] pin=0xFF defense.
void rulesResetTables();

// Arm every configured rule from the table (the boot-load path, §11.5). Call once
// after the config store + inputs are loaded.
void rulesManagerInit(uint32_t now);

// Evaluate every configured rule against its live source input value. Call on
// the alarm-check cadence from the main loop.
void rulesManagerUpdate(uint32_t now);

// Max configured severity over all rules currently ACTIVE (annunciator rollup).
RuleSeverity rulesSystemSeverity();
// Same rollup, skipping acknowledged slots — what the BUZZER follows (#433).
RuleSeverity rulesSystemSeverityUnacked();

// --- annunciator acknowledgement (#433, ARCHITECTURE §11.4) ------------------
// The mode button and `ALARM SILENCE` acknowledge every rule that is sounding
// the buzzer NOW (ACTIVE at severity ALARM). An acknowledged rule stops
// driving the buzzer (and, under SCOPE ALL, the LED blink). Timed policy
// (`durationMs` > 0): the mute holds for exactly that long, through a
// clear-and-retrip. Until-clear policy (0): it holds until the rule stops
// sounding. Either way a rule that trips LATER is not covered and sounds, and
// a deleted/replaced rule's ack is dropped. Per-slot state keyed by rule id,
// RAM only. `rulesAckExpire` must be called on the annunciator cadence with
// the configured policy.
void     rulesAckReset();
uint8_t  rulesAckActiveAlarms(uint32_t now);   // returns how many rules were NEWLY acked
void     rulesAckExpire(uint32_t now, uint32_t durationMs);
bool     rulesRuleAcked(int slot);
uint8_t  rulesAckCount();
uint32_t rulesAckElapsedMs(int slot, uint32_t now);   // 0 when not acked
uint8_t  rulesAckGeneration();   // bumps on every acked-set change; wraps (#455)

// --- rule table helpers (operate on rules[]) ---
int     rulesRuleSlot(uint8_t id);    // slot index for a rule id, or -1
int     rulesRuleFreeSlot();          // first empty slot, or -1 if full
uint8_t rulesRuleAllocId();           // smallest unused id, or RULES_ID_NONE if exhausted
uint8_t rulesRuleCount();             // number of configured rules

// Referential integrity on rule delete: drop `ruleId` from every Action's Rule
// membership and release whatever was driving those Actions. An Action that had
// other member Rules keeps them and keeps following them; one left with none
// reverts to manual-only. Returns how many were actively driven (override or
// manual latch) at the time, so the caller can say so rather than silently
// de-energizing a relay. Called from RULE DEL / SET <pin> ALARM OFF.
uint8_t rulesClearActionsForRule(uint8_t ruleId);

// Delete every Rule sourced from input `pin`, through the same referential-
// integrity path as RULE DEL (each rule's id is dropped from every Action's
// membership). Called by CLEAR <pin>, which would otherwise leave the Rules
// orphaned on a pin with no input (#345). Returns the number of Rules removed;
// optional `releasedOut` accumulates how many Actions were actively driven at
// the time, so the caller can report the relays it just de-energized (saturating
// at 255 rather than wrapping — an under-report would understate the change).
uint8_t rulesRemoveRulesForInput(uint8_t pin, uint8_t* releasedOut = nullptr);

// RULE <id> ENABLE|DISABLE (#346): park a Rule without deleting it, or re-arm a
// parked one. A disabled Rule sits in RULE_DISABLED, never asserts, and (since
// #343) cannot contribute a failsafe. Returns true if the flag actually changed;
// false means it was already in the requested state and NOTHING was touched —
// re-enabling an already-armed Rule must not re-init it and drag a live rule
// back through INIT->WARMUP (#254).
bool    rulesRuleSetEnabled(uint8_t id, bool enable, uint32_t now);

// --- application default-Rule template (§11.5) ---

// True if any Rule already references this input pin.
bool    rulesRuleExistsForInput(uint8_t pin);

// Create-from-template: seed one canonical default Rule for `pin` from its
// application's template, IF no Rule already references `pin` (implicit
// re-apply preserves). Returns the new rule id, or RULES_ID_NONE if nothing
// was created (rule already exists / app has no default alarm / table full).
uint8_t rulesSeedRuleForInput(uint8_t pin, uint8_t appIndex, uint32_t now);

// RULE RESET: rewrite rule <id>'s config from its source input's application
// template and re-arm (explicit reset clobbers, §11.5). Returns false if the
// rule is unknown, its input is gone, or the application has no default rule.
bool    rulesRuleReset(uint8_t id, uint32_t now);

// --- per-input "alarm Rule" facade (SET <pin> ALARM ...) --------------------
// The webapp / CLI expose a single per-input alarm on an input. That alarm maps
// to exactly one canonical Rule on the input at severity ALARM, whose condition
// is one of: OUTSIDE_BAND (a Min+Max band, what SET APPLICATION seeds), BELOW (a
// Min-only floor), or ABOVE (a Max-only ceiling) — §11.5, #312. These helpers let
// SET ALARM and the JSON dump address that one Rule by input pin without juggling
// its id (#295).

// Slot of the input's canonical alarm Rule (severity ALARM, condition one of
// {OUTSIDE_BAND, BELOW, ABOVE}, sourced from `pin`), or -1 if none. Read-back and
// the upsert both key off this.
int     rulesAlarmRuleSlotForInput(uint8_t pin);

// Create-or-replace the input's alarm Rule at severity ALARM, re-arming it. An
// existing alarm Rule is edited in place — id, source, and any Action links are
// preserved (no DEL+ADD churn), even when `condition` changes (band↔one-sided).
// `condition` selects the shape:
//   RULE_OUTSIDE_BAND — band, uses [lo, hi] (caller must pass lo < hi)
//   RULE_BELOW        — Min-only floor, uses `lo` as the limit (hi ignored)
//   RULE_ABOVE        — Max-only ceiling, uses `lo` as the limit (hi ignored)
// Returns the Rule id, or RULES_ID_NONE only when a *new* Rule was needed and
// the table is full. `warnResetOut`, if given, receives a RULE_WARN_*_MIN/MAX
// mask (below) naming every warn side the edit changed behind the operator's
// back — degraded to AUTO because the new limit stranded it (#425, amendment
// A7), or cleared because the new shape has no such side (#427) — so a caller
// can print a NOTE. Sides are named in facade terms (min = floor, max =
// ceiling), NOT by storage slot: the lo slot IS the ceiling on an ABOVE rule.
// Always 0 for a brand-new Rule (it starts AUTO on both sides).
#define RULE_WARN_RESET_MIN   0x01  // floor   warn edge degraded to AUTO (limit moved past it)
#define RULE_WARN_RESET_MAX   0x02  // ceiling warn edge degraded to AUTO (limit moved past it)
#define RULE_WARN_DROPPED_MIN 0x04  // floor   warn setting cleared (MAX-only alarm has no lower side)
#define RULE_WARN_DROPPED_MAX 0x08  // ceiling warn setting cleared (MIN-only alarm has no upper side)
uint8_t rulesSetAlarmRuleForInput(uint8_t pin, uint8_t condition, float lo, float hi, uint32_t now,
                                   uint8_t* warnResetOut = nullptr);

// RULE <id> LIMIT (#457): move any Rule's condition/limits in place, whatever
// its severity and whether or not it's the input's canonical alarm Rule. Same
// shape logic as the facade above (applyAlarmShape: band<->one-sided, warn
// degrade reported in `warnResetOut`), but it never touches severity, the
// enabled flag or the RuleContext, and keeps the id and every Action link —
// what DEL+ADD used to lose. `hystClearedOut` is set when the existing
// hysteresis was too wide for the new limits and was zeroed. Caller validates
// lo < hi for a band. Returns false for an unknown id.
bool rulesSetRuleLimit(uint8_t id, uint8_t condition, float lo, float hi,
                       uint8_t* warnResetOut = nullptr, bool* hystClearedOut = nullptr);

// RULE <id> SEVERITY (#470): change a Rule's tier in place, keeping its id,
// limits, enabled flag, Action links and live RuleContext (DEL+ADD lost the
// links). The warn tier only exists on ALARM rules, so leaving ALARM resets
// both warn sides to AUTO (`warnClearedOut` = a side was not AUTO). Any
// acknowledgement on the rule is dropped: it silenced the old tier, and a
// rule moved back to ALARM must be heard. Setting the current severity is a
// no-op. Returns false for an unknown id.
bool rulesSetRuleSeverity(uint8_t id, uint8_t severity, bool* warnClearedOut = nullptr);

// Edit the warn tier of the input's canonical alarm Rule (#425, slice B) — the
// facade behind `SET <pin> ALARM WARN ...`. Unlike the shape edit above, this
// REJECTS an illegal side rather than degrading it (validates a COPY via
// rulesWarnSideValid, commits only if both sides check out): an explicit WARN
// edit is the operator naming a number, not a limit move dragging one along.
// Returns false when the input has no alarm Rule (`badSideOut` = 0xFF) or a
// requested SET side is illegal for the rule's condition (`badSideOut` = 0 for
// lo, 1 for hi) — including a hi value on a one-sided (ABOVE/BELOW) rule, where
// `hiMode` must be RULE_WARN_AUTO (no hi side to set/clear). No re-arm.
bool rulesSetAlarmWarnForInput(uint8_t pin, RuleWarnMode loMode, float lo,
                                RuleWarnMode hiMode, float hi, uint8_t* badSideOut = nullptr);

// Remove the input's alarm Rule (frees the pool slot; referring Actions revert
// to manual-only). Returns true if one was removed.
// Optional `releasedOut` receives how many Actions following the removed Rule were
// actively driven, so the caller can report a relay it just de-energized (#329).
bool    rulesRemoveAlarmRuleForInput(uint8_t pin, uint8_t* releasedOut = nullptr);

// Application changed under existing rules: rewrite every Rule sourced from
// `pin` to the input's (now-updated) application template and re-arm, keeping
// each rule's id. Returns the number of rules rewritten (0 if none reference
// the pin or the new application has no usable default band — the old, now
// stale, thresholds are then left in place for the caller to warn about).
uint8_t rulesReseedRulesForInput(uint8_t pin, uint32_t now);

// === Actions ===
// Configure all action target pins as outputs and set their initial state
// (boot-load). Call once after SystemConfig is loaded. Does NOT touch the pin
// registry — call rulesSyncActionPins() separately (see below).
void rulesActionsInit();

// Re-sync the pin registry with the configured Action targets: drop any entries
// this engine previously added, then register each current target as
// PIN_OUTPUT (conflict-checked). Idempotent — safe at boot and after a runtime
// config reload (the registry is not otherwise cleared mid-run). Returns the
// number of conflicts detected (each also logged).
//
// ORDERING: action pins must register LAST, so they are validated against — and
// never override — the hardware-fixed bus / serial / system / RGB pins. Call it
// once near the end of boot (after those subsystems init) and again whenever the
// action table changes at runtime (ADD / DEL / config import), when the registry
// is already fully populated.
uint8_t rulesSyncActionPins();

// Evaluate every configured action and drive its target. Call each cycle after
// rulesManagerUpdate(). Actuation is suppressed in CONFIG mode (bench safety,
// mirrors the relay/buzzer gate in #268).
void rulesActionsUpdate(bool configMode);

// Current driven (logical, pre-invert) state of the action in `slot`, for LIST.
bool rulesActionState(int slot);

// True when the action in `slot` is rule-driven and ANY member rule's source
// input is untrustworthy — i.e. a CONFIGURED input reading NaN. Shared by the
// evaluation loop and by the CLI / BLE reporting paths so an override held
// against a dead sensor is never silent (#329). Manual-only, dangling-ref,
// disabled-rule and orphan-rule (no input on the source pin, #345) members all
// report false: none of them can assert, so none may force a failsafe.
bool rulesActionSourceFaulted(int slot);

// True when the action in `slot` has at least one member rule that is currently
// ACTIVE with a trustworthy source — i.e. the automation is demanding this
// output right now. Read this rather than inferring it from the alarm-event
// feed: that feed carries only WARNING/ALARM, so a RULE_SEV_NONE control-only
// rule (the cooling-fan / shift-light case the tier exists for) is invisible
// there, and reconstructing it client-side means duplicating hysteresis,
// debounce and warmup and risking an opinion that disagrees with what actually
// drove the pin (#348). Shares resolveActionDrive() with the evaluation loop so
// the two cannot diverge — this reports what DID drive the output, never a
// second opinion about it. False for manual-only and dangling-ref members and
// for a member whose source is faulted (a NaN reading never asserts). Disabled
// and orphan-rule (#345) members report false too, but via the engine — it
// parks their contexts at RULE_DISABLED / RULE_READY — not by a check here;
// deliberately so, since a context that somehow read ACTIVE would be driving
// the pin and the frame must agree with the pin.
bool rulesActionRuleActive(int slot);

// Both of the above in one membership walk, for callers that want both (the BLE
// frame does). Zero-valued for an out-of-range or unconfigured slot.
ActionDrive rulesActionDrive(int slot);

// --- Rule<->Action membership (M:N, §11.6 / #343) ---
// An Action is OR-driven by its member Rules: active while ANY member is active.
// The first member is stored inline in Action.ruleRef and the rest in
// actionLinks[], but NOTHING outside rules_manager.cpp should care — read
// membership through these accessors, never off Action.ruleRef.
uint8_t rulesActionRuleCount(int slot);              // 0 = manual-only
uint8_t rulesActionRuleAt(int slot, uint8_t index);  // member `index`, or RULES_ID_NONE
bool    rulesActionHasRule(int slot, uint8_t ruleId);
uint8_t rulesActionLinkCount();                      // occupied actionLinks[] rows (for SAVE)

// Mutators. Each RELEASES the action's override + manual latch (the driver
// changed — #294/#329 invariant), so the action starts de-asserted. Optional
// `wasDriving` receives whether that release actually dropped a live drive, so a
// caller can report a relay it just de-energized rather than doing it silently
// (and can avoid claiming a release that never happened). A rejected call
// (false) mutates nothing and never releases.
bool    rulesActionAddRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving = nullptr);     // false = unknown ids / link table full
bool    rulesActionRemoveRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving = nullptr);  // false = not a member
bool    rulesActionSetRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving = nullptr);     // membership := exactly {ruleId}
bool    rulesActionClearRules(uint8_t actionId);                  // → manual-only; returns "was driving"

// Free an action's link rows WITHOUT touching its drive state — for ACTION DEL,
// where the record itself is going away (the pin is de-asserted separately).
void    rulesActionForgetLinks(uint8_t actionId);

// Repair Rule membership after a bulk table load (EEPROM store, JSON import),
// where an Action may reference a Rule that didn't load (truncated store,
// smaller-cap build, hand-edited config, rules-only import). Drops dangling /
// duplicate link rows, and promotes a surviving member when the inline one is
// the casualty — so the engine never evaluates a dangling ref and a multi-member
// action never silently degrades to manual. Returns the number of repairs (each
// also logged); call before re-arming. Releases the drive on any action it
// repairs (#294/#329).
uint8_t rulesSweepActionRules();

// The link-table half of the sweep, exposed for callers that only touch links.
// Drops rows that are dangling (unknown action/rule) or duplicate an action's
// inline member. Returns how many were dropped.
uint8_t rulesPruneActionLinks();

// --- action table helpers (operate on actions[]) ---
int     rulesActionSlot(uint8_t id);
int     rulesActionFreeSlot();
uint8_t rulesActionAllocId();
uint8_t rulesActionCount();
void    rulesActionConfigurePin(int slot);   // pinMode target OUTPUT + de-assert
bool    rulesActionSetManual(uint8_t id, bool on);  // manual latch (unlinked actions)
// ACTION <id> ON|OFF from any surface: routes to the manual latch when the action
// is unlinked, or to a FORCE_ON/FORCE_OFF operator override when it is rule-driven
// (#329). Use this rather than rulesActionSetManual() from command paths.
bool    rulesActionCommand(uint8_t id, bool on);
// Operator override (RAM-only, reverts to AUTO on reboot). ACTION <id> AUTO
// releases it; it is also cleared on any rule-link change and on pin reconfigure.
ActionOverride rulesActionOverride(int slot);
// Clear BOTH the override and the manual latch — the "this action's driver just
// changed, so it starts de-asserted" invariant. Returns true if either was
// actually driving. Call on every link change; clearing only one leaves the #294
// stale-latch snap-on reachable via the other.
bool    rulesActionReleaseDrive(uint8_t id);
bool    rulesActionSetOverride(uint8_t id, ActionOverride ovr);
bool    rulesActionSetName(uint8_t id, const char* name);  // ACTION <id> NAME <text>

#endif // INCLUDE_RULES_ENGINE
#endif // RULES_MANAGER_H
