/*
 * thermistors.h - NTC Thermistor Sensors
 *
 * VDO temperature sensors (table and Steinhart-Hart) and generic NTC placeholders.
 */

#ifndef SENSOR_LIBRARY_SENSORS_THERMISTORS_H
#define SENSOR_LIBRARY_SENSORS_THERMISTORS_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
// VDO Table-based
static const char PSTR_VDO_120C_TABLE[] PROGMEM = "VDO_120C_TABLE";
static const char PSTR_VDO_120C_TABLE_LABEL[] PROGMEM = "VDO 120C (table)";
static const char PSTR_VDO_150C_TABLE[] PROGMEM = "VDO_150C_TABLE";
static const char PSTR_VDO_150C_TABLE_LABEL[] PROGMEM = "VDO 150C (table)";

// VDO Steinhart-Hart
static const char PSTR_VDO_120C_STEINHART[] PROGMEM = "VDO_120C_STEINHART";
static const char PSTR_VDO_120C_STEINHART_LABEL[] PROGMEM = "VDO 120C (Steinhart-Hart)";
static const char PSTR_VDO_150C_STEINHART[] PROGMEM = "VDO_150C_STEINHART";
static const char PSTR_VDO_150C_STEINHART_LABEL[] PROGMEM = "VDO 150C (Steinhart-Hart)";

// Generic NTC (placeholders)
static const char PSTR_NTC_TABLE[] PROGMEM = "GENERIC_NTC_TABLE";
static const char PSTR_NTC_TABLE_LABEL[] PROGMEM = "Generic NTC (custom table)";
static const char PSTR_NTC_STEINHART[] PROGMEM = "GENERIC_NTC_STEINHART";
static const char PSTR_NTC_STEINHART_LABEL[] PROGMEM = "Generic NTC (custom Steinhart-Hart)";
static const char PSTR_NTC_BETA[] PROGMEM = "GENERIC_NTC_BETA";
static const char PSTR_NTC_BETA_LABEL[] PROGMEM = "Generic NTC (custom Beta equation)";

// Linear temperature
static const char PSTR_GENERIC_TEMP_LINEAR[] PROGMEM = "GENERIC_TEMP_LINEAR";
static const char PSTR_GENERIC_TEMP_LINEAR_LABEL[] PROGMEM = "0.5-4.5V linear (-40 to 150" "\xC2\xB0" "C)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define THERMISTOR_SENSORS \
    /* VDO Table-based */ \
    X_SENSOR(PSTR_VDO_120C_TABLE, PSTR_VDO_120C_TABLE_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &vdo120_lookup_cal, SENSOR_READ_INTERVAL_MS, -40.0, 150.0, 0x7FEA, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_150C_TABLE, PSTR_VDO_150C_TABLE_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &vdo150_lookup_cal, SENSOR_READ_INTERVAL_MS, -40.0, 180.0, 0xD2ED, PIN_ANALOG) \
    /* VDO Steinhart-Hart */ \
    X_SENSOR(PSTR_VDO_120C_STEINHART, PSTR_VDO_120C_STEINHART_LABEL, nullptr, readThermistorSteinhart, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_STEINHART, &vdo120_steinhart_cal, SENSOR_READ_INTERVAL_MS, -40.0, 150.0, 0x7434, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_150C_STEINHART, PSTR_VDO_150C_STEINHART_LABEL, nullptr, readThermistorSteinhart, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_STEINHART, &vdo150_steinhart_cal, SENSOR_READ_INTERVAL_MS, -40.0, 180.0, 0x90B7, PIN_ANALOG) \
    /* Generic NTC (placeholders - no read function) */ \
    X_SENSOR(PSTR_NTC_TABLE, PSTR_NTC_TABLE_LABEL, nullptr, nullptr, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, nullptr, 0, -40.0, 150.0, 0x482D, PIN_ANALOG) \
    X_SENSOR(PSTR_NTC_STEINHART, PSTR_NTC_STEINHART_LABEL, nullptr, nullptr, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_STEINHART, nullptr, 0, -40.0, 150.0, 0xA5F7, PIN_ANALOG) \
    X_SENSOR(PSTR_NTC_BETA, PSTR_NTC_BETA_LABEL, nullptr, nullptr, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_BETA, nullptr, 0, -40.0, 150.0, 0x1F61, PIN_ANALOG) \
    /* Linear temperature */ \
    X_SENSOR(PSTR_GENERIC_TEMP_LINEAR, PSTR_GENERIC_TEMP_LINEAR_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_TEMPERATURE, CAL_LINEAR, &generic_temp_linear_cal, SENSOR_READ_INTERVAL_MS, -40.0, 150.0, 0xDF11, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_THERMISTORS_H
