/*
 * level.h - Level / Position Sensors (0-100% normalized analog inputs)
 *
 * Table-based resistive fuel level senders (VDO, Stewart-Warner, Jeep)
 * and generic linear position sensors (e.g. throttle position).
 * Outputs percentage: 0% = Empty/closed, 100% = Full/open.
 */

#ifndef SENSOR_LIBRARY_SENSORS_LEVEL_H
#define SENSOR_LIBRARY_SENSORS_LEVEL_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
// Position sensors (linear analog)
static const char PSTR_GENERIC_TPS[] PROGMEM = "GENERIC_TPS";
static const char PSTR_GENERIC_TPS_LABEL[] PROGMEM = "0.5-4.5V linear (0-100% throttle)";

// Fuel level senders (resistive, table-based)
static const char PSTR_VDO_FUEL_LEVEL_180[] PROGMEM = "VDO_FUEL_LEVEL_180";
static const char PSTR_VDO_FUEL_LEVEL_180_LABEL[] PROGMEM = "VDO Fuel Level 3-180\xCE\xA9 (European, ascending)";
static const char PSTR_VDO_FUEL_LEVEL_240[] PROGMEM = "VDO_FUEL_LEVEL_240";
static const char PSTR_VDO_FUEL_LEVEL_240_LABEL[] PROGMEM = "VDO Fuel Level 240-34\xCE\xA9 (European, descending)";
static const char PSTR_VDO_FUEL_LEVEL_75[] PROGMEM = "VDO_FUEL_LEVEL_75";
static const char PSTR_VDO_FUEL_LEVEL_75_LABEL[] PROGMEM = "VDO Fuel Level 75-3\xCE\xA9 (tubular, descending)";
static const char PSTR_VDO_FUEL_LEVEL_90[] PROGMEM = "VDO_FUEL_LEVEL_90";
static const char PSTR_VDO_FUEL_LEVEL_90_LABEL[] PROGMEM = "VDO Fuel Level 0-90\xCE\xA9 (US standard, ascending)";

// Stewart-Warner
static const char PSTR_SW_FUEL_LEVEL_240[] PROGMEM = "SW_FUEL_LEVEL_240";
static const char PSTR_SW_FUEL_LEVEL_240_LABEL[] PROGMEM = "Stewart-Warner Fuel Level 240-33\xCE\xA9 (US, descending)";
static const char PSTR_SW_FUEL_LEVEL_240_DESC[] PROGMEM = "Stewart-Warner fuel sender, 240\xCE\xA9 empty, 33\xCE\xA9 full. Endpoints only; linear interp, \xC2\xB1""10% level.";
static const char PSTR_SW_FORD_FUEL_LEVEL[] PROGMEM = "SW_FORD_FUEL_LEVEL";
static const char PSTR_SW_FORD_FUEL_LEVEL_LABEL[] PROGMEM = "Ford/Chrysler Fuel Level 73-10\xCE\xA9 (US, descending)";
static const char PSTR_SW_FORD_FUEL_LEVEL_DESC[] PROGMEM = "Ford/Chrysler fuel sender (60s-80s). 73\xCE\xA9 empty, 10\xCE\xA9 full. Endpoints only; linear interp, \xC2\xB1""10% level.";

// Jeep/AMC
static const char PSTR_JEEP_CJ_FUEL_LEVEL[] PROGMEM = "JEEP_CJ_FUEL_LEVEL";
static const char PSTR_JEEP_CJ_FUEL_LEVEL_LABEL[] PROGMEM = "Jeep CJ Fuel Level Sender (0-100%, 10-73\xCE\xA9)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define LEVEL_SENSORS \
    /* Position sensors (linear analog) */ \
    X_SENSOR(PSTR_GENERIC_TPS, PSTR_GENERIC_TPS_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_LEVEL, CAL_LINEAR, &generic_tps_linear_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0xF738, PIN_ANALOG) \
    /* Fuel level senders (resistive, table-based) */ \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_180, PSTR_VDO_FUEL_LEVEL_180_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel180_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x7D88, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_240, PSTR_VDO_FUEL_LEVEL_240_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel240_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x8145, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_75, PSTR_VDO_FUEL_LEVEL_75_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel75_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x331B, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_FUEL_LEVEL_90, PSTR_VDO_FUEL_LEVEL_90_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &vdo_fuel90_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x3358, PIN_ANALOG) \
    /* Stewart-Warner */ \
    X_SENSOR(PSTR_SW_FUEL_LEVEL_240, PSTR_SW_FUEL_LEVEL_240_LABEL, PSTR_SW_FUEL_LEVEL_240_DESC, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &sw_fuel_level_240_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x82A6, PIN_ANALOG) \
    X_SENSOR(PSTR_SW_FORD_FUEL_LEVEL, PSTR_SW_FORD_FUEL_LEVEL_LABEL, PSTR_SW_FORD_FUEL_LEVEL_DESC, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &sw_ford_fuel_level_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x6EFB, PIN_ANALOG) \
    /* Jeep/AMC */ \
    X_SENSOR(PSTR_JEEP_CJ_FUEL_LEVEL, PSTR_JEEP_CJ_FUEL_LEVEL_LABEL, nullptr, readLevelTable, nullptr, \
             MEASURE_LEVEL, CAL_LEVEL_TABLE, &jeep_cj_fuel_level_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x0437, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_LEVEL_H
