/*
 * cmd_action.cpp - ACTION command (control engine: actuators driven by Rules)
 *
 * Manages the persisted Action table (systemConfig.actions[]). Actions are
 * managed by stable id — no names (§11.8). v1 implements ACT_RELAY (a generic
 * GPIO actuator, the relay successor); other types are reserved.
 *
 *   ACTION ADD RELAY <target> [FAILSAFE ON|OFF] [INVERT]
 *   ACTION <id> RULE <ruleId|NONE>     link to a Rule (or unlink → manual)
 *   ACTION <id> ON|OFF                 manual override (manual-only actions)
 *   ACTION LIST | ACTION <id> | ACTION DEL <id>
 */

#include "../config.h"
#include "../control/control_types.h"  // defines ENABLE_CONTROL_ENGINE before the guard

#if ENABLE_CONTROL_ENGINE

#include "command_handlers.h"
#include "command_helpers.h"
#include "input_manager.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/message_api.h"
#include "../control/control_manager.h"

// CLI handlers are cold — keep them in flash, not Teensy ITCM (RAM1).
#ifndef FLASHMEM
#define FLASHMEM
#endif

static FLASHMEM const char* actionTypeName(uint8_t t) {
    switch (t) {
        case ACT_RELAY:   return "RELAY";
        case ACT_BEEPER:  return "BEEPER";
        case ACT_LED:     return "LED";
        case ACT_DISPLAY: return "DISPLAY";
        default:          return "?";
    }
}

static FLASHMEM void printActionRow(int i) {
    const Action& a = systemConfig.actions[i];
    msg.control.print(F("  #"));
    msg.control.print(a.id);
    msg.control.print(F("  "));
    msg.control.print(actionTypeName(a.type));
    msg.control.print(F(" pin"));
    msg.control.print(a.target);
    if (a.behavior & ACT_BEHAVIOR_INVERT) msg.control.print(F(" INV"));
    msg.control.print(F("  src="));
    if (a.ruleRef == CONTROL_ID_NONE) {
        msg.control.print(F("manual"));
    } else {
        msg.control.print(F("rule#"));
        msg.control.print(a.ruleRef);
    }
    if (a.type == ACT_RELAY) {
        msg.control.print(F("  failsafe="));
        msg.control.print(a.failsafe == ACT_FAILSAFE_ON ? F("ON") : F("OFF"));
    }
    msg.control.print(F("  -> "));
    msg.control.println(controlActionState(i) ? F("ON") : F("OFF"));
}

static FLASHMEM void printAllActions() {
    uint8_t n = controlActionCount();
    msg.control.print(F("Actions ("));
    msg.control.print(n);
    msg.control.print('/');
    msg.control.print(MAX_ACTIONS);
    msg.control.println(F("):"));
    if (n == 0) {
        msg.control.println(F("  (none configured)"));
        return;
    }
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (systemConfig.actions[i].id != CONTROL_ID_NONE) printActionRow(i);
    }
}

static FLASHMEM int actionAdd(int argc, const char* const* argv) {
    // ACTION ADD RELAY <target> [FAILSAFE ON|OFF] [INVERT]
    if (argc < 4) {
        msg.control.println(F("ERROR: usage: ACTION ADD RELAY <target> [FAILSAFE ON|OFF] [INVERT]"));
        return 1;
    }
    if (!streq_P(argv[2], PSTR("RELAY"))) {
        msg.control.println(F("ERROR: v1 supports RELAY only (BEEPER/LED/DISPLAY reserved)"));
        return 1;
    }

    bool validPin = false;
    uint8_t target = parsePin(argv[3], &validPin);
    if (!validPin) {
        msg.control.println(F("ERROR: invalid target pin"));
        return 1;
    }

    uint8_t behavior = 0;
    uint8_t failsafe = ACT_FAILSAFE_OFF;
    for (int i = 4; i < argc; i++) {
        if (streq_P(argv[i], PSTR("FAILSAFE")) && (i + 1) < argc) {
            i++;
            if (streq_P(argv[i], PSTR("ON")))       failsafe = ACT_FAILSAFE_ON;
            else if (streq_P(argv[i], PSTR("OFF")))  failsafe = ACT_FAILSAFE_OFF;
            else { msg.control.println(F("ERROR: FAILSAFE requires ON or OFF")); return 1; }
        } else if (streq_P(argv[i], PSTR("INVERT"))) {
            behavior |= ACT_BEHAVIOR_INVERT;
        } else {
            msg.control.print(F("ERROR: unexpected token: "));
            msg.control.println(argv[i]);
            return 1;
        }
    }

    int slot = controlActionFreeSlot();
    uint8_t id = controlActionAllocId();
    if (slot < 0 || id == CONTROL_ID_NONE) {
        msg.control.print(F("ERROR: action table full ("));
        msg.control.print(MAX_ACTIONS);
        msg.control.println(F(")"));
        return 1;
    }

    Action& a = systemConfig.actions[slot];
    a.id = id;
    a.type = ACT_RELAY;
    a.target = target;
    a.ruleRef = CONTROL_ID_NONE;   // manual-only until linked
    a.behavior = behavior;
    a.failsafe = failsafe;
    controlActionConfigurePin(slot);   // pinMode OUTPUT + de-assert

    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.println(F(" added (manual). Link with ACTION <id> RULE <ruleId>. SAVE to persist."));
    return 0;
}

static FLASHMEM int actionLink(uint8_t id, const char* ruleArg) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    if (streq_P(ruleArg, PSTR("NONE"))) {
        systemConfig.actions[slot].ruleRef = CONTROL_ID_NONE;
        msg.control.print(F("Action #"));
        msg.control.print(id);
        msg.control.println(F(" unlinked (manual). SAVE to persist."));
        return 0;
    }
    uint8_t ruleId = (uint8_t)atoi(ruleArg);
    if (controlRuleSlot(ruleId) < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(ruleId);
        return 1;
    }
    systemConfig.actions[slot].ruleRef = ruleId;
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.print(F(" linked to rule #"));
    msg.control.print(ruleId);
    msg.control.println(F(". SAVE to persist."));
    return 0;
}

static FLASHMEM int actionManual(uint8_t id, bool on) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    controlActionSetManual(id, on);
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.print(F(" manual "));
    msg.control.println(on ? F("ON") : F("OFF"));
    if (systemConfig.actions[slot].ruleRef != CONTROL_ID_NONE) {
        msg.control.println(F("  (note: action is rule-driven; ON/OFF applies only when manual — ACTION <id> RULE NONE to unlink)"));
    }
    return 0;
}

static FLASHMEM int actionDel(uint8_t id) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    controlActionConfigurePin(slot);  // de-assert the pin before freeing
    systemConfig.actions[slot].id = CONTROL_ID_NONE;
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.println(F(" deleted. SAVE to persist."));
    return 0;
}

FLASHMEM int cmd_action(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: ACTION requires a subcommand"));
        msg.control.println(F("  Usage: ACTION LIST | ADD RELAY <target> ... | <id> RULE <ruleId> | <id> ON|OFF | DEL <id>"));
        return 1;
    }

    if (streq_P(argv[1], PSTR("LIST"))) {
        printAllActions();
        return 0;
    }

    const bool structural = streq_P(argv[1], PSTR("ADD")) || streq_P(argv[1], PSTR("DEL"));
    if (structural && isInRunMode()) {
        msg.control.print(F("ERROR: ACTION "));
        msg.control.print(argv[1]);
        msg.control.println(F(" is config-only; enter CONFIG mode first"));
        return 1;
    }

    if (streq_P(argv[1], PSTR("ADD"))) return actionAdd(argc, argv);
    if (streq_P(argv[1], PSTR("DEL"))) {
        if (argc < 3) { msg.control.println(F("ERROR: DEL requires an action id")); return 1; }
        return actionDel((uint8_t)atoi(argv[2]));
    }

    // ACTION <id> <verb> ...
    int slot = controlActionSlot((uint8_t)atoi(argv[1]));
    if (slot < 0) {
        msg.control.print(F("ERROR: unknown action id or subcommand: "));
        msg.control.println(argv[1]);
        return 1;
    }
    uint8_t id = (uint8_t)atoi(argv[1]);

    if (argc < 3) {           // ACTION <id> — detail
        printActionRow(slot);
        return 0;
    }

    if (streq_P(argv[2], PSTR("RULE"))) {       // link/unlink — config-only
        if (isInRunMode()) {
            msg.control.println(F("ERROR: ACTION <id> RULE is config-only; enter CONFIG mode first"));
            return 1;
        }
        if (argc < 4) { msg.control.println(F("ERROR: usage: ACTION <id> RULE <ruleId|NONE>")); return 1; }
        return actionLink(id, argv[3]);
    }
    if (streq_P(argv[2], PSTR("ON")))  return actionManual(id, true);   // RUN-allowed
    if (streq_P(argv[2], PSTR("OFF"))) return actionManual(id, false);  // RUN-allowed

    msg.control.print(F("ERROR: unknown ACTION verb: "));
    msg.control.println(argv[2]);
    return 1;
}

#endif // ENABLE_CONTROL_ENGINE
