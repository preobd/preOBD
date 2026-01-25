/*
 * voltage.h - Voltage Sensors
 *
 * Voltage divider for battery monitoring.
 */

#ifndef SENSOR_LIBRARY_SENSORS_VOLTAGE_H
#define SENSOR_LIBRARY_SENSORS_VOLTAGE_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_VOLTAGE_DIVIDER[] PROGMEM = "VOLTAGE_DIVIDER";
static const char PSTR_VOLTAGE_DIVIDER_LABEL[] PROGMEM = "Battery voltage (12V divider)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define VOLTAGE_SENSORS \
    X_SENSOR(PSTR_VOLTAGE_DIVIDER, PSTR_VOLTAGE_DIVIDER_LABEL, nullptr, readVoltageDivider, nullptr, \
             MEASURE_VOLTAGE, CAL_VOLTAGE_DIVIDER, nullptr, SENSOR_READ_INTERVAL_MS, 0.0, 30.0, 0x311D, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_VOLTAGE_H
