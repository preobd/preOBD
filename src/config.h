/*
 * config.h - User Configuration File
 * Enable/disable modules and sensors here
 */

#ifndef CONFIG_H
#define CONFIG_H

// ===== ENABLED OUTPUT MODULES =====
#define ENABLE_CAN
//#define ENABLE_REALDASH
//#define ENABLE_SERIAL_OUTPUT
//#define ENABLE_SD_LOGGING

// ===== ENABLED DISPLAY MODULES =====
#define ENABLE_LCD
//#define ENABLE_OLED

// ===== ENABLED SENSORS =====
#define ENABLE_CHT
#define ENABLE_EGT
#define ENABLE_COOLANT_TEMP
//#define ENABLE_OIL_TEMP
//#define ENABLE_TCASE_TEMP
//#define ENABLE_BOOST_PRESSURE
//#define ENABLE_OIL_PRESSURE
#define ENABLE_PRIMARY_BATTERY
//#define ENABLE_AUXILIARY_BATTERY
#define ENABLE_AMBIENT_TEMP
#define ENABLE_BAROMETRIC_PRESSURE
#define ENABLE_HUMIDITY
#define ENABLE_ALTITUDE

// ===== ANALOG INPUTS =====
#define OIL_TEMP_INPUT A0
#define TCASE_TEMP_INPUT A1
#define COOLANT_TEMP_INPUT A2
#define OIL_PRESSURE_INPUT A3
#define BOOST_PRESSURE_INPUT A4
#define FUEL_PRESSURE_INPUT A5
#define PRIMARY_BATTERY_INPUT A8
#define SECONDARY_BATTERY_INPUT A7

// ===== DIGITAL I/O =====
#define CAN_INT 2
#define BUZZER 3
#define SILENCE 4
#define CHT_INPUT 6
#define EGT_INPUT 7
#define CAN_CS 9

// ===== ALARM CONFIGURATION =====
#define SILENCE_DURATION 30000  // ms

// ===== SENSOR THRESHOLDS =====
#define CHT_MIN -1
#define CHT_MAX 495
#define EGT_MIN -1
#define EGT_MAX 600
#define COOLANT_TEMP_MIN -1
#define COOLANT_TEMP_MAX 100
#define OIL_TEMP_MIN -1
#define OIL_TEMP_MAX 100
#define TCASE_TEMP_MIN -1
#define TCASE_TEMP_MAX 100
#define BOOST_PRESSURE_MIN -1
#define BOOST_PRESSURE_MAX 2
#define OIL_PRESSURE_MIN -1
#define OIL_PRESSURE_MAX 5

// ===== CALIBRATION =====
#define SEA_LEVEL_PRESSURE_HPA 1013.25  // standard atmospheric pressure (1013.25 hPa / 101.325 kPa)

// ===== TIMING =====
#define LOOP_DELAY_MS 200

#endif
