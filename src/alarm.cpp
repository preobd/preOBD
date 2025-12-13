/*
 * alarm.cpp - Alarm system management
 */

#include "inputs/input.h"
#include "lib/sensor_types.h"
#include "config.h"

static bool alarmActive = false;
static bool silenced = false;
static unsigned long silenceStartTime = 0;

void initAlarm() {
    pinMode(BUZZER, OUTPUT);
    pinMode(MODE_BUTTON, INPUT_PULLUP);  // Internal pullup - button pulls to GND when pressed
    noTone(BUZZER);
}

void checkSensorAlarm(Input *ptr) {
    // Check global alarm enable flag first
    #ifndef ENABLE_ALARMS
    return;
    #endif

    if (!ptr->flags.alarm || !ptr->flags.isEnabled || isnan(ptr->value)) {
        return;
    }

    // Check if value is outside threshold
    if (ptr->value >= ptr->maxValue || ptr->value <= ptr->minValue) {
        alarmActive = true;
    }
}

void updateAlarm() {
    // Check MODE_BUTTON for alarm silence (only functional in RUN mode)
    // Button is active LOW (INPUT_PULLUP - button pulls to GND when pressed)
    if (digitalRead(MODE_BUTTON) == LOW) {
        silenced = true;
        silenceStartTime = millis();
    }
    
    // Check if silence duration expired
    if (silenced && (millis() - silenceStartTime >= SILENCE_DURATION)) {
        silenced = false;
    }
    
    // Sound alarm if active and not silenced
    if (alarmActive && !silenced) {
        tone(BUZZER, 700);
    } else {
        noTone(BUZZER);
    }
    
    // Reset alarm flag for next iteration
    alarmActive = false;
}

bool isAlarmActive() {
    return alarmActive;
}

bool isAlarmSilenced() {
    return silenced;
}
