/*
 * digital.h - Digital Input Sensors
 *
 * Float switch and other digital on/off sensors.
 */

#ifndef SENSOR_LIBRARY_SENSORS_DIGITAL_H
#define SENSOR_LIBRARY_SENSORS_DIGITAL_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_FLOAT_SWITCH[] PROGMEM = "FLOAT_SWITCH";
static const char PSTR_FLOAT_SWITCH_LABEL[] PROGMEM = "Float/level switch (digital)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define DIGITAL_SENSORS \
    X_SENSOR(PSTR_FLOAT_SWITCH, PSTR_FLOAT_SWITCH_LABEL, nullptr, readDigitalFloatSwitch, initFloatSwitch, \
             MEASURE_DIGITAL, CAL_NONE, nullptr, SENSOR_READ_INTERVAL_MS, 0.0, 1.0, 0xF22C, PIN_DIGITAL)

#endif // SENSOR_LIBRARY_SENSORS_DIGITAL_H
