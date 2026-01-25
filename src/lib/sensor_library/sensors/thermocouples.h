/*
 * thermocouples.h - K-Type Thermocouple Sensors
 *
 * MAX6675 and MAX31855 thermocouple amplifier chips.
 */

#ifndef SENSOR_LIBRARY_SENSORS_THERMOCOUPLES_H
#define SENSOR_LIBRARY_SENSORS_THERMOCOUPLES_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_MAX6675[] PROGMEM = "MAX6675";
static const char PSTR_MAX6675_LABEL[] PROGMEM = "K-Type Thermocouple (0-1024" "\xC2\xB0" "C)";
static const char PSTR_MAX31855[] PROGMEM = "MAX31855";
static const char PSTR_MAX31855_LABEL[] PROGMEM = "K-Type Thermocouple (-270-1372" "\xC2\xB0" "C)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define THERMOCOUPLE_SENSORS \
    X_SENSOR(PSTR_MAX6675, PSTR_MAX6675_LABEL, nullptr, readMAX6675, initThermocoupleCS, \
             MEASURE_TEMPERATURE, CAL_NONE, nullptr, 250, 0.0, 1024.0, 0x2A23, PIN_DIGITAL) \
    X_SENSOR(PSTR_MAX31855, PSTR_MAX31855_LABEL, nullptr, readMAX31855, initThermocoupleCS, \
             MEASURE_TEMPERATURE, CAL_NONE, nullptr, 100, -200.0, 1350.0, 0x6B91, PIN_DIGITAL)

#endif // SENSOR_LIBRARY_SENSORS_THERMOCOUPLES_H
