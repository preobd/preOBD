/*
 * alarm.cpp - Alarm system management
 */

#include "input.h"
#include "sensor_types.h"
#include "config.h"

static bool alarmActive = false;
static bool silenced = false;
static unsigned long silenceStartTime = 0;

void initAlarm() {
    pinMode(BUZZER, OUTPUT);
    pinMode(SILENCE, INPUT);
    noTone(BUZZER);
}

void checkSensorAlarm(Input *ptr) {
    // Check global alarm enable flag first
    #ifndef ENABLE_ALARMS
    return;
    #endif

    if (!ptr->alarm || !ptr->isEnabled || isnan(ptr->value)) {
        return;
    }

    // Check if value is outside threshold
    if (ptr->value >= ptr->maxValue || ptr->value <= ptr->minValue) {
        alarmActive = true;
    }
}

void updateAlarm() {
    // Check silence button
    if (digitalRead(SILENCE) == HIGH) {
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
