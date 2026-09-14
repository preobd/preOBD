/*
 * cmd_rule.cpp - RULE command (control engine: create / list / inspect / delete)
 *
 * Manages the persisted Rule table (rules[]) and its runtime
 * contexts. Rules are managed by stable id — no names (ARCHITECTURE §11.8);
 * RULE LIST renders ground truth (source input + condition + severity + live
 * state) so the id is the only handle needed.
 *
 * v1 subset: ADD / LIST / <id> / DEL. RESET (re-copy the application default
 * template, §11.5) lands with the template machinery. Actions, the annunciator,
 * and the RELAY facade are separate commits.
 */

#include "../config.h"
#include "../rules/rules_types.h"  // defines INCLUDE_RULES_ENGINE before the guard

#if INCLUDE_RULES_ENGINE

#include <math.h>  // fabsf, isnan — hysteresis sanity check + faulted-value display

#include "command_handlers.h"
#include "command_helpers.h"
#include "../inputs/input_manager.h"
#include "../inputs/input.h"
#include "../core/system_mode.h"
#include "../storage/system_config.h"
#include "../core/message_api.h"
#include "../sensors/units_registry.h"    // getUnitStringByIndex
#include "../sensors/sensor_library.h"    // convertFromBaseUnits
#include "../rules/rules_manager.h"
#include "../rules/rules_engine.h"

// CLI handlers are cold — keep them in flash, not Teensy ITCM (RAM1).
// FLASHMEM is a Teensy core macro — no-op elsewhere.
#ifndef FLASHMEM
#define FLASHMEM
#endif

// --- display helpers ----------------------------------------------------

static FLASHMEM const char* conditionName(uint8_t c) {
    switch (c) {
        case RULE_ABOVE:        return "ABOVE";
        case RULE_BELOW:        return "BELOW";
        case RULE_OUTSIDE_BAND: return "OUTSIDE";
        case RULE_INSIDE_BAND:  return "INSIDE";
        default:                return "?";
    }
}

static FLASHMEM const char* severityName(uint8_t s) {
    switch (s) {
        case RULE_SEV_NONE:    return "--";
        case RULE_SEV_INFO:    return "INFO";
        case RULE_SEV_WARNING: return "WARNING";
        case RULE_SEV_ALARM:   return "ALARM";
        default:               return "?";
    }
}

static FLASHMEM const char* ruleStateName(uint8_t s) {
    switch (s) {
        case RULE_DISABLED: return "DISABLED";
        case RULE_INIT:     return "INIT";
        case RULE_WARMUP:   return "WARMUP";
        case RULE_READY:    return "READY";
        case RULE_ACTIVE:   return "ACTIVE";
        default:            return "?";
    }
}

// One detail line for the rule in slot `i`.
static FLASHMEM void printRuleRow(int i) {
    const Rule& r = rules[i];
    Input* in = getInputByPin(r.sourceInput);

    msg.control.print(F("  #"));
    msg.control.print(r.id);
    msg.control.print(F("  "));
    // source input: prefer the input's abbreviation, fall back to the pin.
    // No input at all on that pin is an ORPHAN (e.g. left behind by a pre-#345
    // CLEAR) — say so, or the row reads like an ordinary rule that just hasn't
    // warmed up yet. An orphan can never assert and never forces a failsafe.
    if (in && in->abbrName[0]) {
        msg.control.print(in->abbrName);
    } else {
        msg.control.print(F("pin"));
        msg.control.print(r.sourceInput);
        if (!in) msg.control.print(F(" (no input)"));
    }
    msg.control.print(F("  "));
    msg.control.print(conditionName(r.condition));
    msg.control.print(' ');
    // r.limitLo/limitHi/hysteresis persist in base units (like in->value); convert
    // to the source input's display units so this row reads consistently with
    // LIST INPUTS / INFO instead of showing an unlabeled base-unit number (#324).
    // hysteresis is a delta, not an absolute reading, so it only scales — adding
    // the display-unit offset (e.g. °F's +32) would be wrong for a width.
    if (in) {
        const uint8_t unitsIdx = in->unitsIndex;
        msg.control.print(convertFromBaseUnits(r.limitLo, unitsIdx));
        if (r.condition == RULE_OUTSIDE_BAND || r.condition == RULE_INSIDE_BAND) {
            msg.control.print(F(".."));
            msg.control.print(convertFromBaseUnits(r.limitHi, unitsIdx));
        }
        msg.control.print(' ');
        msg.control.print((__FlashStringHelper*)getUnitStringByIndex(unitsIdx));
        if (r.hysteresis != 0.0f) {
            msg.control.print(F(" hyst "));
            msg.control.print(convertFromBaseUnits(r.hysteresis, unitsIdx) - convertFromBaseUnits(0.0f, unitsIdx));
        }
    } else {
        msg.control.print(r.limitLo);
        if (r.condition == RULE_OUTSIDE_BAND || r.condition == RULE_INSIDE_BAND) {
            msg.control.print(F(".."));
            msg.control.print(r.limitHi);
        }
        if (r.hysteresis != 0.0f) {
            msg.control.print(F(" hyst "));
            msg.control.print(r.hysteresis);
        }
    }
    msg.control.print(F("  ["));
    msg.control.print(severityName(r.severity));
    msg.control.print(F("]  "));
    // Distinguish "parked by the operator" (flag cleared — RULE <id> ENABLE
    // re-arms it) from the transient unarmed context state, which also renders
    // as RULE_DISABLED (#346).
    if (!(r.flags & RULE_FLAG_ENABLED)) {
        msg.control.print(F("PARKED"));
    } else {
        msg.control.print(ruleStateName(g_ruleContexts[i].state));
    }
    // live value for context
    if (in) {
        msg.control.print(F("  val="));
        if (isnan(in->value)) {
            msg.control.print(F("ERROR"));
        } else {
            msg.control.print(convertFromBaseUnits(in->value, in->unitsIndex));
        }
    }
    msg.control.println();
}

static FLASHMEM const char* actionTypeName(uint8_t t) {
    switch (t) {
        case ACT_RELAY:   return "RELAY";
        case ACT_BEEPER:  return "BEEPER";
        case ACT_LED:     return "LED";
        case ACT_DISPLAY: return "DISPLAY";
        default:          return "?";
    }
}

// Reverse lookup: which Action(s) (if any) are linked to rule id `ruleId`.
static FLASHMEM void printLinkedActions(uint8_t ruleId) {
    bool any = false;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        const Action& a = actions[i];
        // Membership, not just the inline first member (#343) — a rule that is
        // one of several driving an action must still list it here.
        if (a.id == RULES_ID_NONE || !rulesActionHasRule(i, ruleId)) continue;
        if (!any) {
            msg.control.println(F("  Linked actions:"));
            any = true;
        }
        msg.control.print(F("    #"));
        msg.control.print(a.id);
        msg.control.print(F("  "));
        msg.control.print(actionTypeName(a.type));
        msg.control.print(F(" pin"));
        msg.control.print(a.target);
        // Say when this rule is only one of the things that can fire the action,
        // so deleting the rule doesn't look like it will free the actuator.
        const uint8_t members = rulesActionRuleCount(i);
        if (members > 1) {
            msg.control.print(F("  (1 of "));
            msg.control.print(members);
            msg.control.print(F(" rules)"));
        }
        msg.control.print(F("  -> "));
        msg.control.println(rulesActionState(i) ? F("ON") : F("OFF"));
    }
    if (!any) {
        msg.control.println(F("  Linked actions: (none)"));
    }
}

static FLASHMEM void printAllRules() {
    uint8_t n = rulesRuleCount();
    msg.control.print(F("Rules ("));
    msg.control.print(n);
    msg.control.print('/');
    msg.control.print(MAX_RULES);
    msg.control.println(F("):"));
    if (n == 0) {
        msg.control.println(F("  (none configured)"));
        return;
    }
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].id != RULES_ID_NONE) printRuleRow(i);
    }
    // Annunciator rollup over all rules — this is what drives the buzzer/LED.
    msg.control.print(F("System severity (rules): "));
    msg.control.println(severityName(rulesSystemSeverity()));
}

// --- RULE ADD -----------------------------------------------------------

static FLASHMEM int ruleAdd(int argc, const char* const* argv) {
    // RULE ADD <input> ABOVE|BELOW <limit> [HYST <w>] [INFO|WARNING|ALARM|NONE]
    // RULE ADD <input> OUTSIDE <lo> <hi>   [HYST <w>] [INFO|WARNING|ALARM|NONE]
    if (argc < 5) {
        msg.control.println(F("ERROR: usage: RULE ADD <input> ABOVE|BELOW <limit> [HYST <w>] [INFO|WARNING|ALARM|NONE]"));
        msg.control.println(F("              RULE ADD <input> OUTSIDE <lo> <hi> [HYST <w>] [INFO|WARNING|ALARM|NONE]"));
        return 1;
    }

    bool validPin = false;
    uint8_t pin = parsePin(argv[2], &validPin);
    if (!validPin) {
        msg.control.println(F("ERROR: invalid input pin"));
        return 1;
    }
    if (getInputByPin(pin) == nullptr) {
        msg.control.println(F("ERROR: no input configured on that pin (SET it first)"));
        return 1;
    }

    uint8_t condition;
    float limitLo, limitHi = 0.0f;
    int optStart;  // first index to scan for optional tokens

    if (streq_P(argv[3], PSTR("ABOVE")) || streq_P(argv[3], PSTR("BELOW"))) {
        condition = streq_P(argv[3], PSTR("ABOVE")) ? RULE_ABOVE : RULE_BELOW;
        limitLo = atof(argv[4]);
        optStart = 5;
    } else if (streq_P(argv[3], PSTR("OUTSIDE"))) {
        if (argc < 6) {
            msg.control.println(F("ERROR: OUTSIDE requires <lo> <hi>"));
            return 1;
        }
        condition = RULE_OUTSIDE_BAND;
        limitLo = atof(argv[4]);
        limitHi = atof(argv[5]);
        if (limitLo >= limitHi) {
            msg.control.println(F("ERROR: OUTSIDE requires lo < hi"));
            return 1;
        }
        optStart = 6;
    } else {
        msg.control.println(F("ERROR: condition must be ABOVE, BELOW, or OUTSIDE"));
        return 1;
    }

    // Optional tokens: HYST <w>, and a severity keyword (default ALARM).
    float hysteresis = 0.0f;
    uint8_t severity = RULE_SEV_ALARM;
    for (int i = optStart; i < argc; i++) {
        if (streq_P(argv[i], PSTR("HYST")) && (i + 1) < argc) {
            hysteresis = atof(argv[++i]);
        } else if (streq_P(argv[i], PSTR("INFO"))) {
            severity = RULE_SEV_INFO;
        } else if (streq_P(argv[i], PSTR("WARNING"))) {
            severity = RULE_SEV_WARNING;
        } else if (streq_P(argv[i], PSTR("ALARM"))) {
            severity = RULE_SEV_ALARM;
        } else if (streq_P(argv[i], PSTR("NONE"))) {
            severity = RULE_SEV_NONE;
        } else {
            msg.control.print(F("ERROR: unexpected token: "));
            msg.control.println(argv[i]);
            return 1;
        }
    }

    // Reject a hysteresis so wide it inverts the clear thresholds — the rule
    // would never de-assert and the alarm/relay would latch ON forever. (The
    // legacy setRelayThresholds() carried the equivalent guard.)
    if (hysteresis < 0.0f) {
        msg.control.println(F("ERROR: HYST must be >= 0"));
        return 1;
    }
    if (condition == RULE_OUTSIDE_BAND) {
        // Each edge insets inward by HYST; if 2*HYST >= band width the insets
        // cross and the clear test (value < lo+hyst || value > hi-hyst) is always true.
        if (hysteresis * 2.0f >= (limitHi - limitLo)) {
            msg.control.println(F("ERROR: HYST too large; must be < (hi-lo)/2 or the rule never clears"));
            return 1;
        }
    } else {  // ABOVE / BELOW — clear point is limit -/+ HYST; keep it on the live side of 0.
        if (hysteresis >= fabsf(limitLo)) {
            msg.control.println(F("ERROR: HYST too large; must be < |limit| or the rule never clears"));
            return 1;
        }
    }

    int slot = rulesRuleFreeSlot();
    uint8_t id = rulesRuleAllocId();
    if (slot < 0 || id == RULES_ID_NONE) {
        msg.control.print(F("ERROR: rule table full ("));
        msg.control.print(MAX_RULES);
        msg.control.println(F(")"));
        return 1;
    }

    Rule& r = rules[slot];
    r.id = id;
    r.sourceInput = pin;
    r.condition = condition;
    r.severity = severity;
    r.flags = RULE_FLAG_ENABLED;
    r.limitLo = limitLo;
    r.limitHi = limitHi;
    r.hysteresis = hysteresis;
    r.warmupMs = RULE_DEFAULT_WARMUP_MS;
    r.debounceMs = RULE_DEFAULT_DEBOUNCE_MS;

    rulesRuleInit(g_ruleContexts[slot], r, millis());  // create-from-... : arm

    msg.control.print(F("Rule #"));
    msg.control.print(id);
    msg.control.println(F(" added. SAVE to persist."));
    return 0;
}

// --- RULE DEL -----------------------------------------------------------

static FLASHMEM int ruleDel(uint8_t id) {
    int slot = rulesRuleSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(id);
        return 1;
    }
    rules[slot].id = RULES_ID_NONE;
    g_ruleContexts[slot].state = RULE_DISABLED;
    // referential integrity: referring actions → manual-only
    const uint8_t released = rulesClearActionsForRule(id);
    msg.control.print(F("Rule #"));
    msg.control.print(id);
    msg.control.println(F(" deleted. SAVE to persist."));
    // Never de-energize a relay an operator was deliberately holding without
    // saying so — deleting a rule is not obviously an action command (#329).
    if (released) {
        msg.control.print(F("  NOTE: released "));
        msg.control.print(released);
        msg.control.println(F(" driven action(s) that followed this rule; they are now OFF"));
    }
    return 0;
}

// --- RULE <id> ENABLE|DISABLE (#346) ------------------------------------

// Park / re-arm a Rule without deleting it. Idempotent on purpose: re-enabling an
// already-armed Rule must NOT re-init it, or the operator drags a live, warmed
// rule back through INIT->WARMUP and silences it (and any Action it drives) for
// warmupMs — the #254 lesson, same as the SET ALARM in-place edit.
static FLASHMEM int ruleSetEnabled(uint8_t id, bool enable) {
    int slot = rulesRuleSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(id);
        return 1;
    }
    const bool released = rulesRuleSetEnabled(id, enable, millis());
    msg.control.print(F("Rule #"));
    msg.control.print(id);
    if (!released) {
        msg.control.println(enable ? F(" already enabled") : F(" already disabled"));
        return 0;
    }
    msg.control.println(enable ? F(" enabled (re-armed). SAVE to persist.")
                               : F(" disabled (parked; it can no longer assert). SAVE to persist."));
    printLinkedActions(id);
    return 0;
}

// --- INFO <pin> ALARM (#338) --------------------------------------------

// The per-input alarm view. Renders the input's canonical alarm Rule — the same
// record `SET <pin> ALARM` writes — instead of the retired legacy Input alarm
// fields, which the control engine stopped evaluating and which therefore always
// read "Enabled: NO / Min 0 / Max 0" even with a live, firing alarm (#338).
FLASHMEM void printInputAlarmInfo(uint8_t pin) {
    char label[12];
    formatPinLabel(pin, label, sizeof(label));

    if (getInputByPin(pin) == nullptr) {
        msg.control.print(F("ERROR: Input for pin "));
        msg.control.print(label);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Alarm Info ["));
    msg.control.print(label);
    msg.control.println(F("] ====="));

    const int slot = rulesAlarmRuleSlotForInput(pin);
    if (slot < 0) {
        msg.control.println(F("  No alarm Rule on this input."));
        msg.control.println(F("  Set one with: SET <pin> ALARM <min> <max> | ALARM MIN <v> | ALARM MAX <v>"));
        msg.control.println();
        return;
    }

    const Rule& r = rules[slot];
    printRuleRow(slot);
    msg.control.print(F("  Warmup: "));
    msg.control.print(r.warmupMs);
    msg.control.print(F(" ms   Debounce: "));
    msg.control.print(r.debounceMs);
    msg.control.print(F(" ms   Time in state: "));
    msg.control.print(millis() - g_ruleContexts[slot].stateEntryTime);
    msg.control.println(F(" ms"));
    printLinkedActions(r.id);
    // Other Rules can watch the same input (warnings, control-only fan rules);
    // this view only ever shows the canonical alarm one, so point at the rest.
    msg.control.println(F("  (all rules on this input: RULE LIST)"));
    msg.control.println();
}

// --- dispatch -----------------------------------------------------------

FLASHMEM int cmd_rule(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: RULE requires a subcommand"));
        msg.control.println(F("  Usage: RULE LIST | ADD ... | <id> | <id> ENABLE|DISABLE | DEL <id> | RESET <id>"));
        return 1;
    }

    // RUN-allowed: LIST and <id> status reads. Structural edits are CONFIG-only.
    if (streq_P(argv[1], PSTR("LIST"))) {
        printAllRules();
        return 0;
    }

    const bool structural = streq_P(argv[1], PSTR("ADD")) ||
                            streq_P(argv[1], PSTR("DEL")) ||
                            streq_P(argv[1], PSTR("RESET"));
    if (structural && isInRunMode()) {
        msg.control.print(F("ERROR: RULE "));
        msg.control.print(argv[1]);
        msg.control.println(F(" is config-only; enter CONFIG mode first"));
        return 1;
    }

    if (streq_P(argv[1], PSTR("ADD"))) {
        return ruleAdd(argc, argv);
    }
    if (streq_P(argv[1], PSTR("DEL"))) {
        if (argc < 3) {
            msg.control.println(F("ERROR: DEL requires a rule id"));
            return 1;
        }
        return ruleDel((uint8_t)atoi(argv[2]));
    }
    if (streq_P(argv[1], PSTR("RESET"))) {
        if (argc < 3) {
            msg.control.println(F("ERROR: RESET requires a rule id"));
            return 1;
        }
        uint8_t id = (uint8_t)atoi(argv[2]);
        if (rulesRuleReset(id, millis())) {
            msg.control.print(F("Rule #"));
            msg.control.print(id);
            msg.control.println(F(" reset to application defaults (re-armed). SAVE to persist."));
            return 0;
        }
        msg.control.print(F("ERROR: cannot reset rule #"));
        msg.control.print(id);
        msg.control.println(F(" (unknown, or its input has no application default)"));
        return 1;
    }

    // RULE <id> — show one rule's detail.
    uint8_t ruleId = (uint8_t)atoi(argv[1]);
    int slot = rulesRuleSlot(ruleId);
    if (slot < 0) {
        msg.control.print(F("ERROR: unknown rule id or subcommand: "));
        msg.control.println(argv[1]);
        return 1;
    }
    if (argc >= 3) {
        const bool enable  = streq_P(argv[2], PSTR("ENABLE"));
        const bool disable = streq_P(argv[2], PSTR("DISABLE"));
        if (enable || disable) {
            // Structural, like ADD/DEL/RESET: parking a rule changes what the
            // engine can assert, so it belongs in CONFIG (#346).
            if (isInRunMode()) {
                msg.control.print(F("ERROR: RULE <id> "));
                msg.control.print(argv[2]);
                msg.control.println(F(" is config-only; enter CONFIG mode first"));
                return 1;
            }
            return ruleSetEnabled(ruleId, enable);
        }
        msg.control.print(F("ERROR: unknown RULE verb: "));
        msg.control.println(argv[2]);
        return 1;
    }

    printRuleRow(slot);
    printLinkedActions(ruleId);
    return 0;
}

#endif // INCLUDE_RULES_ENGINE
