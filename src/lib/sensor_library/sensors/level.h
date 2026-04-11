/*
 * level.h - Fluid Level Sensors
 *
 * Table-based resistive fuel level senders (VDO).
 * Outputs percentage: 0% = Empty, 100% = Full.
 */

#ifndef SENSOR_LIBRARY_SENSORS_LEVEL_H
#define SENSOR_LIBRARY_SENSORS_LEVEL_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_VDO_FUEL_LEVEL_180[] PROGMEM = "VDO_FUEL_LEVEL_180";
static const char PSTR_VDO_FUEL_LEVEL_180_LABEL[] PROGMEM = "VDO Fuel Level 3-180\xCE\xA9 (European, ascending)";
static const char PSTR_VDO_FUEL_LEVEL_240[] PROGMEM = "VDO_FUEL_LEVEL_240";
static const char PSTR_VDO_FUEL_LEVEL_240_LABEL[] PROGMEM = "VDO Fuel Level 240-34\xCE\xA9 (European, descending)";
static const char PSTR_VDO_FUEL_LEVEL_75[] PROGMEM = "VDO_FUEL_LEVEL_75";
static const char PSTR_VDO_FUEL_LEVEL_75_LABEL[] PROGMEM = "VDO Fuel Level 75-3\xCE\xA9 (tubular, descending)";
static const char PSTR_VDO_FUEL_LEVEL_90[] PROGMEM = "VDO_FUEL_LEVEL_90";
static const char PSTR_VDO_FUEL_LEVEL_90_LABEL[] PROGMEM = "VDO Fuel Level 0-90\xCE\xA9 (US standard, ascending)";

// Jeep/AMC
static const char PSTR_JEEP_CJ_FUEL_LEVEL[] PROGMEM = "JEEP_CJ_FUEL_LEVEL";
static const char PSTR_JEEP_CJ_FUEL_LEVEL_LABEL[] PROGMEM = "Jeep CJ Fuel Level Sender (0-100%, 10-73\xCE\xA9)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define LEVEL_SENSORS \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_180, PSTR_VDO_FUEL_LEVEL_180_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel180_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x7D88, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_240, PSTR_VDO_FUEL_LEVEL_240_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel240_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x8145, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_75, PSTR_VDO_FUEL_LEVEL_75_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel75_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x331B, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_90, PSTR_VDO_FUEL_LEVEL_90_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel90_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x3358, PIN_ANALOG) \
    /* Jeep/AMC */ \
    X_SENSOR(PSTR_JEEP_CJ_FUEL_LEVEL, PSTR_JEEP_CJ_FUEL_LEVEL_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &jeep_cj_fuel_level_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x0437, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_LEVEL_H
