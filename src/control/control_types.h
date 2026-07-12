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
 * Authoritative design: ARCHITECTURE.md §11. Key locked decisions reflected here:
 *   - v1 is N:1 (an Action is triggered by ≤1 Rule; a Rule fans out to many).
 *   - Records are managed by a stable `id`, NOT array slot. Rules carry no name;
 *     Actions carry an optional operator label (`name`) — see §11.8.
 *   - Rule.sourceInput is the input PIN (the existing stable input identity),
 *     not an inputs[] slot (§8 Q9 axis A).
 *   - RuleState / RuleContext are the lift-and-rename target of the existing
 *     AlarmState / AlarmContext (input.h); they model the same warmup/debounce
 *     state machine (§11.2).
 *
 * NOTE: the rules[]/actions[] tables are standalone RAM globals (control_manager.cpp),
 * persisted by the shared config-store arena (input_manager.cpp) — NOT embedded
 * in SystemConfig. See the extern declarations below.
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
// Platform caps (RAM table sizes) — decoupled from MAX_INPUTS so the engine
// supports N:N (multiple Rules per input, multiple Actions per Rule), without
// tying the cap to an input-count guess.
//
// Default is a uniform 64 across all 32-bit boards: generous (8x the legacy cap
// and far beyond any realistic config — ~15-20 inputs, a handful of rules each),
// while deliberately leaving the ~13 KB of RAM1 that a 254-slot table would eat
// FREE for future features. 64 is a fixed RAM ceiling, NOT the engine's hard
// limit: record ids are uint8_t with 0xFF reserved, so 254 is the absolute id
// space — a board with RAM to spare can override CONTROL_RAM_{RULES,ACTIONS}_MAX
// upward toward 254. Overflow is never silent: the shared EEPROM arena truncates
// LOUDLY (input_manager.cpp) and auto-seed warns at the CLI (cmd_set.cpp).
//
// AVR (Mega, 8 KB SRAM) is a hard physical exception — it overrides DOWN to 2
// (boards/mega2560/board.h); 64 would blow its budget.
//
// This carries NO dependency on SYSTEM_CONFIG_ADDRESS / the EEPROM layout, so the
// cap stays a pure compile-time constant here (no header cycle).
// ============================================================================
#ifndef CONTROL_RAM_RULES_MAX
#define CONTROL_RAM_RULES_MAX   64    // standard RAM cap; absolute id-space max is 254
#endif
#ifndef CONTROL_RAM_ACTIONS_MAX
#define CONTROL_RAM_ACTIONS_MAX 64
#endif
#define MAX_RULES   CONTROL_RAM_RULES_MAX
#define MAX_ACTIONS CONTROL_RAM_ACTIONS_MAX
static_assert(MAX_RULES   >= 1 && MAX_RULES   <= 254, "MAX_RULES out of id space [1,254]");
static_assert(MAX_ACTIONS >= 1 && MAX_ACTIONS <= 254, "MAX_ACTIONS out of id space [1,254]");

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
// Persisted records (EEPROM-resident; stored in the shared config-store arena,
// input_manager.cpp). Records are managed by a stable `id`, NOT array slot.
// Rules carry no name — LIST renders ground truth from the source input's name +
// condition + severity. Actions DO carry an optional human-readable `name` (an
// actuator like "Cooling Fan" wants a label that travels to every surface: CLI,
// JSON dump, mobile app), affordable now that the shared arena packs records by
// used count rather than the old fixed-array-in-SystemConfig layout (§11.8).
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

// ~22 B.
struct Action {
    uint8_t id;         // stable per-record id; CONTROL_ID_NONE = empty slot
    uint8_t type;       // ActionType
    uint8_t target;     // pin / relay channel / LED channel
    uint8_t ruleRef;    // Rule.id that triggers it; CONTROL_ID_NONE = manual-only (N:1 in v1)
    uint8_t behavior;   // ACT_BEHAVIOR_*
    uint8_t failsafe;   // ActionFailsafe (RELAY only; ignored by other types in v1)
    char    name[16];   // optional operator label (e.g. "Cooling Fan"); "" = unnamed
};

// ============================================================================
// The persisted Rule/Action tables are standalone RAM globals (defined in
// control_manager.cpp), mirroring Input inputs[] — NOT members of SystemConfig.
// Persistence is handled by the shared EEPROM config-store arena in
// input_manager.cpp (saveInputConfig/loadInputConfig), which packs inputs, rules
// and actions into one front-growing region. Declared here so every translation
// unit that sees the record types can reach the tables without pulling in
// control_manager.h (e.g. pin_registry.cpp, which only has system_config.h).
// ============================================================================
#if ENABLE_CONTROL_ENGINE
extern Rule   rules[MAX_RULES];
extern Action actions[MAX_ACTIONS];
#endif

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
