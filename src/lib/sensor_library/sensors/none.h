/*
 * none.h - SENSOR_NONE Placeholder Entry
 *
 * Index 0 placeholder for unconfigured inputs.
 */

#ifndef SENSOR_LIBRARY_SENSORS_NONE_H
#define SENSOR_LIBRARY_SENSORS_NONE_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_NONE[] PROGMEM = "NONE";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define NONE_SENSORS \
    X_SENSOR(PSTR_NONE, nullptr, nullptr, nullptr, nullptr, \
             MEASURE_TEMPERATURE, CAL_NONE, nullptr, 0, 0.0, 0.0, 0x2F75, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_NONE_H
