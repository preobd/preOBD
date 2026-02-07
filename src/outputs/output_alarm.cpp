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

#ifdef ENABLE_LED
#include "../lib/rgb_led.h"
#endif

// ===== ALARM OUTPUT STATE =====
static bool alarmSilenced = false;        // Is alarm currently silenced?
static uint32_t silenceStartTime = 0;    // When was silence button pressed?

// ===== INITIALIZATION =====

void initAlarmOutput() {
    // Configure buzzer output pin
    // Note: Pin is already registered in registerSystemPins() as PIN_BUZZER
    pinMode(BUZZER, OUTPUT);
    noTone(BUZZER);  // Ensure buzzer is off initially

    // Configure silence button with internal pullup
    // Button is active LOW (pulls pin to GND when pressed)
    pinMode(MODE_BUTTON, INPUT_PULLUP);

#ifdef ENABLE_LED
    // RGB LED is initialized separately in main.cpp
    msg.debug.info(TAG_ALARM, "Alarm output initialized (buzzer + LED indicator)");
#else
    msg.debug.info(TAG_ALARM, "Alarm output initialized (buzzer)");
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

#ifdef ENABLE_LED
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
    // Check silence button (active LOW with pullup)
    // Only record first press (don't re-trigger on held button)
    if (digitalRead(MODE_BUTTON) == LOW) {
        if (!alarmSilenced) {  // Only update on first press
            alarmSilenced = true;
            silenceStartTime = millis();
        }
    }

    // Check if silence duration expired
    if (alarmSilenced && (millis() - silenceStartTime >= SILENCE_DURATION)) {
        alarmSilenced = false;
    }

    // ===== ALARM STATE SCANNING =====
    // Scan all inputs to determine worst-case severity
    AlarmSeverity systemSeverity = getSystemSeverity();

#ifdef ENABLE_LED
    // ===== LED CONTROL =====
    updateLEDs(systemSeverity);
#endif

    // ===== BUZZER CONTROL =====
    // Sound alarm only on RED (SEVERITY_ALARM), not on YELLOW (SEVERITY_WARNING)
    if (systemSeverity == SEVERITY_ALARM && !alarmSilenced) {
        tone(BUZZER, 700);  // 700 Hz alarm tone
    } else {
        noTone(BUZZER);     // Turn off buzzer
    }
}

// ===== QUERY FUNCTIONS =====

bool isAnyAlarmActive() {
    return getSystemSeverity() == SEVERITY_ALARM;
}

bool isAlarmSilenced() {
    return alarmSilenced;
}
