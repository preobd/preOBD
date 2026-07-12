/*
 * control_types.h - Control Engine type definitions (Rules / Actions)
 *
 * The control engine unifies three previously-separate subsystems — per-input
 * alarm thresholds, the relay threshold engine, and the hardwired buzzer/LED
 * output — into one condition→effect pipeline:
 *
 *     Input --(watched by)--> Rule --(triggers)--> Action(s)
 *                              │
 *                              └─ severity --> built-in Annunciator
 *
 * This header defines the persisted record types and the runtime state. It is
 * intentionally logic-free and dependency-light so it compiles standalone; the
 * evaluation engine, EEPROM wiring, and CLI land in later commits.
 *
 * Authoritative design: ARCHITECTURE.md §11. Concrete v1 plan + decisions:
 * CONTROL_ENGINE_PLAN.md. Key locked decisions reflected here:
 *   - v1 is N:1 (an Action is triggered by ≤1 Rule; a Rule fans out to many).
 *   - Records are managed by a stable `id`, NOT array slot and NOT a name —
 *     there is deliberately no name field (CONTROL_ENGINE_PLAN §1.1 / §11.8).
 *   - Rule.sourceInput is the input PIN (the existing stable input identity),
 *     not an inputs[] slot (§8 Q9 axis A).
 *   - RuleState / RuleContext are the lift-and-rename target of the existing
 *     AlarmState / AlarmContext (input.h); they model the same warmup/debounce
 *     state machine (§11.2).
 *
 * NOTE: not yet referenced by SystemConfig or Input. Wiring the rules[]/
 * actions[] tables into SystemConfig (replacing RelayConfig relays[], with the
 * SYSTEM_CONFIG_VERSION bump) is the next implementation step.
 */

#ifndef CONTROL_TYPES_H
#define CONTROL_TYPES_H

#include <Arduino.h>

// ============================================================================
// Feature gate. The control engine subsumes alarms + relays, so it is active
// whenever either was (a profile may still force it explicitly). Profiles own
// feature flags (CLAUDE.md), but this derivation keeps existing profiles
// working without edits — define ENABLE_CONTROL_ENGINE in a profile to override.
// ============================================================================
#ifndef ENABLE_CONTROL_ENGINE
#  if (defined(ENABLE_ALARMS) && ENABLE_ALARMS) || (defined(ENABLE_RELAY_OUTPUT) && ENABLE_RELAY_OUTPUT)
#    define ENABLE_CONTROL_ENGINE 1
#  else
#    define ENABLE_CONTROL_ENGINE 0
#  endif
#endif

// ============================================================================
// Platform caps (per-profile, SRAM/EEPROM-bound — see ARCHITECTURE §11.9)
// Profiles may override. Defaults: 8/8 on Teensy/STM32; AVR profiles set 2/2.
// ============================================================================
#ifndef MAX_RULES
#define MAX_RULES   8
#endif
#ifndef MAX_ACTIONS
#define MAX_ACTIONS 8
#endif

// Reserved id sentinel: an unused record, and (as Action.ruleRef) "manual-only".
#define CONTROL_ID_NONE 0xFF

// Defaults for a manually-created Rule (RULE ADD) when the operator omits them.
// Application default-Rule templates (§11.5) will carry their own; these are the
// hand-entry fallback.
#define RULE_DEFAULT_WARMUP_MS   3000
#define RULE_DEFAULT_DEBOUNCE_MS 500

// ============================================================================
// Enumerations (persisted as uint8 — values are stable, append only)
// ============================================================================

// Comparison shape of a Rule's condition (§11.2).
// ABOVE/BELOW map to today's relay AUTO_HIGH/AUTO_LOW; OUTSIDE_BAND is today's
// per-input min/max alarm collapsed into one Rule.
enum RuleCondition : uint8_t {
    RULE_ABOVE        = 0,  // asserts when value rises above limitLo (clears at limitLo − hyst)
    RULE_BELOW        = 1,  // asserts when value drops below limitLo (clears at limitLo + hyst)
    RULE_OUTSIDE_BAND = 2,  // asserts when value < limitLo OR > limitHi
    RULE_INSIDE_BAND  = 3   // DEFERRED — reserved slot, not evaluated in v1 (§11.2)
};

// Configured severity of a Rule — drives the built-in Annunciator (§11.4).
// Distinct from runtime AlarmSeverity ("currently firing?"): this is config
// ("how loud when it fires"). NONE = pure control Rule (drives Actions only).
enum RuleSeverity : uint8_t {
    RULE_SEV_NONE    = 0,  // no annunciation — control-only (fan, shift light)
    RULE_SEV_WARNING = 1,  // LCD banner + log + AlarmBroadcaster, no buzzer
    RULE_SEV_ALARM   = 2   // + buzzer (with silence button)
};

// Runtime state machine — mirror of AlarmState (input.h), to be unified (§11.2).
enum RuleState : uint8_t {
    RULE_DISABLED = 0,  // Rule disabled
    RULE_INIT     = 1,  // initial state after create/boot
    RULE_WARMUP   = 2,  // sensor settling, evaluation blocked
    RULE_READY    = 3,  // armed, evaluating
    RULE_ACTIVE   = 4   // condition currently asserted
};

// Named actuator kind (§11.3).
enum ActionType : uint8_t {
    ACT_RELAY   = 0,
    ACT_BEEPER  = 1,
    ACT_LED     = 2,
    ACT_DISPLAY = 3
};

// Bit flags
#define RULE_FLAG_ENABLED     0x01  // Rule.flags
#define ACT_BEHAVIOR_INVERT   0x01  // Action.behavior: active-low / inverted output

// Action.failsafe — state a RELAY Action assumes when its source reading is
// untrustworthy (OPEN/SHORTED/NO-READING/rail-pegged; §11.3). Per-Action so a
// cooling fan can fail ON while a fuel pump fails OFF.
enum ActionFailsafe : uint8_t {
    ACT_FAILSAFE_OFF = 0,
    ACT_FAILSAFE_ON  = 1
};

// ============================================================================
// Persisted records (EEPROM-resident; live inside SystemConfig — Option A)
// No name fields: managed by stable `id`; LIST renders ground truth from the
// source input's name + condition + severity (§11.8).
// ============================================================================

// ~21–24 B depending on platform alignment.
struct Rule {
    float    limitLo;       // ABOVE/BELOW: the limit; OUTSIDE_BAND: low edge
    float    limitHi;       // OUTSIDE_BAND: high edge; unused for ABOVE/BELOW
    float    hysteresis;    // clear-band width, applied inward from each limit
    uint16_t debounceMs;    // condition must hold past the limit this long (= legacy persistTime_ms)
    uint16_t warmupMs;      // suppress evaluation until settled / since boot (= legacy warmupTime_ms)
    uint8_t  id;            // stable per-record id; CONTROL_ID_NONE = empty slot. NOT an array slot.
    uint8_t  sourceInput;   // input PIN (stable identity), NOT an inputs[] slot (§8 Q9)
    uint8_t  condition;     // RuleCondition
    uint8_t  severity;      // RuleSeverity
    uint8_t  flags;         // RULE_FLAG_*
};

// ~6 B.
struct Action {
    uint8_t id;         // stable per-record id; CONTROL_ID_NONE = empty slot
    uint8_t type;       // ActionType
    uint8_t target;     // pin / relay channel / LED channel
    uint8_t ruleRef;    // Rule.id that triggers it; CONTROL_ID_NONE = manual-only (N:1 in v1)
    uint8_t behavior;   // ACT_BEHAVIOR_*
    uint8_t failsafe;   // ActionFailsafe (RELAY only; ignored by other types in v1)
};

// ============================================================================
// Runtime context (NOT persisted) — lift-and-rename target of AlarmContext.
// ============================================================================
struct RuleContext {
    RuleState state;            // current runtime state
    uint32_t  stateEntryTime;   // millis() when current state was entered
    uint32_t  faultStartTime;   // millis() when threshold violation started (0 = none) — debounce anchor
    uint16_t  warmupTime_ms;    // effective warmup (from the Rule)
    uint16_t  persistTime_ms;   // effective debounce (from the Rule)
};

#endif // CONTROL_TYPES_H
