/*
 * jeep_calibrations.h - Jeep/AMC Sensor Calibration Data
 *
 * Calibration data for Jeep CJ/SJ (1970s-1980s) and XJ Cherokee 4.0L (1984-1996)
 * sensors. Covers both gauge-cluster senders and Renix EFI ECU sensors.
 *
 * Data source: FSM resistance tables, community bench measurements, and
 * cross-reference to aftermarket equivalent part numbers. These curves are
 * community-derived rather than factory-published precision data like VDO.
 *
 * Part number families: Multiple Mopar/aftermarket PNs alias to the same
 * curve — this is consistent with how Detroit-era sensors were sourced.
 */

#ifndef JEEP_CALIBRATIONS_H
#define JEEP_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../sensor_calibration_data.h"

// Jeep sensors use SENSOR_BIAS_LOW_Z (100Ω) or SENSOR_BIAS_HIGH_Z (2.49kΩ) depending on the
// sender family. Both are defined in sensor_calibration_data.h and map to fixed hardware
// positions on the preOBD PCB.

// ===== JEEP 4.0L COOLANT TEMP GAUGE SENDER =====
// Mopar 56027012 family: gauge sender for 1984-1996 XJ Cherokee 4.0L
// Aliases: SMP TS158, Wells TU108, NTK EF0008
// Electrical: single-wire NTC, body-grounded, drives analog gauge (NOT ECU)
// Valid range: 0-120°C in 5°C steps (25 points)
// NOTE: Despite being a "gauge sender," the 56027012 is a high-impedance NTC
// (135Ω @ 120°C to 7800Ω @ 0°C). It requires the 2.49kΩ bias position on
// the preOBD custom PCB, NOT the 100Ω VDO/low-impedance position.
// On Arduino/Teensy builds, recommended bias resistor: 2.2kΩ–2.49kΩ.
static const float jeep40_temp_resistance[] PROGMEM = {
    7800, 6200, 5000, 4000, 3200, 2600, 2100, 1700, 1400,
    1150,  950,  800,  680,  580,  500,  430,  370,  320,  280,
     245,  215,  190,  170,  150,  135
};
static const float jeep40_temp_temperature[] PROGMEM = {
      0,   5,  10,  15,  20,  25,  30,  35,  40,
     45,  50,  55,  60,  65,  70,  75,  80,  85,  90,
     95, 100, 105, 110, 115, 120
};

static const PROGMEM ThermistorLookupCalibration jeep40_temp_gauge_cal = {
    .bias_resistor = SENSOR_BIAS_HIGH_Z,
    .resistance_table = jeep40_temp_resistance,
    .temperature_table = jeep40_temp_temperature,
    .table_size = 25
};

// ===== JEEP RENIX ECU COOLANT TEMP SENSOR =====
// Renix EFI ECU CTS for 1987-1990 XJ Cherokee 4.0L (DISTINCT from gauge sender)
// Aliases: SMP TX18, Renix-era Mopar variants
// Electrical: 2-wire NTC, 5V pull-up in ECU; higher cold resistance than gauge sender
// Valid range: -20-180°C (11 points)
// Bias: 2.49kΩ position on preOBD custom PCB (high-impedance NTC, 200–9400Ω)
static const float renix_cts_resistance[] PROGMEM = {
    9400, 5800, 3700, 2400, 1600, 1100, 750, 520, 370, 270, 200
};
static const float renix_cts_temperature[] PROGMEM = {
    -20,   0,   20,   40,   60,   80,  100, 120, 140, 160, 180
};

static const PROGMEM ThermistorLookupCalibration renix_cts_cal = {
    .bias_resistor = SENSOR_BIAS_HIGH_Z,
    .resistance_table = renix_cts_resistance,
    .temperature_table = renix_cts_temperature,
    .table_size = 11
};

// ===== JEEP 4.0L OIL PRESSURE GAUGE SENDER =====
// Mopar 56026779 family: oil pressure sender for 1984-1996 XJ Cherokee 4.0L
// Aliases: SMP PS133, Wells PS131
// Electrical: variable-resistance sender, ~90Ω @ 0 psi, decreasing to ~2Ω @ 80 psi
// Resistance stored ASCENDING (2→90Ω) with pressure DESCENDING (80→0 psi)
// to satisfy readPressureTable()'s interpolateAscending() requirement.
// Valid range: 0-80 psi (17 points)
// Bias: 100Ω position on preOBD custom PCB (low-impedance sender, 2–90Ω)
static const float jeep40_oil_resistance[] PROGMEM = {
     2,  5, 10, 15, 20, 25, 30, 36, 42,
    48, 54, 60, 66, 72, 78, 84, 90
};
static const float jeep40_oil_pressure[] PROGMEM = {
    80, 75, 70, 65, 60, 55, 50, 45, 40,
    35, 30, 25, 20, 15, 10,  5,  0
};

static const PROGMEM PressureTableCalibration jeep40_oil_gauge_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = jeep40_oil_resistance,
    .pressure_table = jeep40_oil_pressure,
    .table_size = 17
};

// ===== JEEP CJ COOLANT TEMP GAUGE SENDER (1972-1986) =====
// AMC-era single-wire NTC, body-grounded gauge sender
// OE part: varies by year (Crown J3212002 and equivalents)
// Electrical: 9-73Ω low-resistance NTC, drives analog gauge via pulsed voltage regulator
// Source: AMC FSM resistance specs + community temperature correlation
// WARNING: Temperature mapping is approximate — FSM only documents gauge positions,
// not precise R-vs-T data. Accuracy estimated ±5°C.
// Bias: 100Ω position on preOBD custom PCB (low-impedance sender, 9–73Ω)
static const float jeep_cj_temp_resistance[] PROGMEM = {
    73.0, 36.0, 20.0, 13.0, 9.0
};
static const float jeep_cj_temp_temperature[] PROGMEM = {
    49.0, 82.0, 99.0, 116.0, 127.0
};

static const PROGMEM ThermistorLookupCalibration jeep_cj_temp_gauge_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = jeep_cj_temp_resistance,
    .temperature_table = jeep_cj_temp_temperature,
    .table_size = 5
};

// ===== JEEP CJ OIL PRESSURE SENDER — 1" THIN GAUGE (~1982-1986) =====
// Crown #3212004 and equivalents
// Electrical: 9-70Ω, resistance decreases with pressure
// Source: Bench-tested Crown #3212004 (JeepForum), confirmed against FSM spec ranges
// Resistance stored ASCENDING (9→70Ω) with pressure DESCENDING (80→0 psi)
// to satisfy readPressureTable()'s interpolateAscending() requirement.
// Bias: 100Ω position on preOBD custom PCB (low-impedance sender, 9–70Ω)
static const float jeep_cj_oil_thin_resistance[] PROGMEM = {
     9.0, 14.0, 23.0, 33.0, 70.0
};
static const float jeep_cj_oil_thin_pressure[] PROGMEM = {
    80.0, 60.0, 40.0, 20.0,  0.0
};

static const PROGMEM PressureTableCalibration jeep_cj_oil_thin_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = jeep_cj_oil_thin_resistance,
    .pressure_table = jeep_cj_oil_thin_pressure,
    .table_size = 5
};

// ===== JEEP CJ OIL PRESSURE SENDER — 2" DEEP GAUGE (1976-~1982) =====
// OE sender for 2" deep oil pressure gauge
// Electrical: 33-240Ω, resistance decreases with pressure
// Source: 1979 Jeep CJ Factory Service Manual (FSM spec midpoints)
// Resistance stored ASCENDING (33.5→240Ω) with pressure DESCENDING (80→0 psi)
// to satisfy readPressureTable()'s interpolateAscending() requirement.
// Bias: 100Ω position on preOBD custom PCB (sender max 240Ω; at 0 psi:
//   V = 5 × 100/340 = 1.47V → 1202 counts on 10-bit ADC, adequate resolution)
static const float jeep_cj_oil_deep_resistance[] PROGMEM = {
     33.5,  67.0, 103.0, 153.0, 240.0
};
static const float jeep_cj_oil_deep_pressure[] PROGMEM = {
    80.0, 60.0, 40.0, 20.0,  0.0
};

static const PROGMEM PressureTableCalibration jeep_cj_oil_deep_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = jeep_cj_oil_deep_resistance,
    .pressure_table = jeep_cj_oil_deep_pressure,
    .table_size = 5
};

// ===== JEEP CJ FUEL LEVEL SENDER (1972-1986) =====
// AMC-era fuel tank float sender (potentiometer-type)
// Electrical: 10-73Ω, resistance decreases as fuel level rises (Full = low R)
// Source: AMC Factory Service Manual (3 confirmed points: Empty/Half/Full)
// Quarter-tank points (25%, 75%) are linearly interpolated — float sender
// resistance is approximately linear with float arm position.
// Bias: 100Ω position on preOBD custom PCB (low-impedance sender, 10–73Ω)
// Stored DESCENDING (73→10Ω) with level ASCENDING (0→100%) per LevelTableCalibration
// convention for descending senders (ascending = false).
static const float jeep_cj_fuel_resistance[] PROGMEM = {
    73.0, 48.0, 23.0, 16.5, 10.0
};
static const float jeep_cj_fuel_level[] PROGMEM = {
     0.0, 25.0, 50.0, 75.0, 100.0
};

static const PROGMEM LevelTableCalibration jeep_cj_fuel_level_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = jeep_cj_fuel_resistance,
    .level_table = jeep_cj_fuel_level,
    .table_size = 5,
    .ascending = false
};

#endif // JEEP_CALIBRATIONS_H
