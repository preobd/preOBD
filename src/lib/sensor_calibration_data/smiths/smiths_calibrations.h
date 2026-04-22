/*
 * smiths_calibrations.h - Smiths Instrument Sensor Calibration Data
 *
 * Calibration data for Smiths temperature and pressure senders used on
 * British classic cars: MG, Triumph, Jaguar, Austin-Healey, Mini, Morgan.
 *
 * Data source: Published resistance specifications, community bench measurements,
 * and cross-reference to aftermarket equivalent part numbers. These curves are
 * consistent across multiple sources for TT6811/TT4201; GTR104 is 3-point only.
 *
 * All Smiths senders are low-impedance (10–700Ω) — use SENSOR_BIAS_LOW_Z (100Ω position).
 */

#ifndef SMITHS_CALIBRATIONS_H
#define SMITHS_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== SMITHS TT6811 COOLANT TEMP SENDER =====
// Smiths TT6811: gauge sender for MG, Triumph TR4/TR6, Austin-Healey, Mini
// Electrical: single-wire NTC, body-grounded, drives Smiths temperature gauge
// Resistance decreases with temperature (NTC characteristic)
// Valid range: 70–120°C (6 points, 10°C steps)
// Source: Published Smiths specification sheet; confirmed across multiple sources
// Bias: SENSOR_BIAS_LOW_Z (100Ω) — sender range 30–128Ω
static const float smiths_tt6811_resistance[] PROGMEM = {
    128.0,  92.0,  70.0,  50.0,  38.0,  30.0
};
static const float smiths_tt6811_temperature[] PROGMEM = {
     70.0,  80.0,  90.0, 100.0, 110.0, 120.0
};

static const PROGMEM ThermistorLookupCalibration smiths_tt6811_temp_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = smiths_tt6811_resistance,
    .temperature_table = smiths_tt6811_temperature,
    .table_size = 6
};

// ===== SMITHS TT4201 COOLANT TEMP SENDER (JAGUAR E-TYPE) =====
// Smiths TT4201: gauge sender for Jaguar E-Type and related XK-engine cars
// Electrical: single-wire NTC, body-grounded, drives Smiths temperature gauge
// Resistance decreases with temperature (NTC characteristic)
// Valid range: 30–110°C (6 points)
// Source: Published Smiths specification sheet; consistent with E-Type FSM specs
// Bias: SENSOR_BIAS_LOW_Z (100Ω) — sender range 10–180Ω
static const float smiths_tt4201_resistance[] PROGMEM = {
    180.0, 156.0,  84.0,  49.4,  31.0,  10.0
};
static const float smiths_tt4201_temperature[] PROGMEM = {
     30.0,  40.0,  60.0,  80.0, 100.0, 110.0
};

static const PROGMEM ThermistorLookupCalibration smiths_tt4201_temp_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = smiths_tt4201_resistance,
    .temperature_table = smiths_tt4201_temperature,
    .table_size = 6
};

// ===== SMITHS GTR104 COOLANT TEMP SENDER (GENERIC BRITISH GAUGE) =====
// Smiths GTR104: generic gauge sender used across many British classics
// Electrical: single-wire NTC, body-grounded, drives Smiths/Jaeger temperature gauge
// Valid range: 20–100°C (3 points — limited data; accuracy estimated ±8°C)
// Source: Community bench measurements; only 3 reliable data points available
// WARNING: 3-point curve — accuracy is approximate between measured points.
//          For precision applications use TT6811 or TT4201 if those match your gauge.
// Bias: SENSOR_BIAS_LOW_Z (100Ω) — sender range 47.5–675Ω (cold end readable on 100Ω:
//   V = 5 × 100/775 = 0.65V → ~530 counts on 10-bit ADC, adequate resolution)
static const float smiths_gtr104_resistance[] PROGMEM = {
    675.0, 165.0,  47.5
};
static const float smiths_gtr104_temperature[] PROGMEM = {
     20.0,  60.0, 100.0
};

static const PROGMEM ThermistorLookupCalibration smiths_gtr104_temp_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = smiths_gtr104_resistance,
    .temperature_table = smiths_gtr104_temperature,
    .table_size = 3
};

// ===== SMITHS BP/ACP OIL PRESSURE SENDER =====
// Smiths oil pressure sender for BP and ACP gauges (MG, Triumph, many others)
// Electrical: variable-resistance sender, 240Ω @ 0 psi, decreasing to 20Ω @ 80 psi
// Full-scale pressure varies by application: 60–100 PSI depending on gauge
// Source: Published Smiths endpoint specifications (2 points confirmed)
// WARNING: Only endpoints are from the Smiths spec. Intermediate points below
//   are true linear R-vs-P interpolation — Smiths oil senders are typically
//   approximately linear but this is not guaranteed. Accuracy estimated ±10%
//   full scale between endpoints. Bench-verified points welcome (issue #141).
// Resistance stored ASCENDING (20→240Ω) with pressure DESCENDING (80→0 psi)
// to satisfy readPressureTable()'s interpolateAscending() requirement.
// Bias: SENSOR_BIAS_LOW_Z (100Ω) — sender range 20–240Ω
static const float smiths_oil_bp_resistance[] PROGMEM = {
     20.0,  56.6,  93.4, 130.0, 166.6, 203.4, 240.0
};
static const float smiths_oil_bp_pressure[] PROGMEM = {
     80.0,  66.7,  53.3,  40.0,  26.7,  13.3,   0.0
};

static const PROGMEM PressureTableCalibration smiths_oil_bp_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = smiths_oil_bp_resistance,
    .pressure_table = smiths_oil_bp_pressure,
    .table_size = 7
};

#endif // SMITHS_CALIBRATIONS_H
