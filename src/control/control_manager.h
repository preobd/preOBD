/*
 * control_manager.h - Runtime owner of the control engine's Rule contexts.
 *
 * Bridges the persisted rule table (rules[]) to live input values:
 * holds the parallel RuleContext[] runtime state, evaluates every configured
 * rule each cycle against its source input, and rolls up system severity.
 *
 * Also exposes the rule-table helpers the RULE CLI uses (lookup / allocate by
 * stable id). Glue layer — not host-unit-tested (the pure engine is); see
 * control_engine.{h,cpp} for the tested logic.
 */

#ifndef CONTROL_MANAGER_H
#define CONTROL_MANAGER_H

#include "control_types.h"
#include "control_engine.h"

#if ENABLE_CONTROL_ENGINE

// Runtime contexts, indexed 1:1 with rules[].
extern RuleContext g_ruleContexts[MAX_RULES];

// Stamp every Rule/Action slot empty (id = CONTROL_ID_NONE) and disable every
// rule context. The tables are BSS-zeroed standalone globals where id==0 is a
// VALID id, so this MUST run before they are trusted on any path that does NOT
// load a valid store: boot before load, every load-failure/early-return, and
// factory reset. Mirrors the inputs[] pin=0xFF defense.
void controlResetTables();

// Arm every configured rule from the table (the boot-load path, §11.5). Call once
// after the config store + inputs are loaded.
void controlManagerInit(uint32_t now);

// Evaluate every configured rule against its live source input value. Call on
// the alarm-check cadence from the main loop.
void controlManagerUpdate(uint32_t now);

// Max configured severity over all rules currently ACTIVE (annunciator rollup).
RuleSeverity controlSystemSeverity();

// --- rule table helpers (operate on rules[]) ---
int     controlRuleSlot(uint8_t id);    // slot index for a rule id, or -1
int     controlRuleFreeSlot();          // first empty slot, or -1 if full
uint8_t controlRuleAllocId();           // smallest unused id, or CONTROL_ID_NONE if exhausted
uint8_t controlRuleCount();             // number of configured rules

// Clear any Action.ruleRef pointing at `ruleId` (referential integrity on rule
// delete — those actions revert to manual-only). Called from RULE DEL.
void    controlClearActionsForRule(uint8_t ruleId);

// --- application default-Rule template (§11.5) ---

// True if any Rule already references this input pin.
bool    controlRuleExistsForInput(uint8_t pin);

// Create-from-template: seed one canonical default Rule for `pin` from its
// application's template, IF no Rule already references `pin` (implicit
// re-apply preserves). Returns the new rule id, or CONTROL_ID_NONE if nothing
// was created (rule already exists / app has no default alarm / table full).
uint8_t controlSeedRuleForInput(uint8_t pin, uint8_t appIndex, uint32_t now);

// RULE RESET: rewrite rule <id>'s config from its source input's application
// template and re-arm (explicit reset clobbers, §11.5). Returns false if the
// rule is unknown, its input is gone, or the application has no default rule.
bool    controlRuleReset(uint8_t id, uint32_t now);

// --- per-input "alarm Rule" facade (SET <pin> ALARM ...) --------------------
// The webapp / CLI expose a single per-input min/max alarm band. That band maps
// to exactly one canonical Rule on the input: condition OUTSIDE_BAND, severity
// ALARM (what SET APPLICATION seeds, §11.5). These helpers let SET ALARM and the
// JSON dump address that one Rule by input pin without juggling its id (#295).

// Slot of the input's canonical alarm Rule (OUTSIDE_BAND / ALARM sourced from
// `pin`), or -1 if none. Read-back and the upsert both key off this.
int     controlAlarmRuleSlotForInput(uint8_t pin);

// Create-or-replace the input's alarm Rule as an OUTSIDE_BAND/ALARM band
// [lo,hi], re-arming it. An existing Rule is edited in place — id, source, and
// any Action links are preserved (no DEL+ADD churn). Caller must pass lo < hi.
// Returns the Rule id, or CONTROL_ID_NONE only when a *new* Rule was needed and
// the table is full.
uint8_t controlSetAlarmBandForInput(uint8_t pin, float lo, float hi, uint32_t now);

// Remove the input's alarm Rule (frees the pool slot; referring Actions revert
// to manual-only). Returns true if one was removed.
bool    controlRemoveAlarmRuleForInput(uint8_t pin);

// Application changed under existing rules: rewrite every Rule sourced from
// `pin` to the input's (now-updated) application template and re-arm, keeping
// each rule's id. Returns the number of rules rewritten (0 if none reference
// the pin or the new application has no usable default band — the old, now
// stale, thresholds are then left in place for the caller to warn about).
uint8_t controlReseedRulesForInput(uint8_t pin, uint32_t now);

// === Actions ===
// Configure all action target pins as outputs and set their initial state
// (boot-load). Call once after SystemConfig is loaded. Does NOT touch the pin
// registry — call controlSyncActionPins() separately (see below).
void controlActionsInit();

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
uint8_t controlSyncActionPins();

// Evaluate every configured action and drive its target. Call each cycle after
// controlManagerUpdate(). Actuation is suppressed in CONFIG mode (bench safety,
// mirrors the relay/buzzer gate in #268).
void controlActionsUpdate(bool configMode);

// Current driven (logical, pre-invert) state of the action in `slot`, for LIST.
bool controlActionState(int slot);

// --- action table helpers (operate on actions[]) ---
int     controlActionSlot(uint8_t id);
int     controlActionFreeSlot();
uint8_t controlActionAllocId();
uint8_t controlActionCount();
void    controlActionConfigurePin(int slot);   // pinMode target OUTPUT + de-assert
bool    controlActionSetManual(uint8_t id, bool on);  // ACTION <id> ON|OFF

#endif // ENABLE_CONTROL_ENGINE
#endif // CONTROL_MANAGER_H
