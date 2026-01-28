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

#ifdef ENABLE_LEDS
    // Configure LED output pins (with conflict checks)
    if (validateNoPinConflict(GREEN_LED, PIN_OUTPUT, "Green LED")) {
        registerPin(GREEN_LED, PIN_OUTPUT, "Green LED");
        pinMode(GREEN_LED, OUTPUT);
        digitalWrite(GREEN_LED, LOW);
    } else {
        msg.debug.warn(TAG_ALARM, "Green LED pin %d conflict - skipping", GREEN_LED);
    }

    if (validateNoPinConflict(YELLOW_LED, PIN_OUTPUT, "Yellow LED")) {
        registerPin(YELLOW_LED, PIN_OUTPUT, "Yellow LED");
        pinMode(YELLOW_LED, OUTPUT);
        digitalWrite(YELLOW_LED, LOW);
    } else {
        msg.debug.warn(TAG_ALARM, "Yellow LED pin %d conflict - skipping", YELLOW_LED);
    }

    if (validateNoPinConflict(RED_LED, PIN_OUTPUT, "Red LED")) {
        registerPin(RED_LED, PIN_OUTPUT, "Red LED");
        pinMode(RED_LED, OUTPUT);
        digitalWrite(RED_LED, LOW);
    } else {
        msg.debug.warn(TAG_ALARM, "Red LED pin %d conflict - skipping", RED_LED);
    }

    msg.debug.info(TAG_ALARM, "Alarm output initialized (buzzer + LEDs)");
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

#ifdef ENABLE_LEDS
// Update LED states based on system severity
void updateLEDs(AlarmSeverity severity) {
    // Mutually exclusive LED control
    digitalWrite(GREEN_LED, (severity == SEVERITY_NORMAL) ? HIGH : LOW);
    digitalWrite(YELLOW_LED, (severity == SEVERITY_WARNING) ? HIGH : LOW);
    digitalWrite(RED_LED, (severity == SEVERITY_ALARM) ? HIGH : LOW);
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

#ifdef ENABLE_LEDS
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
