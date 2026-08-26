/*
 * cmd_action.cpp - ACTION command (control engine: actuators driven by Rules)
 *
 * Manages the persisted Action table (actions[]). Actions are managed by stable
 * id, with an optional operator label (§11.8). v1 implements ACT_RELAY (a generic
 * GPIO actuator, the relay successor); other types are reserved.
 *
 *   ACTION ADD RELAY <target> [FAILSAFE ON|OFF] [INVERT]
 *   ACTION <id> RULE <ruleId|NONE>     membership := exactly that Rule (or none)
 *   ACTION <id> RULE ADD <ruleId>      add a member Rule (OR-driven, #343)
 *   ACTION <id> RULE REMOVE <ruleId>   drop one member Rule
 *   ACTION <id> NAME <text>            set operator label (e.g. "Cooling Fan")
 *   ACTION <id> ON|OFF                 drive by hand: manual latch on an unlinked
 *                                      action, operator override on a rule-driven
 *                                      one (outranks the rule AND the failsafe)
 *   ACTION <id> AUTO                   release an operator override
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
#include "../lib/pin_registry.h"
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
    const Action& a = actions[i];
    msg.control.print(F("  #"));
    msg.control.print(a.id);
    if (a.name[0] != '\0') {
        msg.control.print(F(" \""));
        msg.control.print(a.name);
        msg.control.print('"');
    }
    msg.control.print(F("  "));
    msg.control.print(actionTypeName(a.type));
    msg.control.print(F(" pin"));
    msg.control.print(a.target);
    if (a.behavior & ACT_BEHAVIOR_INVERT) msg.control.print(F(" INV"));
    msg.control.print(F("  src="));
    const uint8_t members = controlActionRuleCount(i);
    if (members == 0) {
        msg.control.print(F("manual"));
    } else {
        // Every member, comma-separated ("rule#2,#5" reads as the OR it is) — the
        // operator must be able to see at a glance everything that can fire this
        // actuator, not just the first one (#343).
        msg.control.print(F("rule#"));
        for (uint8_t m = 0; m < members; m++) {
            if (m > 0) { msg.control.print(F(",#")); }
            msg.control.print(controlActionRuleAt(i, m));
        }
        if (members > 1) msg.control.print(F(" (any)"));
        ActionOverride ovr = controlActionOverride(i);
        if (ovr != ACT_OVR_AUTO) {
            msg.control.print(F(" (OVERRIDE "));
            msg.control.print(ovr == ACT_OVR_FORCE_ON ? F("ON") : F("OFF"));
            // Never let a force held against a dead sensor look ordinary (#329).
            if (controlActionSourceFaulted(i)) msg.control.print(F(" - source faulted"));
            msg.control.print(')');
        }
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
        if (actions[i].id != CONTROL_ID_NONE) printActionRow(i);
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

    // Pin-conflict check (mirrors input/serial config). The registry catches
    // buses, system pins, RGB output, and other actions; inputs aren't in the
    // registry (they self-track), so check inputs[] explicitly.
    if (!isPinAvailable(target)) {
        msg.control.print(F("ERROR: pin "));
        msg.control.print(target);
        msg.control.print(F(" already in use ("));
        msg.control.print(getPinUsageTypeName(getPinUsage(target)));
        const char* held = getPinDescription(target);
        if (held) { msg.control.print(F(": ")); msg.control.print(held); }
        msg.control.println(F(")"));
        return 1;
    }
    if (getInputByPin(target) != nullptr) {
        msg.control.print(F("ERROR: pin "));
        msg.control.print(target);
        msg.control.println(F(" is already used by a configured input"));
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

    Action& a = actions[slot];
    a.id = id;
    a.type = ACT_RELAY;
    a.target = target;
    a.ruleRef = CONTROL_ID_NONE;   // manual-only until linked
    controlActionForgetLinks(id);  // ids are reusable — never inherit a dead action's members
    a.behavior = behavior;
    a.failsafe = failsafe;
    memset(a.name, 0, sizeof(a.name));  // unnamed; a reused slot may hold a stale label
    controlActionConfigurePin(slot);   // pinMode OUTPUT + de-assert
    controlSyncActionPins();           // claim the target pin in the registry

    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.println(F(" added (manual). Link with ACTION <id> RULE <ruleId>. SAVE to persist."));
    return 0;
}

// Report the membership left behind by a link change, so the operator sees the
// resulting OR set rather than just the record they touched.
static FLASHMEM void printMembership(int slot) {
    const uint8_t n = controlActionRuleCount(slot);
    msg.control.print(F("  now driven by "));
    if (n == 0) {
        msg.control.println(F("nothing (manual)"));
        return;
    }
    for (uint8_t m = 0; m < n; m++) {
        if (m > 0) msg.control.print(F(" OR "));
        msg.control.print(F("rule#"));
        msg.control.print(controlActionRuleAt(slot, m));
    }
    msg.control.println();
}

static FLASHMEM int actionLink(uint8_t id, const char* ruleArg) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    // NB: the drive is released only on the paths that actually change the link
    // (below), never up here. A rejected relink must leave every bit of state
    // untouched — silently dropping a live operator force on a command that
    // errored out is its own hazard, and the override is meant to be sticky
    // until explicitly released (#329).
    if (streq_P(ruleArg, PSTR("NONE"))) {
        const bool wasDriven = controlActionClearRules(id);
        msg.control.print(F("Action #"));
        msg.control.print(id);
        msg.control.println(F(" unlinked from every rule (manual). SAVE to persist."));
        if (wasDriven) msg.control.println(F("  (released; the action is now OFF until you drive it)"));
        return 0;
    }
    uint8_t ruleId = (uint8_t)atoi(ruleArg);
    if (controlRuleSlot(ruleId) < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(ruleId);
        return 1;
    }
    // A force — or a manual latch armed while this action was still unlinked —
    // applies to what was driving it before, not to the rule it now follows. Left
    // in place, the latch goes dormant and snaps the pin ON the next time the link
    // goes away (#294); releasing both here is what actually closes that (#329).
    // controlActionSetRule() does that release, and replaces the WHOLE membership:
    // `RULE <id>` means "exactly this one" (use RULE ADD to keep the others).
    const bool hadOthers = controlActionRuleCount(slot) > 1;
    bool wasDriven = false;
    controlActionSetRule(id, ruleId, &wasDriven);
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.print(F(" linked to rule #"));
    msg.control.print(ruleId);
    msg.control.println(F(". SAVE to persist."));
    if (hadOthers) msg.control.println(F("  (other member rules dropped; use RULE ADD to keep them)"));
    // Gate on the RELEASE, not on the override alone: the latch is released too,
    // so an action being held ON by hand drops OFF here — exactly the silent
    // de-energizing #329 added reporting for.
    if (wasDriven) msg.control.println(F("  (previous manual/override state released; the action is now OFF)"));
    return 0;
}

// ACTION <id> RULE ADD|REMOVE <ruleId> — edit ONE membership, leaving the rest.
static FLASHMEM int actionLinkEdit(uint8_t id, bool add, const char* ruleArg) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    uint8_t ruleId = (uint8_t)atoi(ruleArg);
    if (controlRuleSlot(ruleId) < 0) {
        msg.control.print(F("ERROR: no rule #"));
        msg.control.println(ruleId);
        return 1;
    }
    bool wasDriven = false;
    if (add) {
        if (controlActionHasRule(slot, ruleId)) {
            msg.control.print(F("Action #"));
            msg.control.print(id);
            msg.control.print(F(" already follows rule #"));
            msg.control.println(ruleId);
            return 0;
        }
        if (!controlActionAddRule(id, ruleId, &wasDriven)) {
            msg.control.print(F("ERROR: rule-link table full ("));
            msg.control.print(MAX_ACTION_LINKS);
            msg.control.println(F("); remove a membership first"));
            return 1;
        }
        msg.control.print(F("Action #"));
        msg.control.print(id);
        msg.control.print(F(" also follows rule #"));
        msg.control.print(ruleId);
        msg.control.println(F(". SAVE to persist."));
    } else {
        if (!controlActionRemoveRule(id, ruleId, &wasDriven)) {
            msg.control.print(F("ERROR: action #"));
            msg.control.print(id);
            msg.control.print(F(" does not follow rule #"));
            msg.control.println(ruleId);
            return 1;
        }
        msg.control.print(F("Action #"));
        msg.control.print(id);
        msg.control.print(F(" no longer follows rule #"));
        msg.control.print(ruleId);
        msg.control.println(F(". SAVE to persist."));
    }
    printMembership(slot);
    // Every membership change releases the drive (#294/#329) — but only SAY so
    // when something was actually released, or the common (already-AUTO, no latch)
    // case reports a state change that never happened.
    if (wasDriven) msg.control.println(F("  (action released to AUTO and starts OFF)"));
    return 0;
}

static FLASHMEM int actionManual(uint8_t id, bool on) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    // On an unlinked action this is the manual latch; on a rule-driven one it is an
    // operator override that outranks both the rule and the failsafe (#329). The
    // override is a DISTINCT field from the manual latch, which is what keeps the
    // #294 bug class away: unlinking later leaves AUTO, not a stale armed latch.
    const uint8_t members = controlActionRuleCount(slot);
    const bool ruleDriven = (members > 0);
    controlActionCommand(id, on);
    msg.control.print(F("Action #"));
    msg.control.print(id);
    if (ruleDriven) {
        msg.control.print(F(" OVERRIDE "));
        msg.control.print(on ? F("ON") : F("OFF"));
        msg.control.print(members > 1 ? F(" (rules held; release with ACTION <id> AUTO)")
                                      : F(" (rule held; release with ACTION <id> AUTO)"));
        msg.control.println();
        if (controlActionSourceFaulted(slot)) {
            msg.control.println(F("  NOTE: source is faulted - the failsafe is being overridden"));
        }
    } else {
        msg.control.print(F(" manual "));
        msg.control.println(on ? F("ON") : F("OFF"));
    }
    return 0;
}

static FLASHMEM int actionAuto(uint8_t id) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    const bool wasOverridden = (controlActionOverride(slot) != ACT_OVR_AUTO);
    controlActionSetOverride(id, ACT_OVR_AUTO);
    msg.control.print(F("Action #"));
    msg.control.print(id);
    if (!wasOverridden) {
        msg.control.println(F(" was not overridden (already AUTO)"));
    } else if (controlActionRuleCount(slot) == 0) {
        // Defensive: nothing sets an override on an unlinked action today (the
        // router sends those to the manual latch), and a link change releases it.
        msg.control.println(F(" override released (action is manual)"));
    } else {
        msg.control.println(F(" override released"));
        printMembership(slot);
    }
    return 0;
}

static FLASHMEM int actionSetName(uint8_t id, const char* name) {
    if (!controlActionSetName(id, name)) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.print(F(" named \""));
    msg.control.print(name);
    msg.control.println(F("\". SAVE to persist."));
    return 0;
}

static FLASHMEM int actionDel(uint8_t id) {
    int slot = controlActionSlot(id);
    if (slot < 0) {
        msg.control.print(F("ERROR: no action #"));
        msg.control.println(id);
        return 1;
    }
    // Capture the resolved physical level the pin is left at so the operator
    // isn't surprised by an inverted (active-low) relay sitting HIGH after clear
    // (#293): de-asserted/OFF respects INVERT, so OFF == HIGH for an active-low
    // relay (its safe, de-energized state) — not a stuck-on bug.
    const Action& a = actions[slot];
    const bool relay = (a.type == ACT_RELAY);
    const bool offHigh = relay && (a.behavior & ACT_BEHAVIOR_INVERT);  // OFF level for inverted = HIGH
    const uint8_t target = a.target;

    controlActionConfigurePin(slot);  // de-assert the pin before freeing
    controlActionForgetLinks(id);     // free its 2nd+ memberships (#343)
    actions[slot].id = CONTROL_ID_NONE;
    controlSyncActionPins();          // release the freed target pin in the registry
    msg.control.print(F("Action #"));
    msg.control.print(id);
    msg.control.println(F(" deleted. SAVE to persist."));
    if (relay) {
        msg.control.print(F("  pin "));
        msg.control.print(target);
        msg.control.print(F(" left de-energized (OFF = "));
        msg.control.print(offHigh ? F("HIGH, inverted") : F("LOW"));
        msg.control.println(F(")"));
    }
    return 0;
}

FLASHMEM int cmd_action(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: ACTION requires a subcommand"));
        msg.control.println(F("  Usage: ACTION LIST | ADD RELAY <target> ... | <id> RULE <ruleId|NONE|ADD <r>|REMOVE <r>> | <id> NAME <text> | <id> ON|OFF|AUTO | DEL <id>"));
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
        if (argc < 4) {
            msg.control.println(F("ERROR: usage: ACTION <id> RULE <ruleId|NONE|ADD <ruleId>|REMOVE <ruleId>>"));
            return 1;
        }
        // ADD/REMOVE edit one membership; a bare id REPLACES the whole set.
        if (streq_P(argv[3], PSTR("ADD")) || streq_P(argv[3], PSTR("REMOVE"))) {
            if (argc < 5) { msg.control.println(F("ERROR: usage: ACTION <id> RULE ADD|REMOVE <ruleId>")); return 1; }
            return actionLinkEdit(id, streq_P(argv[3], PSTR("ADD")), argv[4]);
        }
        return actionLink(id, argv[3]);
    }
    if (streq_P(argv[2], PSTR("NAME"))) {       // operator label — config-only
        if (isInRunMode()) {
            msg.control.println(F("ERROR: ACTION <id> NAME is config-only; enter CONFIG mode first"));
            return 1;
        }
        if (argc < 4) { msg.control.println(F("ERROR: usage: ACTION <id> NAME <text>")); return 1; }
        return actionSetName(id, argv[3]);
    }
    if (streq_P(argv[2], PSTR("ON")))   return actionManual(id, true);   // RUN-allowed
    if (streq_P(argv[2], PSTR("OFF")))  return actionManual(id, false);  // RUN-allowed
    if (streq_P(argv[2], PSTR("AUTO"))) return actionAuto(id);           // RUN-allowed

    msg.control.print(F("ERROR: unknown ACTION verb: "));
    msg.control.println(argv[2]);
    return 1;
}

#endif // ENABLE_CONTROL_ENGINE
