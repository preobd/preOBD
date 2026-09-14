/*
 * frequency.h - Frequency/Pulse-Counting Sensors
 *
 * RPM sensors (W-phase alternator) and speed sensors (Hall effect).
 * These sensors measure frequency/pulse rate and convert to RPM or speed.
 */

#ifndef SENSOR_LIBRARY_SENSORS_FREQUENCY_H
#define SENSOR_LIBRARY_SENSORS_FREQUENCY_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
// RPM sensors
static const char PSTR_W_PHASE_RPM[] PROGMEM = "W_PHASE_RPM";
static const char PSTR_W_PHASE_RPM_LABEL[] PROGMEM = "W-phase alternator RPM";

// Speed sensors
static const char PSTR_HALL_SPEED[] PROGMEM = "HALL_SPEED";
static const char PSTR_HALL_SPEED_LABEL[] PROGMEM = "Hall Effect Speed Sensor";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define FREQUENCY_SENSORS \
    /* RPM sensors */ \
    X_SENSOR(PSTR_W_PHASE_RPM, PSTR_W_PHASE_RPM_LABEL, nullptr, readWPhaseRPM, initWPhaseRPM, \
             MEASURE_RPM, CAL_RPM, &default_rpm_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10000.0, 0x1F3A, PIN_DIGITAL) \
    /* Speed sensors */ \
    X_SENSOR(PSTR_HALL_SPEED, PSTR_HALL_SPEED_LABEL, nullptr, readHallSpeed, initHallSpeed, \
             MEASURE_SPEED, CAL_SPEED, &hall_speed_sensor_cal, SENSOR_READ_INTERVAL_MS, 0.0, 300.0, 0xB076, PIN_DIGITAL)

#endif // SENSOR_LIBRARY_SENSORS_FREQUENCY_H
