/*
 * rules_manager.cpp - Runtime owner of the control engine (see header).
 */

#include "rules_manager.h"

#if INCLUDE_RULES_ENGINE

#include <string.h>
#include <math.h>
#include "../storage/system_config.h"
#include "../inputs/input_manager.h"
#include "../sensors/application_presets.h"
#include "../pinctrl/pin_registry.h"
#include "../pinctrl/pin_owner.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"

// Shared, stable description pointer for every Action-owned pin registry entry.
// claimPins() stores the pointer (no copy), and rulesSyncActionPins()
// identifies its own prior entries by pointer equality against this — so it
// must be a single static string, not a per-call temporary.
static const char* const ACTION_PIN_DESC = "Action output";

RuleContext g_ruleContexts[MAX_RULES];

// Persisted record tables — standalone RAM globals (mirror Input inputs[]).
// Declared extern in rules_types.h; persisted by the shared config-store arena
// (input_manager.cpp), NOT by SystemConfig. These live in BSS (zero-init), and
// id==0 is a VALID record id — so rulesResetTables() MUST stamp every slot
// empty before the tables are trusted (boot / reset / load-failure paths), or a
// blank EEPROM would arm a table full of phantom id-0 records.
Rule       rules[MAX_RULES];
Action     actions[MAX_ACTIONS];
ActionLink actionLinks[MAX_ACTION_LINKS];   // 2nd+ rule memberships (#343)

void rulesResetTables() {
    rulesAckReset();  // same BSS-zero hazard: an ack keyed to id 0 would be live (#433)
    for (int i = 0; i < MAX_RULES; i++) {
        rules[i].id = RULES_ID_NONE;
        g_ruleContexts[i].state = RULE_DISABLED;
    }
    for (int i = 0; i < MAX_ACTIONS; i++) {
        actions[i].id = RULES_ID_NONE;
    }
    for (int i = 0; i < MAX_ACTION_LINKS; i++) {
        actionLinks[i].actionId = RULES_ID_NONE;
    }
}

void rulesManagerInit(uint32_t now) {
    rulesAckReset();  // a (re)load invalidates every acknowledgement (#433)
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& rule = rules[i];
        if (rule.id == RULES_ID_NONE) {
            g_ruleContexts[i].state = RULE_DISABLED;
        } else {
            rulesRuleInit(g_ruleContexts[i], rule, now);  // boot-load: arm (#251)
            // An orphan (source pin carries no input) is INERT by design (#345),
            // and inert is exactly the state that can hide: the rule never fires
            // and never faults, so nothing else would ever mention it. Say so
            // once at load — a store that arrived with more rules than inputs (a
            // truncated save, a smaller-cap build, a hand-edited config) is a
            // real config error, not just a stale rule.
            if (getInputByPin(rule.sourceInput) == nullptr) {
                // CONTROL plane, not debug, for two reasons. (1) This path also
                // runs on JSON IMPORT — the other orphan producer, which accepts
                // a rule whose input pin never loaded — and the app/webapp scan
                // the *import response* for `WARNING:` lines, so a debug-plane
                // notice would be invisible to exactly the client that caused it.
                // (2) F() strings live in flash; msg.debug's printf strings are
                // RAM-resident on AVR (message_api.h) and mega2560 has ~200 B of
                // static headroom left.
                msg.control.print(F("WARNING: rule #"));
                msg.control.print(rule.id);
                msg.control.print(F(" sources pin "));
                msg.control.print(rule.sourceInput);
                msg.control.println(F(" which has no input: inert (cannot fire, "
                                      "cannot force a failsafe)"));
            }
        }
    }
}

void rulesManagerUpdate(uint32_t now) {
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& rule = rules[i];
        if (rule.id == RULES_ID_NONE) continue;

        // Resolve the source input by pin (the stable input identity). A missing
        // source yields NaN, which the engine treats as untrustworthy — the rule
        // can't assert (failsafe intent).
        Input* in = getInputByPin(rule.sourceInput);
        float value = in ? in->value : NAN;

        rulesRuleUpdate(rule, g_ruleContexts[i], value, now);
    }
}

// Annunciator severity of one slot (derived WARNING tier included, §11.2).
static RuleSeverity slotAnnunciatorSeverity(int i) {
    const Rule& rule = rules[i];
    if (rule.id == RULES_ID_NONE) return RULE_SEV_NONE;
    Input* in = getInputByPin(rule.sourceInput);
    float value = in ? in->value : NAN;
    return rulesRuleAnnunciatorSeverity(rule, g_ruleContexts[i], value);
}

static RuleSeverity systemSeverity(bool skipAcked) {
    uint8_t maxSev = RULE_SEV_NONE;
    for (int i = 0; i < MAX_RULES; i++) {
        if (skipAcked && rulesRuleAcked(i)) continue;
        uint8_t s = slotAnnunciatorSeverity(i);
        if (s > maxSev) maxSev = s;
    }
    return (RuleSeverity)maxSev;
}

RuleSeverity rulesSystemSeverity()        { return systemSeverity(false); }
RuleSeverity rulesSystemSeverityUnacked() { return systemSeverity(true);  }

// --- annunciator acknowledgement (#433) --------------------------------------
// Per-SLOT, keyed by the rule id that was acknowledged so a slot reused by a
// different record never inherits an ack. RAM only — an ack is a moment, not
// config — and MAX_RULES*5 B (AVR: 10 B).

static uint8_t  g_ackRuleId[MAX_RULES];   // RULES_ID_NONE = not acknowledged
static uint32_t g_ackAtMs[MAX_RULES];

void rulesAckReset() {
    for (int i = 0; i < MAX_RULES; i++) g_ackRuleId[i] = RULES_ID_NONE;
}

bool rulesRuleAcked(int slot) {
    return slot >= 0 && slot < MAX_RULES &&
           g_ackRuleId[slot] != RULES_ID_NONE && g_ackRuleId[slot] == rules[slot].id;
}

uint8_t rulesAckActiveAlarms(uint32_t now) {
    uint8_t n = 0;
    for (int i = 0; i < MAX_RULES; i++) {
        // Only what is BUZZING now: an ACTIVE rule at ALARM severity. The
        // approach tier (WARNING) and WARNING/INFO rules never sound, so there
        // is nothing to acknowledge — and acking them would suppress the LED
        // under SCOPE ALL for a state the operator never heard.
        if (slotAnnunciatorSeverity(i) != RULE_SEV_ALARM) continue;
        if (rulesRuleAcked(i)) continue;    // re-pressing doesn't restart the clock
        g_ackRuleId[i] = rules[i].id;
        g_ackAtMs[i]   = now;
        n++;
    }
    return n;
}

void rulesAckExpire(uint32_t now, uint32_t durationMs) {
    for (int i = 0; i < MAX_RULES; i++) {
        if (g_ackRuleId[i] == RULES_ID_NONE) continue;
        const bool gone = (g_ackRuleId[i] != rules[i].id);   // deleted / slot reused
        bool lapsed;
        if (durationMs) {
            // Timed: "mute THIS rule for N seconds", full stop. It deliberately
            // survives a clear-and-retrip inside the window — a flaky sensor
            // (NaN glitch de-asserts an ACTIVE rule instantly, then it
            // re-trips after debounce) would otherwise re-sound on every
            // glitch, which is exactly what the old global window rode through.
            lapsed = (uint32_t)(now - g_ackAtMs[i]) >= durationMs;
        } else {
            // Until clear: lapse the moment the rule stops sounding, so a
            // genuine re-trip is heard. A flapping sensor under this policy
            // re-sounds by design — use a timed duration or disable the alarm.
            lapsed = (slotAnnunciatorSeverity(i) != RULE_SEV_ALARM);
        }
        if (gone || lapsed) g_ackRuleId[i] = RULES_ID_NONE;
    }
}

uint8_t rulesAckCount() {
    uint8_t n = 0;
    for (int i = 0; i < MAX_RULES; i++) if (rulesRuleAcked(i)) n++;
    return n;
}

uint32_t rulesAckElapsedMs(int slot, uint32_t now) {
    return rulesRuleAcked(slot) ? (uint32_t)(now - g_ackAtMs[slot]) : 0;
}

// --- generic record-table helpers ---------------------------------------
// Rule and Action are both id-keyed records (RULES_ID_NONE = empty slot; ids
// are stable per record §8 Q9, so a freed id is safe to reuse). These templated
// scans single-source the rule and action table lookups below.

template <typename T>
static int recSlot(const T* arr, int n, uint8_t id) {
    if (id == RULES_ID_NONE) return -1;
    for (int i = 0; i < n; i++) if (arr[i].id == id) return i;
    return -1;
}

template <typename T>
static int recFreeSlot(const T* arr, int n) {
    for (int i = 0; i < n; i++) if (arr[i].id == RULES_ID_NONE) return i;
    return -1;
}

template <typename T>
static uint8_t recAllocId(const T* arr, int n) {
    // Smallest id not currently in use.
    for (uint16_t cand = 0; cand < RULES_ID_NONE; cand++) {
        bool used = false;
        for (int i = 0; i < n; i++) if (arr[i].id == (uint8_t)cand) { used = true; break; }
        if (!used) return (uint8_t)cand;
    }
    return RULES_ID_NONE;
}

template <typename T>
static uint8_t recCount(const T* arr, int n) {
    uint8_t c = 0;
    for (int i = 0; i < n; i++) if (arr[i].id != RULES_ID_NONE) c++;
    return c;
}

// --- rule table helpers -------------------------------------------------

int     rulesRuleSlot(uint8_t id) { return recSlot(rules, MAX_RULES, id); }
int     rulesRuleFreeSlot()       { return recFreeSlot(rules, MAX_RULES); }
uint8_t rulesRuleAllocId()        { return recAllocId(rules, MAX_RULES); }
uint8_t rulesRuleCount()          { return recCount(rules, MAX_RULES); }

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
    out.warnLo     = 0.0f;   // AUTO both sides (warn-mode bits are 0 in `flags` above)
    out.warnHi     = 0.0f;
    out.warmupMs   = p.warmupTime_ms;
    out.debounceMs = p.persistTime_ms;
    return true;
}

bool rulesRuleExistsForInput(uint8_t pin) {
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].id != RULES_ID_NONE &&
            rules[i].sourceInput == pin) {
            return true;
        }
    }
    return false;
}

uint8_t rulesSeedRuleForInput(uint8_t pin, uint8_t appIndex, uint32_t now) {
    if (rulesRuleExistsForInput(pin)) return RULES_ID_NONE;  // implicit re-apply preserves
    Rule tmpl{};
    if (!buildRuleConfigFromApplication(appIndex, tmpl)) return RULES_ID_NONE;
    int slot = rulesRuleFreeSlot();
    uint8_t id = rulesRuleAllocId();
    if (slot < 0 || id == RULES_ID_NONE) return RULES_ID_NONE;  // table full
    tmpl.id = id;
    tmpl.sourceInput = pin;
    rules[slot] = tmpl;
    rulesRuleInit(g_ruleContexts[slot], rules[slot], now);  // create-from-template: arm (#251)
    return id;
}

// Rewrite a rule slot's CONFIG fields from a template, preserving identity
// (id + sourceInput), then re-arm its runtime context.
static void applyRuleTemplate(int slot, const Rule& tmpl, uint32_t now) {
    Rule& r = rules[slot];
    r.condition  = tmpl.condition;
    r.severity   = tmpl.severity;
    r.flags      = tmpl.flags;
    r.limitLo    = tmpl.limitLo;
    r.limitHi    = tmpl.limitHi;
    r.hysteresis = tmpl.hysteresis;
    r.warnLo     = tmpl.warnLo;   // template flags are AUTO (0) — RESET/reseed return warn modes to AUTO too
    r.warnHi     = tmpl.warnHi;
    r.warmupMs   = tmpl.warmupMs;
    r.debounceMs = tmpl.debounceMs;
    rulesRuleInit(g_ruleContexts[slot], r, now);  // re-arm
}

bool rulesRuleReset(uint8_t id, uint32_t now) {
    int slot = rulesRuleSlot(id);
    if (slot < 0) return false;
    Input* in = getInputByPin(rules[slot].sourceInput);
    if (!in) return false;
    Rule tmpl{};
    if (!buildRuleConfigFromApplication(in->applicationIndex, tmpl)) return false;
    applyRuleTemplate(slot, tmpl, now);  // explicit reset re-arms (§11.5)
    return true;
}

// --- per-input "alarm Rule" facade (SET <pin> ALARM ...) --------------------

int rulesAlarmRuleSlotForInput(uint8_t pin) {
    // The one canonical alarm Rule for an input is any ALARM-severity Rule whose
    // condition the per-input facade can represent: OUTSIDE_BAND (a Min+Max band,
    // what SET APPLICATION seeds), BELOW (a Min-only floor), or ABOVE (a Max-only
    // ceiling) — §11.5, #312. Non-ALARM (control/WARNING/INFO) Rules and the
    // deferred INSIDE_BAND stay out of the facade — those remain RULE-only.
    //
    // The facade models exactly ONE alarm per input, so if an input somehow holds
    // more than one facade-eligible ALARM Rule (only reachable by mixing the SET
    // ALARM facade with hand-added RULE ADD, since RULE ADD defaults to ALARM), a
    // BAND wins over a one-sided Rule. That keeps the pre-#312 behaviour byte-for-
    // byte — the facade matched only OUTSIDE_BAND before, so any config with a band
    // resolves to the exact same slot it always did — and only falls back to a
    // one-sided Rule when no band exists (the genuinely new case).
    int firstOneSided = -1;
    for (int i = 0; i < MAX_RULES; i++) {
        const Rule& r = rules[i];
        if (r.id == RULES_ID_NONE || r.sourceInput != pin ||
            r.severity != RULE_SEV_ALARM) {
            continue;
        }
        if (r.condition == RULE_OUTSIDE_BAND) return i;   // band is canonical; prefer it
        if ((r.condition == RULE_BELOW || r.condition == RULE_ABOVE) && firstOneSided < 0) {
            firstOneSided = i;
        }
    }
    return firstOneSided;
}

// Write the facade alarm's condition + limits into `r`, clamping any carried-over
// hysteresis the new shape can't support (mirrors the RULE ADD guards). The facade
// doesn't surface hysteresis, so a value that would wedge the Rule is dropped
// rather than the edit refused.
//
// Also clamps warnLo/warnHi (#425, amendment A7): a limit move can strand a SET
// warn side on the wrong side of its new limit (or, band->one-sided, on a hi
// side that no longer exists). Rather than refuse the shape edit, every SET side
// that `rulesWarnSideValid` now rejects is degraded to AUTO — same precedent as
// the HYST clamp above, so an operator dragging the alarm slider is never
// blocked by a stale warn edge.
//
// The warn slots are remapped by MEANING across a condition change (#427). The
// lo slot is the FLOOR's approach edge for BELOW/OUTSIDE_BAND but the CEILING's
// for ABOVE (the dump reports it as warnMax there); the hi slot is the ceiling's
// for OUTSIDE_BAND only. Carrying the slots verbatim across ABOVE<->OUTSIDE (or
// ABOVE<->BELOW) therefore silently turned a ceiling pre-warn into a floor
// pre-warn — a directionally wrong edge that rulesNormalizeWarn() cannot catch
// because it only checks the edge sits between the limits. MAX 120 / WARN 110 /
// ALARM 0 120 left lo=SET 110 on the band: WARNING at every reading. So: pull
// the floor and ceiling settings out of the OLD shape, place them into the NEW
// shape's slots, and drop a side the new shape has no room for.
//
// Returns a mask in facade terms so a caller can print an honest NOTE:
//   RULE_WARN_RESET_MIN   floor   side degraded to AUTO (limit moved past it)
//   RULE_WARN_RESET_MAX   ceiling side degraded to AUTO (limit moved past it)
//   RULE_WARN_DROPPED_MIN floor   side cleared (new shape has no lower side)
//   RULE_WARN_DROPPED_MAX ceiling side cleared (new shape has no upper side)
// A dropped side is reported only if it held something other than AUTO/0 —
// an AUTO->AUTO no-op is not a change the operator needs to hear about.
static bool slotIsCeiling(uint8_t condition, bool hiSide) {
    // Which real-world side a warn slot stands for under `condition`.
    if (condition == RULE_ABOVE) return !hiSide;   // lo slot = ceiling; no hi slot
    return hiSide;                                  // BELOW/OUTSIDE: lo = floor, hi = ceiling
}

static uint8_t applyAlarmShape(Rule& r, uint8_t condition, float lo, float hi) {
    const uint8_t prev = r.condition;
    uint8_t mask = 0;

    // --- warn slots: lift floor/ceiling out of the old shape ------------------
    RuleWarnMode floorMode = RULE_WARN_AUTO, ceilMode = RULE_WARN_AUTO;
    float        floorVal  = 0.0f,           ceilVal  = 0.0f;
    const bool prevOneSided = (prev == RULE_ABOVE || prev == RULE_BELOW);
    if (prev == RULE_OUTSIDE_BAND || prevOneSided) {
        for (int side = 0; side < 2; ++side) {
            const bool hiSide = (side == 1);
            if (hiSide && prevOneSided) break;   // one-sided shapes have no hi slot
            const RuleWarnMode m = ruleWarnMode(r, hiSide);
            const float        v = hiSide ? r.warnHi : r.warnLo;
            if (slotIsCeiling(prev, hiSide)) { ceilMode = m; ceilVal = v; }
            else                             { floorMode = m; floorVal = v; }
        }
    }
    // A side the new shape cannot hold is dropped — say so if it was set.
    const bool newHasFloor = (condition != RULE_ABOVE);
    const bool newHasCeil  = (condition != RULE_BELOW);
    if (!newHasFloor && (floorMode != RULE_WARN_AUTO || floorVal != 0.0f)) mask |= RULE_WARN_DROPPED_MIN;
    if (!newHasCeil  && (ceilMode  != RULE_WARN_AUTO || ceilVal  != 0.0f)) mask |= RULE_WARN_DROPPED_MAX;

    // --- condition + limits -----------------------------------------------------
    r.condition = condition;
    if (condition == RULE_OUTSIDE_BAND) {
        r.limitLo = lo;
        r.limitHi = hi;
        // Hysteresis wider than the band half-width would cross the clear insets
        // and wedge the Rule permanently asserted.
        if (r.hysteresis * 2.0f >= (hi - lo)) r.hysteresis = 0.0f;
    } else {  // RULE_BELOW / RULE_ABOVE — a single limit lives in limitLo.
        r.limitLo = lo;
        r.limitHi = 0.0f;  // unused for one-sided; keep deterministic for the dump
        // Hysteresis at/above the limit magnitude could invert the clear inset.
        if (r.hysteresis >= fabsf(lo)) r.hysteresis = 0.0f;
    }

    // --- warn slots: place floor/ceiling into the new shape ----------------------
    // Hi slot is AUTO/0 for a one-sided shape regardless of what it held before.
    ruleSetWarnMode(r, false, RULE_WARN_AUTO); r.warnLo = 0.0f;
    ruleSetWarnMode(r, true,  RULE_WARN_AUTO); r.warnHi = 0.0f;
    if (condition == RULE_ABOVE) {
        ruleSetWarnMode(r, false, ceilMode);  r.warnLo = ceilVal;
    } else {
        ruleSetWarnMode(r, false, floorMode); r.warnLo = floorVal;
        if (condition == RULE_OUTSIDE_BAND) { ruleSetWarnMode(r, true, ceilMode); r.warnHi = ceilVal; }
    }

    // --- degrade anything the new limits stranded, reported per real side --------
    const uint8_t slotMask = rulesNormalizeWarn(r);   // bit0 = lo slot, bit1 = hi slot
    if (slotMask & 0x01) mask |= slotIsCeiling(condition, false) ? RULE_WARN_RESET_MAX : RULE_WARN_RESET_MIN;
    if (slotMask & 0x02) mask |= RULE_WARN_RESET_MAX;  // hi slot only exists on a band: ceiling
    return mask;
}

uint8_t rulesSetAlarmRuleForInput(uint8_t pin, uint8_t condition, float lo, float hi, uint32_t now,
                                   uint8_t* warnResetOut) {
    if (warnResetOut) *warnResetOut = 0;
    int slot = rulesAlarmRuleSlotForInput(pin);
    if (slot >= 0) {
        // Edit in place: preserve id, source, warmup/debounce, AND the live
        // runtime state. An alarm tweak must NOT re-arm the Rule — re-arming drags
        // an already-warmed rule back through INIT→WARMUP, silencing the alarm
        // (and any Action it drives) for warmupMs on every apply. On the temp
        // presets that's 20–60 s of dead air after each edit (#295). Limits and
        // condition are read live from the Rule each tick, so the engine
        // re-qualifies against the new shape next cycle with no re-init needed —
        // this holds even when the condition changes (band↔one-sided, #312).
        Rule& r = rules[slot];
        r.severity  = RULE_SEV_ALARM;
        r.flags    |= RULE_FLAG_ENABLED;
        const uint8_t warnMask = applyAlarmShape(r, condition, lo, hi);
        if (warnResetOut) *warnResetOut = warnMask;
        // Arm only if it isn't already (e.g. previously flag-disabled); a live
        // READY/ACTIVE rule keeps its state and just re-qualifies.
        if (g_ruleContexts[slot].state == RULE_DISABLED) {
            rulesRuleInit(g_ruleContexts[slot], r, now);
        }
        return r.id;
    }
    // None yet — allocate a fresh Rule. Inherit the application's warmup/debounce
    // (like the auto-seed) so a webapp-set alarm behaves identically to a seeded
    // one; fall back to the hand-entry defaults when the app has no template.
    int free = rulesRuleFreeSlot();
    uint8_t id = rulesRuleAllocId();
    if (free < 0 || id == RULES_ID_NONE) return RULES_ID_NONE;  // table full
    Input* in = getInputByPin(pin);
    Rule appTmpl{};
    const bool haveAppTmpl = in && buildRuleConfigFromApplication(in->applicationIndex, appTmpl);
    Rule& r = rules[free];
    r.id          = id;
    r.sourceInput = pin;
    r.severity    = RULE_SEV_ALARM;
    r.flags       = RULE_FLAG_ENABLED;
    r.hysteresis  = 0.0f;
    r.warnLo      = 0.0f;   // AUTO both sides (mode bits are 0 in the flags above)
    r.warnHi      = 0.0f;
    applyAlarmShape(r, condition, lo, hi);  // a brand-new Rule is already AUTO; mask is always 0
    r.warmupMs    = haveAppTmpl ? appTmpl.warmupMs   : RULE_DEFAULT_WARMUP_MS;
    r.debounceMs  = haveAppTmpl ? appTmpl.debounceMs : RULE_DEFAULT_DEBOUNCE_MS;
    rulesRuleInit(g_ruleContexts[free], r, now);
    return id;
}

bool rulesSetAlarmWarnForInput(uint8_t pin, RuleWarnMode loMode, float lo,
                                RuleWarnMode hiMode, float hi, uint8_t* badSideOut) {
    if (badSideOut) *badSideOut = 0xFF;
    int slot = rulesAlarmRuleSlotForInput(pin);
    if (slot < 0) return false;  // no alarm Rule on this input — caller reports
    // Validate on a COPY, write only if both sides check out (§11.2, amendment
    // A2): this is an explicit operator WARN edit, so an illegal side is
    // REJECTED, never silently degraded — degrade is reserved for a limit move
    // dragging an existing edge along with it (applyAlarmShape above).
    Rule copy = rules[slot];
    const bool oneSided = (copy.condition == RULE_ABOVE || copy.condition == RULE_BELOW);
    if (oneSided && hiMode != RULE_WARN_AUTO) {
        // A one-sided rule has no hi side to set/clear; the caller (RULE <id>
        // WARN / SET ALARM WARN) must send exactly one value for these shapes.
        if (badSideOut) *badSideOut = 1;
        return false;
    }
    ruleSetWarnMode(copy, false, loMode);
    copy.warnLo = (loMode == RULE_WARN_SET) ? lo : 0.0f;
    ruleSetWarnMode(copy, true, hiMode);
    copy.warnHi = (hiMode == RULE_WARN_SET) ? hi : 0.0f;
    if (!rulesWarnSideValid(copy, false)) {
        if (badSideOut) *badSideOut = 0;
        return false;
    }
    if (!rulesWarnSideValid(copy, true)) {
        if (badSideOut) *badSideOut = 1;
        return false;
    }
    // No re-arm (mirrors the shape-edit facade above, #295): warn edges are
    // read live from the Rule each tick, so the next evaluation just sees them.
    rules[slot] = copy;
    return true;
}

bool rulesRemoveAlarmRuleForInput(uint8_t pin, uint8_t* releasedOut) {
    int slot = rulesAlarmRuleSlotForInput(pin);
    if (slot < 0) return false;
    uint8_t id = rules[slot].id;
    rules[slot].id = RULES_ID_NONE;
    g_ruleContexts[slot].state = RULE_DISABLED;
    uint8_t released = rulesClearActionsForRule(id);  // referential integrity (mirrors RULE DEL)
    if (releasedOut) *releasedOut = released;
    return true;
}

uint8_t rulesRemoveRulesForInput(uint8_t pin, uint8_t* releasedOut) {
    // `released` sums per-rule counts (each <= MAX_ACTIONS), so it is widened and
    // saturated rather than wrapped: an under-report here would understate how
    // many relays a CLEAR just de-energized.
    uint8_t removed = 0;
    uint16_t released = 0;
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& r = rules[i];
        if (r.id == RULES_ID_NONE || r.sourceInput != pin) continue;
        const uint8_t id = r.id;
        r.id = RULES_ID_NONE;
        g_ruleContexts[i].state = RULE_DISABLED;
        released += rulesClearActionsForRule(id);  // mirrors RULE DEL
        removed++;
    }
    if (releasedOut) *releasedOut = (uint8_t)((released > 255) ? 255 : released);
    return removed;
}

bool rulesRuleSetEnabled(uint8_t id, bool enable, uint32_t now) {
    int slot = rulesRuleSlot(id);
    if (slot < 0) return false;
    Rule& r = rules[slot];
    const bool wasEnabled = (r.flags & RULE_FLAG_ENABLED) != 0;
    if (wasEnabled == enable) return false;   // idempotent: touch nothing (#254)
    if (enable) {
        r.flags |= RULE_FLAG_ENABLED;
        rulesRuleInit(g_ruleContexts[slot], r, now);   // re-arm: INIT -> WARMUP -> READY
    } else {
        r.flags &= (uint8_t)~RULE_FLAG_ENABLED;
        g_ruleContexts[slot].state = RULE_DISABLED;
        // Parking IS a state entry: `INFO <pin> ALARM` prints time-in-state, and
        // leaving the old stamp would report "PARKED, time in state: 1 h" for a
        // rule parked one second ago (the enable branch re-stamps via
        // rulesRuleInit).
        g_ruleContexts[slot].stateEntryTime = now;
        g_ruleContexts[slot].faultStartTime = 0;
    }
    return true;
}

uint8_t rulesReseedRulesForInput(uint8_t pin, uint32_t now) {
    Input* in = getInputByPin(pin);
    if (!in) return 0;
    Rule tmpl{};
    // No usable default band for the (new) application — leave existing rules in
    // place for the caller to flag; we can't synthesize a meaningful template.
    if (!buildRuleConfigFromApplication(in->applicationIndex, tmpl)) return 0;
    uint8_t n = 0;
    for (int i = 0; i < MAX_RULES; i++) {
        Rule& r = rules[i];
        if (r.id == RULES_ID_NONE || r.sourceInput != pin) continue;
        applyRuleTemplate(i, tmpl, now);  // rewrite to new app's defaults, keep id
        n++;
    }
    return n;
}

// ===== Actions =====

// --- Rule<->Action membership (M:N, §11.6 / #343) ------------------------
// An Action's FIRST member rule is stored inline in Action.ruleRef; the 2nd and
// later live in actionLinks[]. Every consumer goes through these accessors, so
// the split representation is invisible outside this block — and a store written
// before #343 (zero link rows) reads back as a one-member action unchanged.

uint8_t rulesActionRuleCount(int slot) {
    if (slot < 0 || slot >= MAX_ACTIONS) return 0;
    const Action& a = actions[slot];
    if (a.id == RULES_ID_NONE || a.ruleRef == RULES_ID_NONE) return 0;
    uint8_t n = 1;
    for (int i = 0; i < MAX_ACTION_LINKS; i++)
        if (actionLinks[i].actionId == a.id) n++;
    return n;
}

uint8_t rulesActionRuleAt(int slot, uint8_t index) {
    if (slot < 0 || slot >= MAX_ACTIONS) return RULES_ID_NONE;
    const Action& a = actions[slot];
    if (a.id == RULES_ID_NONE || a.ruleRef == RULES_ID_NONE) return RULES_ID_NONE;
    if (index == 0) return a.ruleRef;
    uint8_t n = 1;
    for (int i = 0; i < MAX_ACTION_LINKS; i++) {
        if (actionLinks[i].actionId != a.id) continue;
        if (n++ == index) return actionLinks[i].ruleId;
    }
    return RULES_ID_NONE;
}

bool rulesActionHasRule(int slot, uint8_t ruleId) {
    if (ruleId == RULES_ID_NONE) return false;
    uint8_t n = rulesActionRuleCount(slot);
    for (uint8_t i = 0; i < n; i++)
        if (rulesActionRuleAt(slot, i) == ruleId) return true;
    return false;
}

uint8_t rulesActionLinkCount() {
    uint8_t c = 0;
    for (int i = 0; i < MAX_ACTION_LINKS; i++)
        if (actionLinks[i].actionId != RULES_ID_NONE) c++;
    return c;
}

// Drop every link row belonging to `actionId` (the action itself is untouched).
static void dropLinksForAction(uint8_t actionId) {
    if (actionId == RULES_ID_NONE) return;
    for (int i = 0; i < MAX_ACTION_LINKS; i++)
        if (actionLinks[i].actionId == actionId) actionLinks[i].actionId = RULES_ID_NONE;
}

bool rulesActionAddRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving) {
    if (wasDriving) *wasDriving = false;
    int slot = rulesActionSlot(actionId);
    if (slot < 0 || ruleId == RULES_ID_NONE) return false;
    if (rulesActionHasRule(slot, ruleId)) return true;  // idempotent
    Action& a = actions[slot];
    if (a.ruleRef == RULES_ID_NONE) {
        a.ruleRef = ruleId;                       // first membership: inline
    } else {
        int free = -1;
        for (int i = 0; i < MAX_ACTION_LINKS && free < 0; i++)
            if (actionLinks[i].actionId == RULES_ID_NONE) free = i;
        if (free < 0) return false;               // link table full — caller reports
        actionLinks[free].actionId = actionId;
        actionLinks[free].ruleId   = ruleId;
    }
    // The membership set is what drives this action, so changing it changes the
    // driver — release override + manual latch, the one invariant that keeps the
    // #294 stale-latch snap-on unreachable (#329). Deliberately unconditional:
    // reasoning about which membership a live force "belonged to" is exactly the
    // ambiguity that produced that bug class. Report it: silently de-energizing a
    // relay an operator was holding by hand is the hazard #329 added reporting for.
    const bool released = rulesActionReleaseDrive(actionId);
    if (wasDriving) *wasDriving = released;
    return true;
}

bool rulesActionRemoveRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving) {
    if (wasDriving) *wasDriving = false;
    int slot = rulesActionSlot(actionId);
    if (slot < 0 || ruleId == RULES_ID_NONE) return false;
    if (!rulesActionHasRule(slot, ruleId)) return false;
    Action& a = actions[slot];
    if (a.ruleRef == ruleId) {
        // Removing the inline member: PROMOTE the first link row into it, so
        // "ruleRef == NONE" keeps meaning "no members at all" everywhere.
        a.ruleRef = RULES_ID_NONE;
        for (int i = 0; i < MAX_ACTION_LINKS; i++) {
            if (actionLinks[i].actionId != actionId) continue;
            a.ruleRef = actionLinks[i].ruleId;
            actionLinks[i].actionId = RULES_ID_NONE;
            break;
        }
    } else {
        for (int i = 0; i < MAX_ACTION_LINKS; i++) {
            if (actionLinks[i].actionId == actionId && actionLinks[i].ruleId == ruleId) {
                actionLinks[i].actionId = RULES_ID_NONE;
                break;
            }
        }
    }
    const bool released = rulesActionReleaseDrive(actionId);
    if (wasDriving) *wasDriving = released;
    return true;
}

bool rulesActionClearRules(uint8_t actionId) {
    int slot = rulesActionSlot(actionId);
    if (slot < 0) return false;
    actions[slot].ruleRef = RULES_ID_NONE;
    dropLinksForAction(actionId);
    return rulesActionReleaseDrive(actionId);
}

bool rulesActionSetRule(uint8_t actionId, uint8_t ruleId, bool* wasDriving) {
    if (wasDriving) *wasDriving = false;
    int slot = rulesActionSlot(actionId);
    if (slot < 0 || ruleId == RULES_ID_NONE) return false;
    actions[slot].ruleRef = ruleId;
    dropLinksForAction(actionId);   // "exactly this one" — drop any extra members
    const bool released = rulesActionReleaseDrive(actionId);
    if (wasDriving) *wasDriving = released;
    return true;
}

void rulesActionForgetLinks(uint8_t actionId) {
    dropLinksForAction(actionId);
}

uint8_t rulesSweepActionRules() {
    // ORDER MATTERS: prune first, so a link row pointing at a missing rule can't
    // be promoted into ruleRef below and resurrect the dangling ref we just fixed.
    uint8_t repairs = rulesPruneActionLinks();
    for (int s = 0; s < MAX_ACTIONS; s++) {
        Action& a = actions[s];
        if (a.id == RULES_ID_NONE || a.ruleRef == RULES_ID_NONE) continue;
        if (rulesRuleSlot(a.ruleRef) >= 0) continue;
        // The inline member's Rule is gone. PROMOTE a surviving link row rather
        // than blanking ruleRef: with links present, ruleRef == NONE would read as
        // "manual-only" everywhere (rulesActionRuleCount short-circuits on it)
        // and silently strand the remaining members — and hand the action back to
        // its manual latch, the #294 failure mode.
        msg.debug.warn(TAG_SYSTEM, "action #%u referenced missing rule #%u",
                       a.id, a.ruleRef);
        a.ruleRef = RULES_ID_NONE;
        for (int i = 0; i < MAX_ACTION_LINKS; i++) {
            if (actionLinks[i].actionId != a.id) continue;
            a.ruleRef = actionLinks[i].ruleId;
            actionLinks[i].actionId = RULES_ID_NONE;
            break;
        }
        // Whatever drove it is no longer what drives it (#294/#329).
        rulesActionReleaseDrive(a.id);
        repairs++;
    }
    return repairs;
}

uint8_t rulesPruneActionLinks() {
    uint8_t dropped = 0;
    for (int i = 0; i < MAX_ACTION_LINKS; i++) {
        ActionLink& l = actionLinks[i];
        if (l.actionId == RULES_ID_NONE) continue;
        const int aslot = rulesActionSlot(l.actionId);
        // Drop a row whose action or rule is gone, and one that duplicates the
        // action's inline member (a hand-edited store / partial JSON import can
        // produce either; both would otherwise inflate the reported member count).
        bool drop = (aslot < 0 || rulesRuleSlot(l.ruleId) < 0 ||
                     actions[aslot].ruleRef == l.ruleId);
        // …and one that duplicates an EARLIER surviving row. Membership is a SET:
        // rulesActionAddRule() can't create a repeat (it is idempotent), but this
        // sweep exists for stores this firmware did not write. Two identical rows
        // would make rulesActionRuleCount() over-report, and RULE REMOVE clears
        // only one — so the action stays rule-driven after the operator believes
        // they removed its last membership for that rule.
        for (int j = 0; j < i && !drop; j++) {
            drop = (actionLinks[j].actionId == l.actionId &&
                    actionLinks[j].ruleId   == l.ruleId);
        }
        if (drop) {
            l.actionId = RULES_ID_NONE;
            dropped++;
        }
    }
    // Postcondition — this is what makes prune-BEFORE-promote sufficient rather
    // than merely tidy: every surviving row for an action is now distinct from the
    // other rows AND from that action's inline ruleRef, so the promote step in
    // rulesSweepActionRules() cannot install a ruleRef that collides with a row
    // it left behind. Duplicates can't be re-introduced after this point.
    return dropped;
}

static bool g_actionManual[MAX_ACTIONS];  // commanded state for manual (no member rules) actions
static bool g_actionState[MAX_ACTIONS];   // last driven logical (pre-invert) state, for LIST
// Operator override of a rule-driven action (#329). Deliberately a DISTINCT field
// from g_actionManual so unlinking a rule leaves AUTO and falls through to the
// already-cleared manual latch — never the stale latch #294 removed. RAM-only:
// nothing here is persisted, so a forced relay reverts to AUTO on reboot.
static ActionOverride g_actionOverride[MAX_ACTIONS];

uint8_t rulesClearActionsForRule(uint8_t ruleId) {
    if (ruleId == RULES_ID_NONE) return 0;
    uint8_t released = 0;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (actions[i].id == RULES_ID_NONE) continue;
        if (!rulesActionHasRule(i, ruleId)) continue;
        // Losing a member must also drop whatever was driving the action, or the
        // pin snaps to a stale state nobody commanded (#294 / #329) — that is
        // rulesActionRemoveRule()'s job; we only need to report it, so sample
        // the drive state first.
        const bool wasDriving = (g_actionOverride[i] != ACT_OVR_AUTO) || g_actionManual[i];
        rulesActionRemoveRule(actions[i].id, ruleId);
        if (wasDriving) released++;
    }
    return released;
}

// Write a resolved logical state to an action's target, applying inversion.
static void driveAction(const Action& a, bool on) {
    bool invert = (a.behavior & ACT_BEHAVIOR_INVERT) != 0;
    digitalWrite(a.target, (on != invert) ? HIGH : LOW);  // (on XOR invert)
}

void rulesActionConfigurePin(int slot) {
    if (slot < 0 || slot >= MAX_ACTIONS) return;
    g_actionManual[slot]   = false;  // a (re)configured slot starts de-asserted (no stale manual)
    g_actionOverride[slot] = ACT_OVR_AUTO;  // ...and no stale operator force (#329)
    Action& a = actions[slot];
    if (a.id == RULES_ID_NONE) return;
    if (a.type == ACT_RELAY) {
        pinMode(a.target, OUTPUT);
        driveAction(a, false);  // de-assert until first evaluation
    }
}

void rulesActionsInit() {
    for (int i = 0; i < MAX_ACTIONS; i++) {
        g_actionManual[i] = false;
        g_actionState[i] = false;
        g_actionOverride[i] = ACT_OVR_AUTO;
        rulesActionConfigurePin(i);
    }
    // NB: the pin-registry sync is deliberately NOT done here. At boot this runs
    // before the RGB and serial subsystems register their (hardware-fixed) pins,
    // so syncing now could let a stray action target claim a pin those need and
    // win the race. The caller invokes rulesSyncActionPins() once, late, after
    // all fixed pins are registered (see main.cpp setup + the JSON import path).
}

uint8_t rulesSyncActionPins() {
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

    // 2. Register each configured action target as PIN_OUTPUT. claimPins()
    //    rejects a pin already claimed (by a bus, system pin, RGB output, a
    //    configured input, or another action), which is exactly the conflict
    //    we want to surface.
    uint8_t conflicts = 0;
    for (int s = 0; s < MAX_ACTIONS; s++) {
        const Action& a = actions[s];
        if (a.id == RULES_ID_NONE) continue;
        if (!claimPins(&a.target, 1, PIN_OUTPUT, PIN_RANK_ACTION, TAG_SYSTEM, ACTION_PIN_DESC)) {
            conflicts++;
            const char* held = getPinDescription(a.target);
            msg.debug.warn(TAG_SYSTEM,
                           "action #%u target pin %u conflicts with %s",
                           a.id, a.target, held ? held : "another use");
        }
    }
    return conflicts;
}

// Is this action's rule source untrustworthy? Shared by the evaluation loop and
// by the CLI/BLE reporting paths, so "source faulted" is computed exactly once
// (#329). A manual-only or dangling-ref action is never "faulted" — it has no
// source to fault.
// The single definition of "this rule's source can't be trusted". Everything else
// — the evaluation loop, the CLI row, the BLE frame — funnels through here, so the
// operator can never be shown a fault state that differs from the one that drove
// the pin. `rslot < 0` (dangling ref) is deliberately NOT a fault: it matches the
// loop's `sourceTrustworthy = true` branch for that case.
static bool sourceFaultedForRuleSlot(int rslot) {
    if (rslot < 0) return false;
    // A DISABLED rule can never assert, so it must not be able to force a
    // failsafe either — otherwise it is pure downside: with M:N membership (#343)
    // `anyFaulted` ORs across members, so one parked rule watching an unconfigured
    // pin would latch a fail-ON relay on forever, and "disable the rule" (the
    // natural way to park a member) would not remove its influence.
    if (!(rules[rslot].flags & RULE_FLAG_ENABLED)) return false;
    Input* in = getInputByPin(rules[rslot].sourceInput);
    // No input configured on the source pin at all (an ORPHAN rule — CLEAR <pin>
    // used to leave these behind) is INERT, not faulted: it can never assert, so
    // by the same argument that settled the disabled-member case above it must
    // not be able to force a failsafe either. Otherwise one forgotten orphan
    // holds every FAILSAFE ON relay it is a member of energized forever, with
    // nothing in ACTION LIST naming the culprit (#345).
    if (in == nullptr) return false;
    // A CONFIGURED input reading NaN is the real sensor-loss case — open/shorted
    // probe, dead bus — and that is exactly what the failsafe exists for.
    return isnan(in->value);
}

// Resolve an action's Rule membership into the aggregate the pure resolver takes.
static ActionDrive resolveActionDrive(int slot) {
    ActionDrive d{};
    d.memberCount = rulesActionRuleCount(slot);
    for (uint8_t i = 0; i < d.memberCount; i++) {
        const int rslot = rulesRuleSlot(rulesActionRuleAt(slot, i));
        if (rslot < 0) continue;   // dangling ref (cleared on rule delete): inert,
                                   // and deliberately NOT a fault — mirrors the
                                   // pre-#343 sourceTrustworthy=true branch.
        if (sourceFaultedForRuleSlot(rslot)) {
            d.anyFaulted = true;   // a faulted source can't assert (NaN never does)
        } else if (rulesRuleIsActive(g_ruleContexts[rslot])) {
            d.anyActive = true;
        }
    }
    return d;
}

// The reporting paths (CLI, BLE frame) share the evaluation loop's resolver
// rather than re-walking membership themselves, so what an operator is shown can
// never be a different computation from what drove the pin. Reporting both bits
// from ONE call also stops packState() resolving the same membership twice per
// slot per loop — ActionBroadcaster::tick() has no scan gate.
ActionDrive rulesActionDrive(int slot) {
    if (slot < 0 || slot >= MAX_ACTIONS || actions[slot].id == RULES_ID_NONE) {
        return ActionDrive{};
    }
    return resolveActionDrive(slot);
}

// ANY member's source faulted counts (#343): with several sources, one dead
// sensor is exactly the condition an operator needs to see before trusting what
// the actuator is doing.
bool rulesActionSourceFaulted(int slot) { return rulesActionDrive(slot).anyFaulted; }

bool rulesActionRuleActive(int slot) { return rulesActionDrive(slot).anyActive; }

void rulesActionsUpdate(bool configMode) {
    for (int i = 0; i < MAX_ACTIONS; i++) {
        Action& a = actions[i];
        if (a.id == RULES_ID_NONE) continue;

        const ActionDrive drive = resolveActionDrive(i);
        bool on = rulesActionDesired(a, drive, g_actionManual[i], g_actionOverride[i]);

        // Bench safety: suppress RULE-DRIVEN actuation in CONFIG so a settling /
        // garbage sensor reading can't fire a relay (#268). An explicit manual
        // action (no member rules, driven only by ACTION <id> ON|OFF) is a
        // deliberate operator command and still actuates in CONFIG — mirrors the
        // legacy setRelayMode() MANUAL behavior (#250). An operator override is
        // the same kind of deliberate command, so it actuates in CONFIG too (#329).
        if (configMode && drive.memberCount > 0 &&
            g_actionOverride[i] == ACT_OVR_AUTO) on = false;

        g_actionState[i] = on;
        if (a.type == ACT_RELAY) driveAction(a, on);
    }
}

bool rulesActionState(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_actionState[slot] : false;
}

int     rulesActionSlot(uint8_t id) { return recSlot(actions, MAX_ACTIONS, id); }
int     rulesActionFreeSlot()       { return recFreeSlot(actions, MAX_ACTIONS); }
uint8_t rulesActionAllocId()        { return recAllocId(actions, MAX_ACTIONS); }
uint8_t rulesActionCount()          { return recCount(actions, MAX_ACTIONS); }

bool rulesActionSetManual(uint8_t id, bool on) {
    int slot = rulesActionSlot(id);
    if (slot < 0) return false;
    g_actionManual[slot] = on;
    return true;
}

ActionOverride rulesActionOverride(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_actionOverride[slot] : ACT_OVR_AUTO;
}

bool rulesActionSetOverride(uint8_t id, ActionOverride ovr) {
    int slot = rulesActionSlot(id);
    if (slot < 0) return false;
    g_actionOverride[slot] = ovr;
    return true;
}

bool rulesActionReleaseDrive(uint8_t id) {
    int slot = rulesActionSlot(id);
    if (slot < 0) return false;
    // An action whose *driver* changes starts de-asserted — one invariant covering
    // both the operator override and the manual latch. Clearing only the override
    // would leave the #294 bug intact by another route: arm the latch while the
    // action is unlinked, link it to a rule (latch goes dormant, not cleared), then
    // unlink or delete that rule — and rulesActionDesired() falls straight back
    // through to the stale latch and snaps the pin ON with no operator command.
    const bool wasDriving = (g_actionOverride[slot] != ACT_OVR_AUTO) || g_actionManual[slot];
    g_actionOverride[slot] = ACT_OVR_AUTO;
    g_actionManual[slot]   = false;
    return wasDriving;
}

bool rulesActionCommand(uint8_t id, bool on) {
    int slot = rulesActionSlot(id);
    if (slot < 0) return false;
    // One router so the CLI and the BLE write can never diverge: an unlinked
    // action's ON/OFF is the manual latch; a rule-driven one's is an override.
    if (rulesActionRuleCount(slot) == 0) {
        g_actionManual[slot] = on;
    } else {
        g_actionOverride[slot] = on ? ACT_OVR_FORCE_ON : ACT_OVR_FORCE_OFF;
    }
    return true;
}

bool rulesActionSetName(uint8_t id, const char* name) {
    int slot = rulesActionSlot(id);
    if (slot < 0) return false;
    char* dst = actions[slot].name;
    strncpy(dst, name ? name : "", sizeof(actions[slot].name) - 1);
    dst[sizeof(actions[slot].name) - 1] = '\0';
    return true;
}

#endif // INCLUDE_RULES_ENGINE
