/*
 * config.h - User Configuration File
 * 
 * BASIC SETUP: Just pick sensor types from sensor_library.h
 * ADVANCED SETUP: See advanced_config.h for custom calibrations
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "sensor_library.h"  // Import sensor catalog

// ===== ENABLED OUTPUT MODULES =====
//#define ENABLE_CAN
//#define ENABLE_REALDASH
//#define ENABLE_SERIAL_OUTPUT
//#define ENABLE_SD_LOGGING

// ===== ENABLED DISPLAY MODULES =====
#define ENABLE_LCD
//#define ENABLE_OLED

// ===== SENSOR CONFIGURATION =====
// For each sensor: enable it, pick a type, assign a pin

// === CHT (Cylinder Head Temperature) ===
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675  // Pick from sensor_library.h
#define CHT_INPUT             6                             // SPI chip select pin
#define CHT_MIN               -1
#define CHT_MAX               495

// === EGT (Exhaust Gas Temperature) ===
#define ENABLE_EGT
#define EGT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX31855  // Pick from sensor_library.h
#define EGT_INPUT             7                             // SPI chip select pin
#define EGT_MIN               -1
#define EGT_MAX               600

// === Coolant Temperature ===
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP              // Pick from sensor_library.h
#define COOLANT_TEMP_INPUT    A2                           // Analog pin
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100

// === Oil Temperature ===
#define ENABLE_OIL_TEMP
#define OIL_TEMP_SENSOR_TYPE  VDO_150C_STEINHART          // Try Steinhart for faster reads
#define OIL_TEMP_INPUT        A0
#define OIL_TEMP_MIN          -1
#define OIL_TEMP_MAX          150

// === Transfer Case Temperature ===
//#define ENABLE_TCASE_TEMP
//#define TCASE_TEMP_SENSOR_TYPE  VDO_120C_LOOKUP
//#define TCASE_TEMP_INPUT        A1
//#define TCASE_TEMP_MIN          -1
//#define TCASE_TEMP_MAX          100

// === Boost Pressure ===
//#define ENABLE_BOOST_PRESSURE
//#define BOOST_PRESSURE_SENSOR_TYPE  VDO_2BAR_PRESSURE
//#define BOOST_PRESSURE_INPUT        A4
//#define BOOST_PRESSURE_MIN          -1
//#define BOOST_PRESSURE_MAX          2

// === Oil Pressure ===
//#define ENABLE_OIL_PRESSURE
//#define OIL_PRESSURE_SENSOR_TYPE    VDO_5BAR_PRESSURE
//#define OIL_PRESSURE_INPUT          A3
//#define OIL_PRESSURE_MIN            1
//#define OIL_PRESSURE_MAX            5

// === Primary Battery ===
//#define ENABLE_PRIMARY_BATTERY
//#define PRIMARY_BATTERY_SENSOR_TYPE   STANDARD_12V_DIVIDER  // Uses platform.h defaults
//#define PRIMARY_BATTERY_INPUT         A8

// === Secondary Battery ===
//#define ENABLE_AUXILIARY_BATTERY
//#define SECONDARY_BATTERY_SENSOR_TYPE  STANDARD_12V_DIVIDER
//#define SECONDARY_BATTERY_INPUT        A7

// === Ambient Temperature (BME280) ===
#define ENABLE_AMBIENT_TEMP
#define AMBIENT_TEMP_SENSOR_TYPE      BME280_AMBIENT_TEMPERATURE
// No input pin needed - uses I2C

// === Barometric Pressure (BME280) ===
#define ENABLE_BAROMETRIC_PRESSURE
#define BARO_PRESSURE_SENSOR_TYPE     BME280_BAROMETRIC_PRESSURE
// No input pin needed - uses I2C

// === Humidity (BME280) ===
#define ENABLE_HUMIDITY
#define HUMIDITY_SENSOR_TYPE          BME280_RELATIVE_HUMIDITY
// No input pin needed - uses I2C

// === Altitude (BME280) ===
#define ENABLE_ALTITUDE
#define ALTITUDE_SENSOR_TYPE          BME280_ESTIMATED_ALTITUDE
// No input pin needed - uses I2C

// ===== DIGITAL I/O =====
#define CAN_INT 2
#define BUZZER 3
#define SILENCE 4
#define CAN_CS 9

// ===== ALARM CONFIGURATION =====
#define SILENCE_DURATION 30000  // ms

// ===== CALIBRATION =====
#define SEA_LEVEL_PRESSURE_HPA 1013.25  // standard atmospheric pressure (1013.25 hPa / 101.325 kPa)

// ===== TIMING =====
#define LOOP_DELAY_MS 200

// ===== ADVANCED CALIBRATION (Optional) =====
// Uncomment to use custom calibrations instead of presets
// See advanced_config.h for examples

// Example: Custom coolant sensor with 470Ω bias resistor
// #define COOLANT_CUSTOM_CALIBRATION
// #ifdef COOLANT_CUSTOM_CALIBRATION
//     #define COOLANT_BIAS_RESISTOR     470.0
//     #define COOLANT_STEINHART_A       1.299e-3
//     #define COOLANT_STEINHART_B       2.401e-4
//     #define COOLANT_STEINHART_C       1.301e-7
// #endif

#endif