/*
 * vdo_calibrations.h - VDO Sensor Calibration Data
 *
 * Contains all VDO sensor calibrations stored in PROGMEM (flash memory).
 * Includes thermistor lookup tables, Steinhart-Hart coefficients, and
 * pressure sensor polynomial calibrations.
 */

#ifndef VDO_CALIBRATIONS_H
#define VDO_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== VDO 120°C (323 095) TABLE DATA =====
// Source: VDO datasheet and empirical measurements
// Valid range: 0°C to 150°C (extrapolated beyond 120°C)
// Resistance vs Temperature lookup table for VDO 120°C (323 095) sensors
static const float vdo120_resistance[] PROGMEM = {
    1743.15, 1364.07, 1075.63, 850.09, 676.95, 543.54, 439.29, 356.64, 291.46,
    239.56, 197.29, 161.46, 134.03, 113.96, 97.05, 82.36, 70.12, 59.73, 51.21,
    44.32, 38.47, 33.4, 29.12, 25.53, 22.44, 19.75, 17.44, 15.46, 13.75, 12.26, 10.96
};

static const float vdo120_temperature[] PROGMEM = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
    105, 110, 115, 120, 125, 130, 135, 140, 145, 150
};

// ===== VDO 150°C (323 057) TABLE DATA =====
// Resistance vs Temperature lookup table for VDO 150°C (323 057) sensors
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

// ===== THERMISTOR TABLE CALIBRATIONS =====

// VDO 120°C (323 095) using lookup table
static const PROGMEM ThermistorLookupCalibration vdo120_lookup_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo120_resistance,
    .temperature_table = vdo120_temperature,
    .table_size = 31
};

// VDO 150°C (323 057) using lookup table
static const PROGMEM ThermistorLookupCalibration vdo150_lookup_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo150_resistance,
    .temperature_table = vdo150_temperature,
    .table_size = 37
};

// ===== THERMISTOR STEINHART-HART CALIBRATIONS =====

// VDO 120°C (323 095) using Steinhart-Hart (curve-fitted from lookup table)
// Steinhart-Hart coefficients curve-fitted from VDO resistance table
// Accuracy: ±1°C across 20-120°C range
// Bias resistor: Set by DEFAULT_BIAS_RESISTOR in config.h
static const PROGMEM ThermistorSteinhartCalibration vdo120_steinhart_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .steinhart_a = 1.764445997570e-03,
    .steinhart_b = 2.499534389889e-04,
    .steinhart_c = 6.773335597401e-08
};

// VDO 150°C (323 057) using Steinhart-Hart (curve-fitted from lookup table)
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

// VDO 5-bar (360 003) pressure sensor polynomial calibration
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

// VDO 2-bar (360 043) pressure sensor polynomial calibration
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

// ===== PRESSURE SENSOR TABLE DATA =====

// VDO 2-bar (360 043) table - Resistance in ASCENDING order
// Source: VDO datasheet
static const float vdo2bar_resistance[] PROGMEM = {
    10.0, 55.0, 100.0, 144.0, 168.0, 184.0
};
static const float vdo2bar_pressure[] PROGMEM = {
    0.0, 0.5, 1.0, 1.5, 1.8, 2.0
};

// VDO 5-bar (360 003) table - Resistance in ASCENDING order
// Source: VDO datasheet
static const float vdo5bar_resistance[] PROGMEM = {
    10.0, 48.0, 82.0, 116.0, 184.0
};
static const float vdo5bar_pressure[] PROGMEM = {
    0.0, 1.0, 2.0, 3.0, 5.0
};

// ===== PRESSURE TABLE CALIBRATIONS =====

// VDO 2-bar (360 043) using lookup table
static const PROGMEM PressureTableCalibration vdo2bar_table_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo2bar_resistance,
    .pressure_table = vdo2bar_pressure,
    .table_size = 6
};

// VDO 5-bar (360 003) using lookup table
static const PROGMEM PressureTableCalibration vdo5bar_table_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = vdo5bar_resistance,
    .pressure_table = vdo5bar_pressure,
    .table_size = 5
};

#endif // VDO_CALIBRATIONS_H
