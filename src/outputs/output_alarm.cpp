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

// ===== ALARM OUTPUT STATE =====
static bool alarmSilenced = false;        // Is alarm currently silenced?
static uint32_t silenceStartTime = 0;    // When was silence button pressed?

// ===== INITIALIZATION =====

void initAlarmOutput() {
    // Configure buzzer output pin
    pinMode(BUZZER, OUTPUT);
    noTone(BUZZER);  // Ensure buzzer is off initially

    // Configure silence button with internal pullup
    // Button is active LOW (pulls pin to GND when pressed)
    pinMode(MODE_BUTTON, INPUT_PULLUP);

    Serial.println(F("✓ Alarm output initialized"));
}

// ===== OUTPUT MODULE INTERFACE =====

void sendAlarmOutput(Input* input) {
    // This function is called per-input by output_manager
    // We don't send per-input data, just need to satisfy the interface
    // (Actual alarm decision happens in updateAlarmOutput which scans all inputs)
}

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
    // Scan all inputs to see if ANY are in alarm
    bool anyAlarmActive = false;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled && inputs[i].flags.isInAlarm) {
            anyAlarmActive = true;
            break;  // Early exit - we only need to know if ANY alarm is active
        }
    }

    // ===== BUZZER CONTROL =====
    // Sound alarm if:
    // 1. At least one input is in alarm, AND
    // 2. Alarm is not silenced
    if (anyAlarmActive && !alarmSilenced) {
        tone(BUZZER, 700);  // 700 Hz alarm tone
    } else {
        noTone(BUZZER);     // Turn off buzzer
    }
}

// ===== QUERY FUNCTIONS =====

bool isAnyAlarmActive() {
    // Scan all inputs to check if any are in alarm
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled && inputs[i].flags.isInAlarm) {
            return true;
        }
    }
    return false;
}

bool isAlarmSilenced() {
    return alarmSilenced;
}
