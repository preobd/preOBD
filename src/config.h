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

// ===== CAN CONFIGURATION =====
// Choose CAN implementation (only relevant if ENABLE_CAN is defined above)
//
// For Teensy 3.x/4.x boards:
//   - USE_FLEXCAN_NATIVE: Use built-in FlexCAN peripheral (no external chip needed)
//     * Teensy 4.0/4.1: CAN1 TX=22, RX=23 (also CAN2 and CAN3 available)
//     * Teensy 3.2/3.5/3.6: CAN1 TX=3, RX=4 (CAN2 available on 3.6)
//   - Leave undefined: Use external MCP2515 chip via SPI (requires CAN_CS and CAN_INT pins)
//
// For other boards (Arduino Mega, Uno, Due):
//   - Must use external MCP2515 chip (USE_FLEXCAN_NATIVE not supported)
//
//#define USE_FLEXCAN_NATIVE  // Uncomment to use built-in FlexCAN on Teensy boards

// ===== ENABLED DISPLAY MODULES =====
#define ENABLE_LCD
//#define ENABLE_OLED

// ===== GLOBAL DISPLAY UNITS DEFAULTS =====
// Set your preferred units for each measurement type
// Individual sensors can override these defaults below

// Temperature default (CELSIUS or FAHRENHEIT)
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS

// Pressure default (BAR, PSI, or KPA)
#define DEFAULT_PRESSURE_UNITS     BAR

// Altitude default (METERS or FEET)
#define DEFAULT_ALTITUDE_UNITS     FEET

// NOTE: Voltage is always displayed in VOLTS
// NOTE: Humidity is always displayed in PERCENT

// ===== SENSOR CONFIGURATION =====
// For each sensor: enable it, pick a type, assign a pin
// Optionally override display units for individual sensors

// === CHT (Cylinder Head Temperature) ===
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_MIN               -1
#define CHT_MAX               495
// Override display units for this sensor (optional)
//#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Uncomment to use F instead of default

// === EGT (Exhaust Gas Temperature) ===
#define ENABLE_EGT
#define EGT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX31855
#define EGT_INPUT             7
#define EGT_MIN               -1
#define EGT_MAX               600
//#define EGT_DISPLAY_UNITS     CELSIUS

// === Coolant Temperature ===
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100
//#define COOLANT_DISPLAY_UNITS FAHRENHEIT

// === Oil Temperature ===
#define ENABLE_OIL_TEMP
#define OIL_TEMP_SENSOR_TYPE  VDO_150C_STEINHART
#define OIL_TEMP_INPUT        A0
#define OIL_TEMP_MIN          -1
#define OIL_TEMP_MAX          150
//#define OIL_DISPLAY_UNITS     CELSIUS

// === Transfer Case Temperature ===
//#define ENABLE_TCASE_TEMP
//#define TCASE_TEMP_SENSOR_TYPE  VDO_120C_LOOKUP
//#define TCASE_TEMP_INPUT        A1
//#define TCASE_TEMP_MIN          -1
//#define TCASE_TEMP_MAX          100
//#define TCASE_DISPLAY_UNITS     FAHRENHEIT

// === Boost Pressure ===
//#define ENABLE_BOOST_PRESSURE
//#define BOOST_PRESSURE_SENSOR_TYPE  VDO_2BAR_PRESSURE
//#define BOOST_PRESSURE_INPUT        A4
//#define BOOST_PRESSURE_MIN          -1
//#define BOOST_PRESSURE_MAX          2
//#define BOOST_DISPLAY_UNITS         PSI  // Override to PSI

// === Oil Pressure ===
//#define ENABLE_OIL_PRESSURE
//#define OIL_PRESSURE_SENSOR_TYPE    VDO_5BAR_PRESSURE
//#define OIL_PRESSURE_INPUT          A3
//#define OIL_PRESSURE_MIN            1
//#define OIL_PRESSURE_MAX            5
//#define OIL_PRESSURE_DISPLAY_UNITS  BAR

// === Primary Battery ===
// #define ENABLE_PRIMARY_BATTERY
// #define PRIMARY_BATTERY_SENSOR_TYPE   STANDARD_12V_DIVIDER
// #define PRIMARY_BATTERY_INPUT         A8
// Voltage is always displayed in VOLTS

// === Secondary Battery ===
//#define ENABLE_AUXILIARY_BATTERY
//#define SECONDARY_BATTERY_SENSOR_TYPE  STANDARD_12V_DIVIDER
//#define SECONDARY_BATTERY_INPUT        A7

// === Custom Voltage Monitoring ===
// Example: Monitoring a 5V rail directly (no divider needed)
//#define ENABLE_CUSTOM_VOLTAGE
//#define CUSTOM_VOLTAGE_SENSOR_TYPE     DIRECT_VOLTAGE_5V
//#define CUSTOM_VOLTAGE_INPUT           A9

// === Ambient Temperature (BME280) ===
#define ENABLE_AMBIENT_TEMP
#define AMBIENT_TEMP_SENSOR_TYPE      BME280_AMBIENT_TEMPERATURE
#define AMBIENT_DISPLAY_UNITS         FAHRENHEIT

// === Barometric Pressure (BME280) ===
#define ENABLE_BAROMETRIC_PRESSURE
#define BARO_PRESSURE_SENSOR_TYPE     BME280_BAROMETRIC_PRESSURE
#define BARO_DISPLAY_UNITS            INHG  // inHg for US weather

// === Humidity (BME280) ===
#define ENABLE_HUMIDITY
#define HUMIDITY_SENSOR_TYPE          BME280_RELATIVE_HUMIDITY
// Humidity is always displayed in PERCENT

// === Altitude (BME280) ===
#define ENABLE_ALTITUDE
#define ALTITUDE_SENSOR_TYPE          BME280_ESTIMATED_ALTITUDE
//#define ALTITUDE_DISPLAY_UNITS        FEET

// ===== DIGITAL I/O =====
#define CAN_INT 2
#define BUZZER 3
#define SILENCE 4
#define CAN_CS 9

// ===== ALARM CONFIGURATION =====
#define SILENCE_DURATION 30000  // ms

// ===== CALIBRATION =====
#define SEA_LEVEL_PRESSURE_HPA 1013.25

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