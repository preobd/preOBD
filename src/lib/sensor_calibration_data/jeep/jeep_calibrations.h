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
#include "../../../config.h"

// ===== JEEP 4.0L COOLANT TEMP GAUGE SENDER =====
// Mopar 56027012 family: gauge sender for 1984-1996 XJ Cherokee 4.0L
// Aliases: SMP TS158, Wells TU108, NTK EF0008
// Electrical: single-wire NTC, body-grounded, drives analog gauge (NOT ECU)
// Valid range: 0-120°C in 5°C steps (25 points)
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
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = jeep40_temp_resistance,
    .temperature_table = jeep40_temp_temperature,
    .table_size = 25
};

// ===== JEEP RENIX ECU COOLANT TEMP SENSOR =====
// Renix EFI ECU CTS for 1987-1990 XJ Cherokee 4.0L (DISTINCT from gauge sender)
// Aliases: SMP TX18, Renix-era Mopar variants
// Electrical: 2-wire NTC, 5V pull-up in ECU; higher cold resistance than gauge sender
// Valid range: -20-180°C (11 points)
static const float renix_cts_resistance[] PROGMEM = {
    9400, 5800, 3700, 2400, 1600, 1100, 750, 520, 370, 270, 200
};
static const float renix_cts_temperature[] PROGMEM = {
    -20,   0,   20,   40,   60,   80,  100, 120, 140, 160, 180
};

static const PROGMEM ThermistorLookupCalibration renix_cts_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
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
static const float jeep40_oil_resistance[] PROGMEM = {
     2,  5, 10, 15, 20, 25, 30, 36, 42,
    48, 54, 60, 66, 72, 78, 84, 90
};
static const float jeep40_oil_pressure[] PROGMEM = {
    80, 75, 70, 65, 60, 55, 50, 45, 40,
    35, 30, 25, 20, 15, 10,  5,  0
};

static const PROGMEM PressureTableCalibration jeep40_oil_gauge_cal = {
    .bias_resistor = DEFAULT_BIAS_RESISTOR,
    .resistance_table = jeep40_oil_resistance,
    .pressure_table = jeep40_oil_pressure,
    .table_size = 17
};

#endif // JEEP_CALIBRATIONS_H
