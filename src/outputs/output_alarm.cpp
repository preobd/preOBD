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

#if ENABLE_LED
#include "../lib/rgb_led.h"
#endif

// ===== ALARM OUTPUT STATE =====
static bool alarmSilenced = false;        // Is alarm currently silenced?
static uint32_t silenceStartTime = 0;    // When was silence button pressed?

// ===== INITIALIZATION =====

void initAlarmOutput() {
#if ENABLE_ALARMS && defined(ALARMS_PIN)
    pinMode(ALARMS_PIN, OUTPUT);
    noTone(ALARMS_PIN);
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

// Scan all inputs and return worst-case severity
AlarmSeverity getSystemSeverity() {
    AlarmSeverity worstSeverity = SEVERITY_NORMAL;

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            if (inputs[i].currentSeverity > worstSeverity) {
                worstSeverity = inputs[i].currentSeverity;
            }
        }
    }

    return worstSeverity;
}

#if ENABLE_LED
// Update RGB LED based on system severity
void updateLEDs(AlarmSeverity severity) {
    switch (severity) {
        case SEVERITY_NORMAL:
            // Normal operation - solid green
            rgbLedSolid(RGB_COLOR_NORMAL, PRIORITY_WARNING);
            break;

        case SEVERITY_WARNING:
            // Warning - blink yellow (or solid if disabled)
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
#if ENABLE_LED || (ENABLE_ALARMS && defined(ALARMS_PIN))
    AlarmSeverity systemSeverity = getSystemSeverity();
#endif

#if ENABLE_LED
    // ===== LED CONTROL =====
    updateLEDs(systemSeverity);
#endif

    // ===== BUZZER CONTROL =====
#if ENABLE_ALARMS && defined(ALARMS_PIN)
    if (systemSeverity == SEVERITY_ALARM && !alarmSilenced) {
        tone(ALARMS_PIN, 700);
    } else {
        noTone(ALARMS_PIN);
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
