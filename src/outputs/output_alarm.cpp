/*
 * output_alarm.cpp - Alarm output module (buzzer, LEDs, etc.)
 *
 * Reacts to Input.flags.isInAlarm state set by alarm_logic.cpp
 * Manages silence button and alarm hardware outputs.
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
#include "../lib/pin_registry.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "../lib/system_mode.h"
#include "../control/control_manager.h"   // annunciator: fold Rule severity into the rollup

#if ENABLE_CONTROL_ENGINE
// The annunciator maps Rule severity to the same buzzer/LED effects as legacy
// alarms, so RuleSeverity and AlarmSeverity must share numeric values.
static_assert((int)RULE_SEV_NONE    == (int)SEVERITY_NORMAL,  "severity enum drift");
static_assert((int)RULE_SEV_WARNING == (int)SEVERITY_WARNING, "severity enum drift");
static_assert((int)RULE_SEV_ALARM   == (int)SEVERITY_ALARM,   "severity enum drift");
#endif

#if ENABLE_LED
#include "../lib/rgb_led.h"
#endif

// ===== ALARM OUTPUT STATE =====
static bool alarmSilenced = false;        // Is alarm currently silenced?
static uint32_t silenceStartTime = 0;    // When was silence button pressed?

// ===== INITIALIZATION =====

void initAlarmOutput() {
#if ENABLE_ALARMS && defined(BUZZER_PIN)
    pinMode(BUZZER_PIN, OUTPUT);
    noTone(BUZZER_PIN);
#endif

#if ENABLE_MODE_BUTTON
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
#endif

#if ENABLE_LED
    msg.debug.info(TAG_ALARM, "Alarm output initialized (buzzer + LED indicator)");
#else
    msg.debug.info(TAG_ALARM, "Alarm output initialized");
#endif
}

// ===== OUTPUT MODULE INTERFACE =====

void sendAlarmOutput(Input* input) {
    // This function is called per-input by output_manager
    // We don't send per-input data, just need to satisfy the interface
    // (Actual alarm decision happens in updateAlarmOutput which scans all inputs)
}

// ===== HELPER FUNCTIONS =====

// Annunciator severity rollup: worst-case over legacy per-input alarms AND
// control-engine Rules. This is the single source the LED + buzzer follow, so
// a triggered Rule (severity >= WARNING) annunciates exactly like an alarm
// (ARCHITECTURE §11.4). Legacy alarm config (preserved across the v12 bump in
// the input region) keeps working; Rules are additive.
AlarmSeverity getSystemSeverity() {
    // The legacy per-input alarm engine is retired — alarms are control-engine
    // Rules (§11). System severity is the max over active Rules.
#if ENABLE_CONTROL_ENGINE
    return (AlarmSeverity)controlSystemSeverity();
#else
    return SEVERITY_NORMAL;
#endif
}

#if ENABLE_LED
// Update RGB LED based on system severity
void updateLEDs(AlarmSeverity severity) {
    switch (severity) {
        case SEVERITY_NORMAL:
            // Normal operation - solid green. Release any latched alarm request
            // first so the LED de-escalates (the alarm color sits at the higher
            // PRIORITY_ALARM and would otherwise stick on after the alarm clears).
            rgbLedRelease(PRIORITY_ALARM);
            rgbLedSolid(RGB_COLOR_NORMAL, PRIORITY_WARNING);
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
            // Critical alarm - fast blink red (or solid if disabled)
#if RGB_ALARM_USE_BLINK
            rgbLedBlink(RGB_COLOR_ALARM, RGB_FAST_BLINK_MS, PRIORITY_ALARM);
#else
            rgbLedSolid(RGB_COLOR_ALARM, PRIORITY_ALARM);
#endif
            break;
    }
}
#endif

void updateAlarmOutput() {
    // ===== SILENCE BUTTON HANDLING =====
#if ENABLE_MODE_BUTTON
    if (digitalRead(MODE_BUTTON_PIN) == LOW) {
        if (!alarmSilenced) {
            alarmSilenced = true;
            silenceStartTime = millis();
        }
    }
#endif

    // Check if silence duration expired
    if (alarmSilenced && (millis() - silenceStartTime >= SILENCE_DURATION)) {
        alarmSilenced = false;
    }

    // ===== ALARM STATE SCANNING =====
#if ENABLE_LED || (ENABLE_ALARMS && defined(BUZZER_PIN))
    AlarmSeverity systemSeverity = getSystemSeverity();
#endif

#if ENABLE_LED
    // ===== LED CONTROL =====
    updateLEDs(systemSeverity);
#endif

    // ===== BUZZER CONTROL =====
    // Buzzer actuation is gated in CONFIG (#268): the engine and LED indicator
    // keep running so alarm state stays current, but the buzzer stays silent
    // while configuring on the bench (disconnected/test sensors would
    // otherwise sound spurious alarms). The LED above is informative, not
    // physical actuation, so it is left running in both modes.
#if ENABLE_ALARMS && defined(BUZZER_PIN)
    if (systemSeverity == SEVERITY_ALARM && !alarmSilenced && !isInConfigMode()) {
        tone(BUZZER_PIN, 700);
    } else {
        noTone(BUZZER_PIN);
    }
#endif
}

// ===== QUERY FUNCTIONS =====

bool isAnyAlarmActive() {
    return getSystemSeverity() == SEVERITY_ALARM;
}

bool isAlarmSilenced() {
    return alarmSilenced;
}
