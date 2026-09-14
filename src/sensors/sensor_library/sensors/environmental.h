/*
 * environmental.h - Environmental Sensors
 *
 * BME280 environmental sensor (temperature, pressure, humidity, elevation).
 */

#ifndef SENSOR_LIBRARY_SENSORS_ENVIRONMENTAL_H
#define SENSOR_LIBRARY_SENSORS_ENVIRONMENTAL_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
static const char PSTR_BME280_TEMP[] PROGMEM = "BME280_TEMP";
static const char PSTR_BME280_TEMP_LABEL[] PROGMEM = "BME280 temperature (I2C)";
static const char PSTR_BME280_PRESSURE[] PROGMEM = "BME280_PRESSURE";
static const char PSTR_BME280_PRESSURE_LABEL[] PROGMEM = "BME280 barometric pressure (I2C)";
static const char PSTR_BME280_HUMIDITY[] PROGMEM = "BME280_HUMIDITY";
static const char PSTR_BME280_HUMIDITY_LABEL[] PROGMEM = "BME280 relative humidity (I2C)";
static const char PSTR_BME280_ELEVATION[] PROGMEM = "BME280_ELEVATION";
static const char PSTR_BME280_ELEVATION_LABEL[] PROGMEM = "BME280 altitude (I2C)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define ENVIRONMENTAL_SENSORS \
    X_SENSOR(PSTR_BME280_TEMP, PSTR_BME280_TEMP_LABEL, nullptr, readBME280Temp, initBME280, \
             MEASURE_TEMPERATURE, CAL_NONE, nullptr, SENSOR_READ_INTERVAL_MS, -40.0, 85.0, 0x72A8, PIN_I2C) \
    X_SENSOR(PSTR_BME280_PRESSURE, PSTR_BME280_PRESSURE_LABEL, nullptr, readBME280Pressure, initBME280, \
             MEASURE_PRESSURE, CAL_NONE, nullptr, SENSOR_READ_INTERVAL_MS, 0.3, 1.1, 0x454B, PIN_I2C) \
    X_SENSOR(PSTR_BME280_HUMIDITY, PSTR_BME280_HUMIDITY_LABEL, nullptr, readBME280Humidity, initBME280, \
             MEASURE_HUMIDITY, CAL_NONE, nullptr, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0x381F, PIN_I2C) \
    X_SENSOR(PSTR_BME280_ELEVATION, PSTR_BME280_ELEVATION_LABEL, nullptr, readBME280Elevation, initBME280, \
             MEASURE_ELEVATION, CAL_NONE, nullptr, SENSOR_READ_INTERVAL_MS, -500.0, 9000.0, 0x2619, PIN_I2C)

#endif // SENSOR_LIBRARY_SENSORS_ENVIRONMENTAL_H
