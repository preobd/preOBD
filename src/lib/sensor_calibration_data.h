/*
 * sensor_calibration_data.h - Sensor Calibration Database
 *
 * Contains all default sensor calibrations stored in PROGMEM (flash memory).
 * This data is used by sensor_library.h to provide default calibrations
 * for various sensor types.
 */

#ifndef SENSOR_CALIBRATION_DATA_H
#define SENSOR_CALIBRATION_DATA_H

#include <Arduino.h>
#include "sensor_types.h"
#include "../config.h"

// ===== VDO 120°C LOOKUP TABLES =====
// Source: VDO datasheet and empirical measurements
// Valid range: 0°C to 150°C (extrapolated beyond 120°C)
// Resistance vs Temperature lookup table for VDO 120°C sensors
static const float vdo120_resistance[] PROGMEM = {
    1743.15, 1364.07, 1075.63, 850.09, 676.95, 543.54, 439.29, 356.64, 291.46,
    239.56, 197.29, 161.46, 134.03, 113.96, 97.05, 82.36, 70.12, 59.73, 51.21,
    44.32, 38.47, 33.4, 29.12, 25.53, 22.44, 19.75, 17.44, 15.46, 13.75, 12.26, 10.96
};

static const float vdo120_temperature[] PROGMEM = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
    105, 110, 115, 120, 125, 130, 135, 140, 145, 150
};

// ===== VDO 150°C LOOKUP TABLES =====
// Resistance vs Temperature lookup table for VDO 150°C sensors
static const float vdo150_resistance[] PROGMEM = {
    3240.18, 2473.60, 1905.87, 1486.65, 1168.64, 926.71, 739.98, 594.90, 481.53,
    392.57, 322.17, 266.19, 221.17, 184.72, 155.29, 131.38, 112.08, 96.40, 82.96,
    71.44, 61.92, 54.01, 47.24, 41.42, 36.51, 32.38, 28.81, 25.70, 23.0, 20.66, 18.59,
    16.74, 15.11, 13.66, 12.38, 11.25, 10.24
};

static const float vdo150_temperature[] PROGMEM = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 105,
    110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175, 180
};

// ===== THERMISTOR LOOKUP CALIBRATIONS =====

// VDO 120°C using lookup table
static const PROGMEM ThermistorLookupCalibration vdo120_lookup_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo120_resistance,
    .temperature_table = vdo120_temperature,
    .table_size = 31
};

// VDO 150°C using lookup table
static const PROGMEM ThermistorLookupCalibration vdo150_lookup_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo150_resistance,
    .temperature_table = vdo150_temperature,
    .table_size = 37
};

// ===== THERMISTOR STEINHART-HART CALIBRATIONS =====

// VDO 120°C using Steinhart-Hart (curve-fitted from lookup table)
// Steinhart-Hart coefficients curve-fitted from VDO resistance table
// Accuracy: ±1°C across 20-120°C range
// Bias resistor: Set by DEFAULT_BIAS_RESISTOR in config.h
static const PROGMEM ThermistorSteinhartCalibration vdo120_steinhart_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .steinhart_a = 1.764445997570e-03,
    .steinhart_b = 2.499534389889e-04,
    .steinhart_c = 6.773335597401e-08
};

// VDO 150°C using Steinhart-Hart (curve-fitted from lookup table)
// Steinhart-Hart coefficients curve-fitted from VDO resistance table
// Accuracy: ±1°C across 20-150°C range
// Bias resistor: Set by DEFAULT_BIAS_RESISTOR in config.h
static const PROGMEM ThermistorSteinhartCalibration vdo150_steinhart_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .steinhart_a = 1.591623373219e-03,
    .steinhart_b = 2.659356969556e-04,
    .steinhart_c = -1.610552525653e-07
};

// ===== PRESSURE SENSOR CALIBRATIONS =====

// VDO 5-bar pressure sensor polynomial calibration
// Source: VDO datasheet curve-fit
// Polynomial: R = -0.3682*P² + 36.465*P + 10.648
// Valid range: 0-5 bar
// Bias resistor: Set by DEFAULT_BIAS_RESISTOR in config.h
static const PROGMEM PolynomialCalibration vdo5bar_polynomial_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .poly_a = -0.3682,
    .poly_b = 36.465,
    .poly_c = 10.648
};

// VDO 2-bar pressure sensor polynomial calibration
// Source: VDO datasheet curve-fit
// Polynomial: R = -3.1515*P² + 93.686*P + 9.6307
// Valid range: 0-2 bar
// Bias resistor: Set by DEFAULT_BIAS_RESISTOR in config.h
static const PROGMEM PolynomialCalibration vdo2bar_polynomial_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .poly_a = -3.1515,
    .poly_b = 93.686,
    .poly_c = 9.6307
};

// Generic 0.5-4.5V linear sensor, 0-5 bar range
// Common automotive MAP/boost sensor specification
// Source: Industry standard for 3-wire pressure sensors
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration generic_boost_linear_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = 0.0,
    .output_max = 5.0
};

// Freescale (NXP) MPX4250AP (20-250 kPa, 0.2V-4.7V)
// Source: MPX4250AP datasheet
// Integrated pressure sensor with signal conditioning
// Output: 0.2V @ 20kPa, 4.7V @ 250kPa
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration mpx4250ap_linear_cal = {
    .voltage_min = 0.2,
    .voltage_max = 4.7,
    .output_min = 0.2,    // 20 kPa = 0.2 bar
    .output_max = 2.5     // 250 kPa = 2.5 bar
};

// Generic 0-150 PSI (0-10.34 bar) linear pressure sensor (0.5V-4.5V)
// Common for oil pressure and fuel pressure monitoring
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration generic_pressure_150psi_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = 0.0,
    .output_max = 10.34     // 150 PSI = 10.34 bar (base unit)
};

// AEM 30-2130-150: 150 PSIg stainless steel pressure sensor (0.5V-4.5V)
// Source: AEM datasheet - Transfer function: PSI = (37.5*V)-18.75
// Accuracy: ±0.5% Full Scale over -40C to 105C
// Common for oil pressure and fuel pressure monitoring
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration aem_30_2130_150_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = 0.0,
    .output_max = 10.34     // 150 PSI = 10.34 bar (base unit)
};

// ===== LINEAR TEMPERATURE SENSOR CALIBRATIONS =====

// Generic linear temperature sensor (-40°C to 150°C, 0.5V-4.5V)
// Common specification for automotive temperature sensors (oil, coolant, transmission)
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
// or choose 3.3V-compatible sensors to avoid damage to ADC inputs
static const PROGMEM LinearCalibration generic_temp_linear_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = -40.0,    // Temperature in °C (base unit)
    .output_max = 150.0
};

// ===== RPM CALIBRATION =====

// Default W-Phase RPM calibration (12-pole alternator, 3:1 pulley ratio)
// Suitable for most automotive alternators
static const PROGMEM RPMCalibration default_rpm_cal = {
    .poles = 12,             // Most common automotive alternator
    .pulley_ratio = 3.0,     // Typical automotive ratio (range 2.5-3.5:1)
    .calibration_mult = 1.0, // No fine-tuning by default
    .timeout_ms = 2000,      // 2 seconds without pulse = engine stopped
    .min_rpm = 100,          // Minimum valid RPM (reject noise)
    .max_rpm = 10000         // Maximum valid RPM (reject spikes)
};

// Alternative: 12-pole alternator with 2:1 pulley ratio
// Common in older vehicles and light trucks
static const PROGMEM RPMCalibration rpm_12p_2to1_cal = {
    .poles = 12,             // 12-pole alternator
    .pulley_ratio = 2.0,     // 2:1 alternator to engine ratio
    .calibration_mult = 1.0, // No fine-tuning by default
    .timeout_ms = 2000,      // 2 seconds without pulse = engine stopped
    .min_rpm = 100,          // Minimum valid RPM (reject noise)
    .max_rpm = 10000         // Maximum valid RPM (reject spikes)
};

#endif // SENSOR_CALIBRATION_DATA_H
