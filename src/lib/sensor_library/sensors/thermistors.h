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
static const char PSTR_VDO_200C_TABLE[] PROGMEM = "VDO_200C_TABLE";
static const char PSTR_VDO_200C_TABLE_LABEL[] PROGMEM = "VDO 200C Cylinder Temp (table)";

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

// Smiths (British classics)
static const char PSTR_SMITHS_TT6811_TEMP[] PROGMEM = "SMITHS_TT6811_TEMP";
static const char PSTR_SMITHS_TT6811_TEMP_LABEL[] PROGMEM = "Smiths TT6811 Coolant Temp (MG/Triumph/Austin-Healey)";
static const char PSTR_SMITHS_TT6811_TEMP_DESC[] PROGMEM = "Smiths TT6811 gauge sender. 6 data points, 70-120\xC2\xB0""C. 100\xCE\xA9 bias.";
static const char PSTR_SMITHS_TT4201_TEMP[] PROGMEM = "SMITHS_TT4201_TEMP";
static const char PSTR_SMITHS_TT4201_TEMP_LABEL[] PROGMEM = "Smiths TT4201 Coolant Temp (Jaguar E-Type)";
static const char PSTR_SMITHS_TT4201_TEMP_DESC[] PROGMEM = "Smiths TT4201 gauge sender. 6 data points, 30-110\xC2\xB0""C. 100\xCE\xA9 bias.";
static const char PSTR_SMITHS_GTR104_TEMP[] PROGMEM = "SMITHS_GTR104_TEMP";
static const char PSTR_SMITHS_GTR104_TEMP_LABEL[] PROGMEM = "Smiths GTR104 Coolant Temp (generic British gauge, approx)";
static const char PSTR_SMITHS_GTR104_TEMP_DESC[] PROGMEM = "Smiths GTR104 gauge sender. 3-point curve only — accuracy approx \xC2\xB1""8\xC2\xB0""C. 100\xCE\xA9 bias.";

// AC Delco / GM OEM
static const char PSTR_ACDELCO_TEMP_PRE79[] PROGMEM = "ACDELCO_TEMP_PRE79";
static const char PSTR_ACDELCO_TEMP_PRE79_LABEL[] PROGMEM = "AC Delco GM Coolant Temp Sender, pre-1979 (1967-1978 trucks)";
static const char PSTR_ACDELCO_TEMP_PRE79_DESC[] PROGMEM = "AC Delco pre-1979 GM gauge sender. Community-derived curve, approx \xC2\xB1""8\xC2\xB0""C. 100\xCE\xA9 bias.";
static const char PSTR_ACDELCO_TEMP_POST79[] PROGMEM = "ACDELCO_TEMP_POST79";
static const char PSTR_ACDELCO_TEMP_POST79_LABEL[] PROGMEM = "AC Delco GM Coolant Temp Sender, 1979+ (1979-1990 trucks)";
static const char PSTR_ACDELCO_TEMP_POST79_DESC[] PROGMEM = "AC Delco 1979+ GM gauge sender. Requires 2.49k\xCE\xA9 bias (NOT 100\xCE\xA9). Approx \xC2\xB1""8\xC2\xB0""C.";

// Bosch NTC M12
static const char PSTR_BOSCH_NTC_M12[] PROGMEM = "BOSCH_NTC_M12";
static const char PSTR_BOSCH_NTC_M12_LABEL[] PROGMEM = "Bosch NTC M12 EFI Temp Sensor (-40 to 120\xC2\xB0""C)";
static const char PSTR_BOSCH_NTC_M12_DESC[] PROGMEM = "Bosch NTC M12 (2057\xCE\xA9 @ 25\xC2\xB0""C). Volvo/VW/Audi/BMW/Haltech. Requires 2.49k\xCE\xA9 bias.";

// Jeep/AMC
static const char PSTR_JEEP_4_0_TEMP_GAUGE[] PROGMEM = "JEEP_4_0_TEMP_GAUGE";
static const char PSTR_JEEP_4_0_TEMP_GAUGE_LABEL[] PROGMEM = "Jeep 4.0L Coolant Temp Sender (Gauge) (0-120" "\xC2\xB0" "C)";
static const char PSTR_JEEP_RENIX_CTS[] PROGMEM = "JEEP_RENIX_CTS";
static const char PSTR_JEEP_RENIX_CTS_LABEL[] PROGMEM = "Jeep Renix ECU Coolant Temp Sensor (-20-180" "\xC2\xB0" "C)";
static const char PSTR_JEEP_CJ_TEMP_GAUGE[] PROGMEM = "JEEP_CJ_TEMP_GAUGE";
static const char PSTR_JEEP_CJ_TEMP_GAUGE_LABEL[] PROGMEM = "Jeep CJ Coolant Temp Sender (Gauge) (49-127" "\xC2\xB0" "C, approx)";

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
    X_SENSOR(PSTR_VDO_200C_TABLE, PSTR_VDO_200C_TABLE_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &vdo200_lookup_cal, SENSOR_READ_INTERVAL_MS, 60.0, 200.0, 0x2F09, PIN_ANALOG) \
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
    /* Smiths (British classics) */ \
    X_SENSOR(PSTR_SMITHS_TT6811_TEMP, PSTR_SMITHS_TT6811_TEMP_LABEL, PSTR_SMITHS_TT6811_TEMP_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &smiths_tt6811_temp_cal, SENSOR_READ_INTERVAL_MS, 70.0, 120.0, 0x5FA9, PIN_ANALOG) \
    X_SENSOR(PSTR_SMITHS_TT4201_TEMP, PSTR_SMITHS_TT4201_TEMP_LABEL, PSTR_SMITHS_TT4201_TEMP_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &smiths_tt4201_temp_cal, SENSOR_READ_INTERVAL_MS, 30.0, 110.0, 0x43A0, PIN_ANALOG) \
    X_SENSOR(PSTR_SMITHS_GTR104_TEMP, PSTR_SMITHS_GTR104_TEMP_LABEL, PSTR_SMITHS_GTR104_TEMP_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &smiths_gtr104_temp_cal, SENSOR_READ_INTERVAL_MS, 20.0, 100.0, 0xF273, PIN_ANALOG) \
    /* AC Delco / GM OEM */ \
    X_SENSOR(PSTR_ACDELCO_TEMP_PRE79, PSTR_ACDELCO_TEMP_PRE79_LABEL, PSTR_ACDELCO_TEMP_PRE79_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &acdelco_pre79_temp_cal, SENSOR_READ_INTERVAL_MS, 40.0, 120.0, 0x6E5B, PIN_ANALOG) \
    X_SENSOR(PSTR_ACDELCO_TEMP_POST79, PSTR_ACDELCO_TEMP_POST79_LABEL, PSTR_ACDELCO_TEMP_POST79_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &acdelco_post79_temp_cal, SENSOR_READ_INTERVAL_MS, 20.0, 120.0, 0x18DA, PIN_ANALOG) \
    /* Bosch NTC M12 */ \
    X_SENSOR(PSTR_BOSCH_NTC_M12, PSTR_BOSCH_NTC_M12_LABEL, PSTR_BOSCH_NTC_M12_DESC, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &bosch_ntc_m12_cal, SENSOR_READ_INTERVAL_MS, -40.0, 120.0, 0xC847, PIN_ANALOG) \
    /* Jeep/AMC */ \
    X_SENSOR(PSTR_JEEP_4_0_TEMP_GAUGE, PSTR_JEEP_4_0_TEMP_GAUGE_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &jeep40_temp_gauge_cal, SENSOR_READ_INTERVAL_MS, 0.0, 120.0, 0x29E8, PIN_ANALOG) \
    X_SENSOR(PSTR_JEEP_RENIX_CTS, PSTR_JEEP_RENIX_CTS_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &renix_cts_cal, SENSOR_READ_INTERVAL_MS, -20.0, 180.0, 0x34F7, PIN_ANALOG) \
    X_SENSOR(PSTR_JEEP_CJ_TEMP_GAUGE, PSTR_JEEP_CJ_TEMP_GAUGE_LABEL, nullptr, readThermistorLookup, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_TABLE, &jeep_cj_temp_gauge_cal, SENSOR_READ_INTERVAL_MS, 49.0, 127.0, 0xCDF2, PIN_ANALOG) \
    /* Linear temperature */ \
    X_SENSOR(PSTR_GENERIC_TEMP_LINEAR, PSTR_GENERIC_TEMP_LINEAR_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_TEMPERATURE, CAL_LINEAR, &generic_temp_linear_cal, SENSOR_READ_INTERVAL_MS, -40.0, 150.0, 0xDF11, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_THERMISTORS_H
