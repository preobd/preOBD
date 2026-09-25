/*
 * cmd_alarm.cpp - ALARM command (annunciator acknowledgement, #433)
 *
 *   ALARM                       status (alias of ALARM STATUS)
 *   ALARM STATUS                severity rollup, acknowledged rules, policy   [RUN ok]
 *   ALARM SILENCE               acknowledge every rule sounding the buzzer     [RUN ok]
 *   ALARM DURATION <secs|CLEAR> timed re-arm (5..630 s, step 5) or hold until  [CONFIG]
 *                               the rule returns to normal
 *   ALARM SCOPE <BUZZER|ALL>    ALL: an acknowledged alarm also stops the LED  [CONFIG]
 *                               blink (steady red — fault still visible)
 *
 * SILENCE is the mode button's short press from a remote client (the app's
 * Acknowledge): both call alarmAcknowledge(), so they cannot diverge. It is
 * RUN-allowed because it changes no configuration — only which currently
 * sounding rules are muted. An ack is per rule: a DIFFERENT rule tripping later
 * still sounds. Timed DURATION mutes the rule for exactly that long (through a
 * glitch-and-retrip); CLEAR mutes it until it returns to normal. That is the
 * point of the per-rule model — a stuck sensor on a long drive is one press,
 * not one every 30 s, and it never masks the next fault. (ARCHITECTURE §11.4.)
 */

#include <Arduino.h>
#include <stdlib.h>
#include "../config.h"
#include "../rules/rules_types.h"  // defines INCLUDE_RULES_ENGINE before the guard

#if INCLUDE_RULES_ENGINE

#include "command_handlers.h"
#include "command_helpers.h"
#include "../core/system_mode.h"
#include "../core/message_api.h"
#include "../storage/system_config.h"
#include "../rules/rules_manager.h"
#include "../outputs/output_alarm.h"
#include "../core/config_generation.h"
#include "../pinctrl/pin_label.h"     // formatPinLabel: "A0", not "14"

// CLI handlers are cold — keep them in flash, not Teensy ITCM (RAM1).
#ifndef FLASHMEM
#define FLASHMEM
#endif

static FLASHMEM void printPolicy() {
    msg.control.print(F("Silence policy: "));
    const uint32_t durMs = alarmSilenceDurationMs();
    if (durMs == 0) msg.control.print(F("until clear"));
    else { msg.control.print(durMs / 1000UL); msg.control.print(F(" s")); }
    msg.control.print(F(", scope "));
    msg.control.println(alarmSilenceIncludesLed() ? F("ALL (buzzer + LED blink)") : F("BUZZER"));
}

// Remaining silence on an acked slot, in whole seconds (rounded up); 0 under
// the until-clear policy.
static uint32_t remainingS(int slot, uint32_t now, uint32_t durMs) {
    if (durMs == 0) return 0;
    const uint32_t el = rulesAckElapsedMs(slot, now);
    return (el >= durMs) ? 0 : (durMs - el + 999UL) / 1000UL;
}

static FLASHMEM int alarmStatus() {
    alarmAckHousekeeping();   // report current state even if the alarm output module is off
    const uint8_t sev = rulesSystemSeverity();
    msg.control.print(F("System severity: "));
    switch (sev) {   // F() per case: a RAM string table costs AVR its last bytes
        case RULE_SEV_ALARM:   msg.control.println(F("ALARM"));   break;
        case RULE_SEV_WARNING: msg.control.println(F("WARNING")); break;
        case RULE_SEV_INFO:    msg.control.println(F("INFO"));    break;
        default:               msg.control.println(F("NORMAL"));  break;
    }
    const uint8_t unacked = rulesSystemSeverityUnacked();
    msg.control.print(F("Buzzer: "));
    if (unacked == RULE_SEV_ALARM)   msg.control.println(isInConfigMode() ? F("would sound (muted in CONFIG)") : F("sounding"));
    else if (sev == RULE_SEV_ALARM)  msg.control.println(F("silenced (all sounding alarms acknowledged)"));
    else                             msg.control.println(F("quiet"));

    const uint32_t now = millis();
    const uint32_t durMs = alarmSilenceDurationMs();
    msg.control.print(F("Acknowledged: "));
    msg.control.println(rulesAckCount());
    for (int i = 0; i < MAX_RULES; i++) {
        if (!rulesRuleAcked(i)) continue;
        char label[12];
        formatPinLabel(rules[i].sourceInput, label, sizeof(label));
        msg.control.print(F("  Rule #"));
        msg.control.print(rules[i].id);
        msg.control.print(F(" (pin "));
        msg.control.print(label);
        msg.control.print(F(") - "));
        if (durMs == 0) {
            msg.control.println(F("until clear"));
        } else {
            msg.control.print(remainingS(i, now, durMs));
            msg.control.println(F(" s remaining"));
        }
    }
    printPolicy();
    return 0;
}

static FLASHMEM int alarmSilence() {
    const bool sounding = (rulesSystemSeverity() == RULE_SEV_ALARM);
    const uint8_t newly = alarmAcknowledge();
    if (!sounding) {
        msg.control.println(F("No alarm sounding - nothing to acknowledge."));
        return 0;
    }
    const uint32_t durMs = alarmSilenceDurationMs();
    if (newly == 0) {
        // Every sounding rule was already acked; the clock is NOT restarted,
        // so say what is actually left rather than repeating the full window.
        msg.control.print(F("Already acknowledged"));
        if (durMs == 0) {
            msg.control.println(F(" (until it clears)."));
        } else {
            uint32_t least = 0xFFFFFFFFUL;
            const uint32_t now = millis();
            for (int i = 0; i < MAX_RULES; i++) {
                if (rulesRuleAcked(i)) { const uint32_t r = remainingS(i, now, durMs); if (r < least) least = r; }
            }
            msg.control.print(F("; "));
            msg.control.print(least == 0xFFFFFFFFUL ? 0 : least);
            msg.control.println(F(" s remaining."));
        }
        return 0;
    }
    msg.control.print(F("Acknowledged "));
    msg.control.print(newly);
    msg.control.print(newly == 1 ? F(" alarm; silent ") : F(" alarms; silent "));
    if (durMs == 0) msg.control.println(F("until it clears."));
    else { msg.control.print(F("for ")); msg.control.print(durMs / 1000UL); msg.control.println(F(" s.")); }
    return 0;
}

static FLASHMEM int alarmDuration(int argc, const char* const* argv) {
    if (argc < 3) {
        msg.control.println(F("ERROR: usage: ALARM DURATION <5-630 (step 5)|CLEAR>"));
        return 1;
    }
    uint16_t secs;
    if (streq_P(argv[2], PSTR("CLEAR"))) {
        secs = 0;
    } else {
        char* end = nullptr;
        long v = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || v <= 0 || v > ALARM_SILENCE_DUR_MAX_S) {
            msg.control.println(F("ERROR: DURATION must be 5..630 seconds (step 5) or CLEAR"));
            return 1;
        }
        secs = (uint16_t)v;
    }
    if (!alarmSilenceSetDuration(secs)) {
        msg.control.println(F("ERROR: DURATION must be a multiple of 5 seconds"));
        return 1;
    }
    printPolicy();
    msg.control.println(F("SAVE to persist."));
    return 0;
}

static FLASHMEM int alarmScope(int argc, const char* const* argv) {
    if (argc < 3) {
        msg.control.println(F("ERROR: usage: ALARM SCOPE <BUZZER|ALL>"));
        return 1;
    }
    if (streq_P(argv[2], PSTR("BUZZER")))   alarmSilenceSetLed(false);
    else if (streq_P(argv[2], PSTR("ALL"))) alarmSilenceSetLed(true);
    else {
        msg.control.println(F("ERROR: SCOPE must be BUZZER or ALL"));
        return 1;
    }
    printPolicy();
    msg.control.println(F("SAVE to persist."));
    return 0;
}

FLASHMEM int cmd_alarm(int argc, const char* const* argv) {
    if (argc < 2 || streq_P(argv[1], PSTR("STATUS"))) return alarmStatus();
    if (streq_P(argv[1], PSTR("SILENCE")))            return alarmSilence();

    const bool policy = streq_P(argv[1], PSTR("DURATION")) || streq_P(argv[1], PSTR("SCOPE"));
    if (policy && isInRunMode()) {
        msg.control.print(F("ERROR: ALARM "));
        msg.control.print(argv[1]);
        msg.control.println(F(" is config-only; enter CONFIG mode first"));
        return 1;
    }
    if (policy) configGenerationBump();   // past the CONFIG gate (#455)
    if (streq_P(argv[1], PSTR("DURATION"))) return alarmDuration(argc, argv);
    if (streq_P(argv[1], PSTR("SCOPE")))    return alarmScope(argc, argv);

    msg.control.print(F("ERROR: unknown ALARM verb: "));
    msg.control.println(argv[1]);
    msg.control.println(F("  Usage: ALARM [STATUS] | SILENCE | DURATION <secs|CLEAR> | SCOPE <BUZZER|ALL>"));
    return 1;
}

#endif // INCLUDE_RULES_ENGINE
