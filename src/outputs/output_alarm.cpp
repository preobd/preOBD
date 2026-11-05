/*
 * output_alarm.cpp - Alarm output module (buzzer, LEDs, etc.)
 *
 * Follows the control-engine Rules' annunciator severity (rules_manager).
 * Owns the acknowledgement POLICY (scope/duration, #433) and the buzzer/LED.
 *
 * This is a true output module integrated with output_manager.
 *
 * DESIGN:
 * - Alarm logic (state machine) lives in alarm_logic.cpp
 * - Hardware control (buzzer, silence) lives HERE
 * - This separation allows alarm logic to be tested without hardware
 * - Output can be enabled/disabled via serial commands like other outputs
 */

#include "output_alarm.h"
#include "../config.h"
#include "../inputs/input_manager.h"
#include "../pinctrl/pin_registry.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../core/system_mode.h"
#include "../core/platform.h"       // platformNoTone()
#include "../rules/rules_manager.h"   // annunciator: fold Rule severity into the rollup
#include "../storage/system_config.h" // alarmSilence policy byte (#433)

#if INCLUDE_RULES_ENGINE
// RuleSeverity and AlarmSeverity are DISTINCT ordinal domains: RuleSeverity
// inserts INFO=1 below WARNING (#308), so they no longer share numeric values.
// Map explicitly at the annunciator boundary — INFO is a log-only notification
// tier and annunciates like NORMAL (no LED/buzzer).
static AlarmSeverity ruleToAlarmSeverity(RuleSeverity s) {
    switch (s) {
        case RULE_SEV_WARNING: return SEVERITY_WARNING;
        case RULE_SEV_ALARM:   return SEVERITY_ALARM;
        default:               return SEVERITY_NORMAL;  // NONE, INFO → no annunciation
    }
}
#endif

#if INCLUDE_LED
#include "../drivers/rgb_led.h"
#endif

// ===== ACKNOWLEDGEMENT POLICY (#433) =====
// State lives per rule in the rules manager (rulesAck*); this module owns the
// policy (scope + duration) and is the single call site for button + CLI.

uint32_t alarmSilenceDurationMs() {
    const uint8_t code = systemConfig.alarmSilence & ALARM_SILENCE_DUR_MASK;
    if (code == ALARM_SILENCE_DUR_DEFAULT) return SILENCE_DURATION;
    if (code == ALARM_SILENCE_DUR_CLEAR)   return 0;
    return (uint32_t)code * ALARM_SILENCE_DUR_STEP_S * 1000UL;
}

bool alarmSilenceIncludesLed() {
    return (systemConfig.alarmSilence & ALARM_SILENCE_LED_BIT) != 0;
}

bool alarmSilenceSetDuration(uint16_t seconds) {
    uint8_t code;
    if (seconds == 0) {
        code = ALARM_SILENCE_DUR_CLEAR;
    } else if (seconds % ALARM_SILENCE_DUR_STEP_S != 0 || seconds > ALARM_SILENCE_DUR_MAX_S) {
        return false;
    } else {
        code = (uint8_t)(seconds / ALARM_SILENCE_DUR_STEP_S);
    }
    systemConfig.alarmSilence = (uint8_t)((systemConfig.alarmSilence & ALARM_SILENCE_LED_BIT) | code);
    return true;
}

void alarmSilenceSetLed(bool includeLed) {
    if (includeLed) systemConfig.alarmSilence |=  ALARM_SILENCE_LED_BIT;
    else            systemConfig.alarmSilence &= (uint8_t)~ALARM_SILENCE_LED_BIT;
}

void alarmAckHousekeeping() {
#if INCLUDE_RULES_ENGINE
    rulesAckExpire(millis(), alarmSilenceDurationMs());
#endif
}

uint8_t alarmAcknowledge() {
#if INCLUDE_RULES_ENGINE
    alarmAckHousekeeping();
    return rulesAckActiveAlarms(millis());
#else
    return 0;
#endif
}

// ===== INITIALIZATION =====

void initAlarmOutput() {
#if INCLUDE_ALARMS && defined(BUZZER_PIN)
    pinMode(BUZZER_PIN, OUTPUT);
    platformNoTone(BUZZER_PIN);
#endif

#if INCLUDE_LED
    msg.debug.info(TAG_ALARM, "Alarm output initialized (buzzer + LED indicator)");
#else
    msg.debug.info(TAG_ALARM, "Alarm output initialized");
#endif
}

// ===== OUTPUT MODULE INTERFACE =====

// ===== HELPER FUNCTIONS =====

// Annunciator severity rollup: worst-case over legacy per-input alarms AND
// control-engine Rules. This is the single source the LED + buzzer follow, so
// a triggered Rule (severity >= WARNING) annunciates exactly like an alarm
// (ARCHITECTURE §11.4). Legacy alarm config (preserved across the v12 bump in
// the input region) keeps working; Rules are additive.
AlarmSeverity getSystemSeverity() {
    // The legacy per-input alarm engine is retired — alarms are control-engine
    // Rules (§11). System severity is the max over active Rules.
#if INCLUDE_RULES_ENGINE
    return ruleToAlarmSeverity(rulesSystemSeverity());
#else
    return SEVERITY_NORMAL;
#endif
}

#if INCLUDE_LED
// Update RGB LED based on system severity. `acked` = every sounding alarm is
// acknowledged under SCOPE ALL (#433): show the ALARM colour steady.
void updateLEDs(AlarmSeverity severity, bool acked) {
    switch (severity) {
        case SEVERITY_NORMAL:
            // Normal operation - solid green at PRIORITY_IDLE (#419). It used to
            // sit at PRIORITY_WARNING, which outranks PRIORITY_MODE and so masked
            // the CONFIG-mode indication permanently: "nothing is wrong" is not a
            // warning, and claiming that slot meant the mode LED lost the
            // arbitration every time and could never be seen.
            //
            // Both higher slots must now be released explicitly. Previously this
            // branch *overwrote* the WARNING slot with green, which released it as
            // a side effect; writing to IDLE instead does not, so a cleared warning
            // would otherwise leave slot 3 active and the LED stuck yellow.
            rgbLedRelease(PRIORITY_ALARM);
            rgbLedRelease(PRIORITY_WARNING);
            rgbLedSolid(RGB_COLOR_NORMAL, PRIORITY_IDLE);
            break;

        case SEVERITY_WARNING:
            // Warning - blink yellow (or solid if disabled). De-escalate from a
            // prior alarm by releasing the higher PRIORITY_ALARM request.
            rgbLedRelease(PRIORITY_ALARM);
#if RGB_ALARM_USE_BLINK
            rgbLedBlink(RGB_COLOR_WARNING, RGB_BLINK_PERIOD_MS, PRIORITY_WARNING);
#else
            rgbLedSolid(RGB_COLOR_WARNING, PRIORITY_WARNING);
#endif
            break;

        case SEVERITY_ALARM:
            // Critical alarm - fast blink red (or solid if disabled, or once
            // acknowledged under SCOPE ALL)
#if RGB_ALARM_USE_BLINK
            if (acked) rgbLedSolid(RGB_COLOR_ALARM, PRIORITY_ALARM);
            else       rgbLedBlink(RGB_COLOR_ALARM, RGB_FAST_BLINK_MS, PRIORITY_ALARM);
#else
            (void)acked;
            rgbLedSolid(RGB_COLOR_ALARM, PRIORITY_ALARM);
#endif
            break;
    }
}
#endif

void updateAlarmOutput() {
    // ===== ACKNOWLEDGEMENT BOOKKEEPING =====
    // The mode button's short press arrives via main.cpp -> alarmAcknowledge()
    // (edge, same path as ALARM SILENCE); here we only expire (#433).
    alarmAckHousekeeping();

    // ===== ALARM STATE SCANNING =====
#if INCLUDE_LED || (INCLUDE_ALARMS && defined(BUZZER_PIN))
    AlarmSeverity systemSeverity = getSystemSeverity();
    // What is still UNACKNOWLEDGED — the buzzer follows this, and so does the
    // LED blink under SCOPE ALL.
    AlarmSeverity unackedSeverity = systemSeverity;
#if INCLUDE_RULES_ENGINE
    unackedSeverity = ruleToAlarmSeverity(rulesSystemSeverityUnacked());
#endif
#endif

#if INCLUDE_LED
    // ===== LED CONTROL =====
    // SCOPE ALL: every sounding alarm acknowledged -> steady red instead of the
    // blink. The fault stays visible; only the "you haven't seen this yet"
    // urgency is dropped. Buzzer-only scope leaves the LED exactly as before.
    const bool ledAcked = alarmSilenceIncludesLed() &&
                          systemSeverity == SEVERITY_ALARM &&
                          unackedSeverity != SEVERITY_ALARM;
    updateLEDs(systemSeverity, ledAcked);
#endif

    // ===== BUZZER CONTROL =====
    // Buzzer actuation is gated in CONFIG (#268): the engine and LED indicator
    // keep running so alarm state stays current, but the buzzer stays silent
    // while configuring on the bench (disconnected/test sensors would
    // otherwise sound spurious alarms). The LED above is informative, not
    // physical actuation, so it is left running in both modes.
#if INCLUDE_ALARMS && defined(BUZZER_PIN)
    if (unackedSeverity == SEVERITY_ALARM && !isInConfigMode()) {
        tone(BUZZER_PIN, 700);
    } else {
        platformNoTone(BUZZER_PIN);
    }
#endif
}

// ===== QUERY FUNCTIONS =====

bool isAnyAlarmActive() {
    return getSystemSeverity() == SEVERITY_ALARM;
}

bool isAlarmSilenced() {
#if INCLUDE_RULES_ENGINE
    return rulesAckCount() > 0;
#else
    return false;
#endif
}
