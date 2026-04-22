/*
 * sw_calibrations.h - Stewart-Warner Sensor Calibration Data
 *
 * Calibration data for Stewart-Warner pressure and fuel level senders used on
 * US hot rod, muscle car, and classic truck applications.
 *
 * Data source: Published Stewart-Warner specifications and community measurements.
 * Note: SW temperature senders use a proprietary resistance curve matched to their
 * gauge movement — insufficient R-vs-T data exists to add reliably. Use a generic
 * NTC sensor type if retrofitting SW temp gauges with preOBD.
 *
 * All SW senders are low-impedance (10–240Ω) — use BIAS_LOW_Z (100Ω position).
 */

#ifndef SW_CALIBRATIONS_H
#define SW_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== STEWART-WARNER OIL PRESSURE SENDER =====
// SW oil pressure sender: 240Ω @ 0 psi, 33.5Ω @ 80 psi
// Used across Stewart-Warner oil pressure gauge applications
// Source: Stewart-Warner published endpoint specifications (2 points confirmed)
// WARNING: Only endpoints are from the SW spec. Intermediate points below are
//   a true linear R-vs-P interpolation — real SW senders may be piecewise or
//   slightly nonlinear. Accuracy estimated ±10% full scale between endpoints.
//   Bench-verified intermediate points would improve this (see issue #141).
// Resistance stored ASCENDING (33.5→240Ω) with pressure DESCENDING (80→0 psi)
// to satisfy readPressureTable()'s interpolateAscending() requirement.
// Bias: BIAS_LOW_Z (100Ω) — sender range 33.5–240Ω
static const float sw_oil_pressure_resistance[] PROGMEM = {
     33.5,  85.1, 136.8, 188.4, 214.2, 240.0
};
static const float sw_oil_pressure_pressure[] PROGMEM = {
     80.0,  60.0,  40.0,  20.0,  10.0,   0.0
};

static const PROGMEM PressureTableCalibration sw_oil_pressure_cal = {
    .bias_resistor = BIAS_LOW_Z,
    .resistance_table = sw_oil_pressure_resistance,
    .pressure_table = sw_oil_pressure_pressure,
    .table_size = 6
};

// ===== STEWART-WARNER FUEL LEVEL SENDER (240-33Ω) =====
// SW fuel tank float sender: 240Ω at empty, 33Ω at full (descending)
// Used with Stewart-Warner fuel level gauges on US classics and hot rods
// Source: Stewart-Warner published endpoint specifications (2 points confirmed)
// WARNING: Only endpoints are from the SW spec. Intermediate points are
//   true linear R-vs-level interpolation — real float senders are usually
//   approximately linear in the 20-80% range but may deviate near empty/full.
//   Accuracy estimated ±10% fuel level between endpoints.
// Stored DESCENDING (240→33Ω) with level ASCENDING (0→100%) per LevelTableCalibration
// convention for descending senders (ascending = false).
// Bias: BIAS_LOW_Z (100Ω) — sender range 33–240Ω
static const float sw_fuel_level_240_resistance[] PROGMEM = {
    240.0, 198.6, 157.2, 115.8,  74.4,  33.0
};
static const float sw_fuel_level_240_level[] PROGMEM = {
      0.0,  20.0,  40.0,  60.0,  80.0, 100.0
};

static const PROGMEM LevelTableCalibration sw_fuel_level_240_cal = {
    .bias_resistor = BIAS_LOW_Z,
    .resistance_table = sw_fuel_level_240_resistance,
    .level_table = sw_fuel_level_240_level,
    .table_size = 6,
    .ascending = false
};

// ===== FORD/CHRYSLER FUEL LEVEL SENDER (73-10Ω) =====
// Ford and Chrysler fuel tank float sender: 73Ω at empty, 10Ω at full
// Commonly found on 1960s–1980s Ford and Chrysler/Mopar vehicles;
// also available from Stewart-Warner as a compatible replacement
// Source: Stewart-Warner published cross-reference endpoints (2 points confirmed)
// WARNING: Only endpoints are from the SW spec. Intermediate points are
//   true linear R-vs-level interpolation. Accuracy estimated ±10% fuel level.
// Stored DESCENDING (73→10Ω) with level ASCENDING (0→100%) per LevelTableCalibration
// convention for descending senders (ascending = false).
// Bias: BIAS_LOW_Z (100Ω) — sender range 10–73Ω
static const float sw_ford_fuel_level_resistance[] PROGMEM = {
     73.0,  60.4,  47.8,  35.2,  22.6,  10.0
};
static const float sw_ford_fuel_level_level[] PROGMEM = {
      0.0,  20.0,  40.0,  60.0,  80.0, 100.0
};

static const PROGMEM LevelTableCalibration sw_ford_fuel_level_cal = {
    .bias_resistor = BIAS_LOW_Z,
    .resistance_table = sw_ford_fuel_level_resistance,
    .level_table = sw_ford_fuel_level_level,
    .table_size = 6,
    .ascending = false
};

#endif // SW_CALIBRATIONS_H
