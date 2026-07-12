/*
 * control_manager.cpp - Runtime owner of the control engine (see header).
 */

#include "control_manager.h"

#if ENABLE_CONTROL_ENGINE

#include <math.h>
#include "../lib/system_config.h"
#include "../inputs/input_manager.h"
#include "../lib/application_presets.h"
#include "../lib/pin_registry.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"

// Shared, stable description pointer for every Action-owned pin registry entry.
// registerPin() stores the pointer (no copy), and controlSyncActionPins()
// identifies its own prior entries by pointer equality against this — so it
// must be a single static string, not a per-call temporary.
static const char* const ACTION_PIN_DESC = "Action output";

RuleContext g_ruleContexts[MAX_RULES];

void controlManagerInit(uint32_t now) {
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& rule = systemConfig.rules[i];
        if (rule.id == CONTROL_ID_NONE) {
            g_ruleContexts[i].state = RULE_DISABLED;
        } else {
            controlRuleInit(g_ruleContexts[i], rule, now);  // boot-load: arm (#251)
        }
    }
}

void controlManagerUpdate(uint32_t now) {
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& rule = systemConfig.rules[i];
        if (rule.id == CONTROL_ID_NONE) continue;

        // Resolve the source input by pin (the stable input identity). A missing
        // source yields NaN, which the engine treats as untrustworthy — the rule
        // can't assert (failsafe intent).
        Input* in = getInputByPin(rule.sourceInput);
        float value = in ? in->value : NAN;

        controlRuleUpdate(rule, g_ruleContexts[i], value, now);
    }
}

RuleSeverity controlSystemSeverity() {
    uint8_t maxSev = RULE_SEV_NONE;
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& rule = systemConfig.rules[i];
        if (rule.id == CONTROL_ID_NONE) continue;
        // Annunciator rollup includes the derived WARNING tier (§11.2), so
        // resolve the source value for each rule.
        Input* in = getInputByPin(rule.sourceInput);
        float value = in ? in->value : NAN;
        uint8_t s = controlRuleAnnunciatorSeverity(rule, g_ruleContexts[i], value);
        if (s > maxSev) maxSev = s;
    }
    return (RuleSeverity)maxSev;
}

// --- generic record-table helpers ---------------------------------------
// Rule and Action are both id-keyed records (CONTROL_ID_NONE = empty slot; ids
// are stable per record §8 Q9, so a freed id is safe to reuse). These templated
// scans single-source the rule and action table lookups below.

template <typename T>
static int recSlot(const T* arr, int n, uint8_t id) {
    if (id == CONTROL_ID_NONE) return -1;
    for (int i = 0; i < n; i++) if (arr[i].id == id) return i;
    return -1;
}

template <typename T>
static int recFreeSlot(const T* arr, int n) {
    for (int i = 0; i < n; i++) if (arr[i].id == CONTROL_ID_NONE) return i;
    return -1;
}

template <typename T>
static uint8_t recAllocId(const T* arr, int n) {
    // Smallest id not currently in use.
    for (uint16_t cand = 0; cand < CONTROL_ID_NONE; cand++) {
        bool used = false;
        for (int i = 0; i < n; i++) if (arr[i].id == (uint8_t)cand) { used = true; break; }
        if (!used) return (uint8_t)cand;
    }
    return CONTROL_ID_NONE;
}

template <typename T>
static uint8_t recCount(const T* arr, int n) {
    uint8_t c = 0;
    for (int i = 0; i < n; i++) if (arr[i].id != CONTROL_ID_NONE) c++;
    return c;
}

// --- rule table helpers -------------------------------------------------

int     controlRuleSlot(uint8_t id) { return recSlot(systemConfig.rules, MAX_RULES, id); }
int     controlRuleFreeSlot()       { return recFreeSlot(systemConfig.rules, MAX_RULES); }
uint8_t controlRuleAllocId()        { return recAllocId(systemConfig.rules, MAX_RULES); }
uint8_t controlRuleCount()          { return recCount(systemConfig.rules, MAX_RULES); }

// --- application default-Rule template (§11.5) ------------------------------

// Fill a Rule's CONFIG fields (not id/sourceInput) from an application's
// default template. v1 derives the canonical shape faithfully from the legacy
// preset thresholds: an alarm-enabled app with a valid [min,max] band becomes
// an OUTSIDE_BAND ALARM Rule (exactly what the old both-sided alarm checked).
// Returns false when the app has no default alarm or a degenerate band.
static bool buildRuleConfigFromApplication(uint8_t appIndex, Rule& out) {
    const ApplicationPreset* flash = getApplicationByIndex(appIndex);
    if (!flash) return false;
    ApplicationPreset p;
    loadApplicationPreset(flash, &p);
    if (!p.defaultAlarmEnabled) return false;
    if (!(p.defaultMinValue < p.defaultMaxValue)) return false;  // no usable band
    out.condition  = RULE_OUTSIDE_BAND;
    out.severity   = RULE_SEV_ALARM;
    out.flags      = RULE_FLAG_ENABLED;
    out.limitLo    = p.defaultMinValue;
    out.limitHi    = p.defaultMaxValue;
    out.hysteresis = 0.0f;
    out.warmupMs   = p.warmupTime_ms;
    out.debounceMs = p.persistTime_ms;
    return true;
}

bool controlRuleExistsForInput(uint8_t pin) {
    for (int i = 0; i < MAX_RULES; i++) {
        if (systemConfig.rules[i].id != CONTROL_ID_NONE &&
            systemConfig.rules[i].sourceInput == pin) {
            return true;
        }
    }
    return false;
}

uint8_t controlSeedRuleForInput(uint8_t pin, uint8_t appIndex, uint32_t now) {
    if (controlRuleExistsForInput(pin)) return CONTROL_ID_NONE;  // implicit re-apply preserves
    Rule tmpl{};
    if (!buildRuleConfigFromApplication(appIndex, tmpl)) return CONTROL_ID_NONE;
    int slot = controlRuleFreeSlot();
    uint8_t id = controlRuleAllocId();
    if (slot < 0 || id == CONTROL_ID_NONE) return CONTROL_ID_NONE;  // table full
    tmpl.id = id;
    tmpl.sourceInput = pin;
    systemConfig.rules[slot] = tmpl;
    controlRuleInit(g_ruleContexts[slot], systemConfig.rules[slot], now);  // create-from-template: arm (#251)
    return id;
}

// Rewrite a rule slot's CONFIG fields from a template, preserving identity
// (id + sourceInput), then re-arm its runtime context.
static void applyRuleTemplate(int slot, const Rule& tmpl, uint32_t now) {
    Rule& r = systemConfig.rules[slot];
    r.condition  = tmpl.condition;
    r.severity   = tmpl.severity;
    r.flags      = tmpl.flags;
    r.limitLo    = tmpl.limitLo;
    r.limitHi    = tmpl.limitHi;
    r.hysteresis = tmpl.hysteresis;
    r.warmupMs   = tmpl.warmupMs;
    r.debounceMs = tmpl.debounceMs;
    controlRuleInit(g_ruleContexts[slot], r, now);  // re-arm
}

bool controlRuleReset(uint8_t id, uint32_t now) {
    int slot = controlRuleSlot(id);
    if (slot < 0) return false;
    Input* in = getInputByPin(systemConfig.rules[slot].sourceInput);
    if (!in) return false;
    Rule tmpl{};
    if (!buildRuleConfigFromApplication(in->applicationIndex, tmpl)) return false;
    applyRuleTemplate(slot, tmpl, now);  // explicit reset re-arms (§11.5)
    return true;
}

// --- per-input "alarm Rule" facade (SET <pin> ALARM ...) --------------------

int controlAlarmRuleSlotForInput(uint8_t pin) {
    // The one canonical alarm Rule for an input is OUTSIDE_BAND / ALARM (what
    // SET APPLICATION seeds). One-sided ABOVE/BELOW Rules a user adds via RULE
    // ADD are deliberately NOT this facade's business — the per-input min/max
    // model can't represent them, so they're left untouched.
    for (int i = 0; i < MAX_RULES; i++) {
        const Rule& r = systemConfig.rules[i];
        if (r.id != CONTROL_ID_NONE && r.sourceInput == pin &&
            r.condition == RULE_OUTSIDE_BAND && r.severity == RULE_SEV_ALARM) {
            return i;
        }
    }
    return -1;
}

uint8_t controlSetAlarmBandForInput(uint8_t pin, float lo, float hi, uint32_t now) {
    int slot = controlAlarmRuleSlotForInput(pin);
    if (slot >= 0) {
        // Edit in place: preserve id, source, warmup/debounce, AND the live
        // runtime state. A band tweak must NOT re-arm the Rule — re-arming drags
        // an already-warmed rule back through INIT→WARMUP, silencing the alarm
        // (and any Action it drives) for warmupMs on every apply. On the temp
        // presets that's 20–60 s of dead air after each edit (#295). Limits are
        // read live from the Rule each tick, so the engine re-qualifies against
        // the new band next cycle with no re-init needed.
        Rule& r = systemConfig.rules[slot];
        r.condition = RULE_OUTSIDE_BAND;
        r.severity  = RULE_SEV_ALARM;
        r.flags    |= RULE_FLAG_ENABLED;
        r.limitLo   = lo;
        r.limitHi   = hi;
        // A hysteresis carried over from a wider band could exceed the new
        // band's half-width and wedge the Rule permanently asserted (the clear
        // insets would cross). Drop it rather than refuse the edit — the facade
        // doesn't surface hysteresis anyway.
        if (r.hysteresis * 2.0f >= (hi - lo)) r.hysteresis = 0.0f;
        // Arm only if it isn't already (e.g. previously flag-disabled); a live
        // READY/ACTIVE rule keeps its state and just re-qualifies.
        if (g_ruleContexts[slot].state == RULE_DISABLED) {
            controlRuleInit(g_ruleContexts[slot], r, now);
        }
        return r.id;
    }
    // None yet — allocate a fresh Rule. Inherit the application's warmup/debounce
    // (like the auto-seed) so a webapp-set alarm behaves identically to a seeded
    // one; fall back to the hand-entry defaults when the app has no template.
    int free = controlRuleFreeSlot();
    uint8_t id = controlRuleAllocId();
    if (free < 0 || id == CONTROL_ID_NONE) return CONTROL_ID_NONE;  // table full
    Input* in = getInputByPin(pin);
    Rule appTmpl{};
    const bool haveAppTmpl = in && buildRuleConfigFromApplication(in->applicationIndex, appTmpl);
    Rule& r = systemConfig.rules[free];
    r.id          = id;
    r.sourceInput = pin;
    r.condition   = RULE_OUTSIDE_BAND;
    r.severity    = RULE_SEV_ALARM;
    r.flags       = RULE_FLAG_ENABLED;
    r.limitLo     = lo;
    r.limitHi     = hi;
    r.hysteresis  = 0.0f;
    r.warmupMs    = haveAppTmpl ? appTmpl.warmupMs   : RULE_DEFAULT_WARMUP_MS;
    r.debounceMs  = haveAppTmpl ? appTmpl.debounceMs : RULE_DEFAULT_DEBOUNCE_MS;
    controlRuleInit(g_ruleContexts[free], r, now);
    return id;
}

bool controlRemoveAlarmRuleForInput(uint8_t pin) {
    int slot = controlAlarmRuleSlotForInput(pin);
    if (slot < 0) return false;
    uint8_t id = systemConfig.rules[slot].id;
    systemConfig.rules[slot].id = CONTROL_ID_NONE;
    g_ruleContexts[slot].state = RULE_DISABLED;
    controlClearActionsForRule(id);  // referential integrity (mirrors RULE DEL)
    return true;
}

uint8_t controlReseedRulesForInput(uint8_t pin, uint32_t now) {
    Input* in = getInputByPin(pin);
    if (!in) return 0;
    Rule tmpl{};
    // No usable default band for the (new) application — leave existing rules in
    // place for the caller to flag; we can't synthesize a meaningful template.
    if (!buildRuleConfigFromApplication(in->applicationIndex, tmpl)) return 0;
    uint8_t n = 0;
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& r = systemConfig.rules[i];
        if (r.id == CONTROL_ID_NONE || r.sourceInput != pin) continue;
        applyRuleTemplate(i, tmpl, now);  // rewrite to new app's defaults, keep id
        n++;
    }
    return n;
}

void controlClearActionsForRule(uint8_t ruleId) {
    if (ruleId == CONTROL_ID_NONE) return;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (systemConfig.actions[i].id != CONTROL_ID_NONE &&
            systemConfig.actions[i].ruleRef == ruleId) {
            systemConfig.actions[i].ruleRef = CONTROL_ID_NONE;  // revert to manual-only
        }
    }
}

// ===== Actions =====

static bool g_actionManual[MAX_ACTIONS];  // commanded state for manual (ruleRef==NONE) actions
static bool g_actionState[MAX_ACTIONS];   // last driven logical (pre-invert) state, for LIST

// Write a resolved logical state to an action's target, applying inversion.
static void driveAction(const Action& a, bool on) {
    bool invert = (a.behavior & ACT_BEHAVIOR_INVERT) != 0;
    digitalWrite(a.target, (on != invert) ? HIGH : LOW);  // (on XOR invert)
}

void controlActionConfigurePin(int slot) {
    if (slot < 0 || slot >= MAX_ACTIONS) return;
    g_actionManual[slot] = false;  // a (re)configured slot starts de-asserted (no stale manual)
    Action& a = systemConfig.actions[slot];
    if (a.id == CONTROL_ID_NONE) return;
    if (a.type == ACT_RELAY) {
        pinMode(a.target, OUTPUT);
        driveAction(a, false);  // de-assert until first evaluation
    }
}

void controlActionsInit() {
    for (int i = 0; i < MAX_ACTIONS; i++) {
        g_actionManual[i] = false;
        g_actionState[i] = false;
        controlActionConfigurePin(i);
    }
    // NB: the pin-registry sync is deliberately NOT done here. At boot this runs
    // before the RGB and serial subsystems register their (hardware-fixed) pins,
    // so syncing now could let a stray action target claim a pin those need and
    // win the race. The caller invokes controlSyncActionPins() once, late, after
    // all fixed pins are registered (see main.cpp setup + the JSON import path).
}

uint8_t controlSyncActionPins() {
    // 1. Drop registry entries this engine previously added (identified by our
    //    shared description pointer) so a runtime config reload doesn't leave a
    //    stale action pin claimed. Collect first, then unregister — unregister
    //    shifts the array, which would corrupt an in-progress index walk.
    uint8_t stale[MAX_ACTIONS];
    uint8_t nStale = 0;
    uint8_t regSize = getPinRegistrySize();
    for (uint8_t i = 0; i < regSize && nStale < MAX_ACTIONS; i++) {
        const PinUsage* u = getPinUsageByIndex(i);
        if (u && u->description == ACTION_PIN_DESC) stale[nStale++] = u->pin;
    }
    for (uint8_t i = 0; i < nStale; i++) unregisterPin(stale[i]);

    // 2. Register each configured action target as PIN_OUTPUT. registerPin()
    //    rejects a pin already claimed (by a bus, system pin, RGB output, or
    //    another action), which is exactly the conflict we want to surface.
    uint8_t conflicts = 0;
    for (int s = 0; s < MAX_ACTIONS; s++) {
        const Action& a = systemConfig.actions[s];
        if (a.id == CONTROL_ID_NONE) continue;
        if (!registerPin(a.target, PIN_OUTPUT, ACTION_PIN_DESC)) {
            conflicts++;
            const char* held = getPinDescription(a.target);
            msg.debug.warn(TAG_SYSTEM,
                           "action #%u target pin %u conflicts with %s",
                           a.id, a.target, held ? held : "another use");
        }
    }
    return conflicts;
}

void controlActionsUpdate(bool configMode) {
    for (int i = 0; i < MAX_ACTIONS; i++) {
        Action& a = systemConfig.actions[i];
        if (a.id == CONTROL_ID_NONE) continue;

        bool ruleActive = false;
        bool sourceTrustworthy = true;
        if (a.ruleRef != CONTROL_ID_NONE) {
            int rslot = controlRuleSlot(a.ruleRef);
            if (rslot < 0) {
                // dangling ref (shouldn't happen — cleared on rule delete): off
                ruleActive = false;
                sourceTrustworthy = true;
            } else {
                ruleActive = controlRuleIsActive(g_ruleContexts[rslot]);
                Input* in = getInputByPin(systemConfig.rules[rslot].sourceInput);
                sourceTrustworthy = (in != nullptr) && !isnan(in->value);
            }
        }

        bool on = controlActionDesired(a, ruleActive, sourceTrustworthy, g_actionManual[i]);

        // Bench safety: suppress RULE-DRIVEN actuation in CONFIG so a settling /
        // garbage sensor reading can't fire a relay (#268). An explicit manual
        // action (ruleRef == NONE, driven only by ACTION <id> ON|OFF) is a
        // deliberate operator command and still actuates in CONFIG — mirrors the
        // legacy setRelayMode() MANUAL behavior (#250).
        if (configMode && a.ruleRef != CONTROL_ID_NONE) on = false;

        g_actionState[i] = on;
        if (a.type == ACT_RELAY) driveAction(a, on);
    }
}

bool controlActionState(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_actionState[slot] : false;
}

int     controlActionSlot(uint8_t id) { return recSlot(systemConfig.actions, MAX_ACTIONS, id); }
int     controlActionFreeSlot()       { return recFreeSlot(systemConfig.actions, MAX_ACTIONS); }
uint8_t controlActionAllocId()        { return recAllocId(systemConfig.actions, MAX_ACTIONS); }
uint8_t controlActionCount()          { return recCount(systemConfig.actions, MAX_ACTIONS); }

bool controlActionSetManual(uint8_t id, bool on) {
    int slot = controlActionSlot(id);
    if (slot < 0) return false;
    g_actionManual[slot] = on;
    return true;
}

#endif // ENABLE_CONTROL_ENGINE
