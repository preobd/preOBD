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
#include "../control/control_types.h"  // defines ENABLE_CONTROL_ENGINE before the guard

#if ENABLE_CONTROL_ENGINE

#include <math.h>  // fabsf — hysteresis sanity check

#include "command_handlers.h"
#include "command_helpers.h"
#include "input_manager.h"
#include "input.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/message_api.h"
#include "../control/control_manager.h"
#include "../control/control_engine.h"

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
    // source input: prefer the input's abbreviation, fall back to the pin
    if (in && in->abbrName[0]) {
        msg.control.print(in->abbrName);
    } else {
        msg.control.print(F("pin"));
        msg.control.print(r.sourceInput);
    }
    msg.control.print(F("  "));
    msg.control.print(conditionName(r.condition));
    msg.control.print(' ');
    msg.control.print(r.limitLo);
    if (r.condition == RULE_OUTSIDE_BAND || r.condition == RULE_INSIDE_BAND) {
        msg.control.print(F(".."));
        msg.control.print(r.limitHi);
    }
    if (r.hysteresis != 0.0f) {
        msg.control.print(F(" hyst "));
        msg.control.print(r.hysteresis);
    }
    msg.control.print(F("  ["));
    msg.control.print(severityName(r.severity));
    msg.control.print(F("]  "));
    msg.control.print(ruleStateName(g_ruleContexts[i].state));
    // live value for context
    if (in) {
        msg.control.print(F("  val="));
        msg.control.print(in->value);
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
        if (a.id == CONTROL_ID_NONE || a.ruleRef != ruleId) continue;
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
        msg.control.print(F("  -> "));
        msg.control.println(controlActionState(i) ? F("ON") : F("OFF"));
    }
    if (!any) {
        msg.control.println(F("  Linked actions: (none)"));
    }
}

static FLASHMEM void printAllRules() {
    uint8_t n = controlRuleCount();
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
        if (rules[i].id != CONTROL_ID_NONE) printRuleRow(i);
    }
    // Annunciator rollup over all rules — this is what drives the buzzer/LED.
    msg.control.print(F("System severity (rules): "));
    msg.control.println(severityName(controlSystemSeverity()));
}

// --- RULE ADD -----------------------------------------------------------

static FLASHMEM int ruleAdd(int argc, const char* const* argv) {
    // RULE ADD <input> ABOVE|BELOW <limit> [HYST <w>] [WARNING|ALARM]
    // RULE ADD <input> OUTSIDE <lo> <hi>   [HYST <w>] [WARNING|ALARM]
    if (argc < 5) {
        msg.control.println(F("ERROR: usage: RULE ADD <input> ABOVE|BELOW <limit> [HYST <w>] [WARNING|ALARM]"));
        msg.control.println(F("              RULE ADD <input> OUTSIDE <lo> <hi> [HYST <w>] [WARNING|ALARM]"));
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

    int slot = controlRuleFreeSlot();
    uint8_t id = controlRuleAllocId();
    if (slot < 0 || id == CONTROL_ID_NONE) {
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

    controlRuleInit(g_ruleContexts[slot], r, millis());  // create-from-... : arm

    msg.control.print(F("Rule #"));
    msg.control.print(id);
    msg.control.println(F(" added. SAVE to persist."));
    return 0;
}

// --- RULE DEL -----------------------------------------------------------

static FLASHMEM int ruleDel(uint8_t id) {
    int slot = controlRuleSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(id);
        return 1;
    }
    rules[slot].id = CONTROL_ID_NONE;
    g_ruleContexts[slot].state = RULE_DISABLED;
    controlClearActionsForRule(id);  // referential integrity: referring actions → manual-only
    msg.control.print(F("Rule #"));
    msg.control.print(id);
    msg.control.println(F(" deleted. SAVE to persist."));
    return 0;
}

// --- dispatch -----------------------------------------------------------

FLASHMEM int cmd_rule(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: RULE requires a subcommand"));
        msg.control.println(F("  Usage: RULE LIST | ADD ... | <id> | DEL <id>"));
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
        if (controlRuleReset(id, millis())) {
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
    int slot = controlRuleSlot(ruleId);
    if (slot < 0) {
        msg.control.print(F("ERROR: unknown rule id or subcommand: "));
        msg.control.println(argv[1]);
        return 1;
    }
    printRuleRow(slot);
    printLinkedActions(ruleId);
    return 0;
}

#endif // ENABLE_CONTROL_ENGINE
