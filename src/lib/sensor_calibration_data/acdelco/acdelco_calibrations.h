/*
 * acdelco_calibrations.h - AC Delco / GM OEM Sensor Calibration Data
 *
 * Calibration data for AC Delco temperature gauge senders used on GM vehicles.
 * Two distinct families exist based on era:
 *
 * Pre-1979 GM gauge sender (1967–1978): Low-impedance, 46–350Ω, compatible with
 *   SENSOR_BIAS_LOW_Z (100Ω position) — same family as VDO and pre-EFI Ford senders.
 *
 * 1979+ GM gauge sender (1979–1990): Higher-impedance, 240–1600Ω, requires
 *   SENSOR_BIAS_HIGH_Z (2.49kΩ position) — NOT compatible with the 100Ω VDO position.
 *
 * Data source: GM FSM resistance ranges and community bench measurements (GM Square Body
 * forums). These curves are community-derived, not factory precision data.
 * Accuracy estimated ±8°C due to limited data points.
 */

#ifndef ACDELCO_CALIBRATIONS_H
#define ACDELCO_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== AC DELCO PRE-1979 GM TEMP GAUGE SENDER (1967–1978) =====
// OE gauge sender for 1967–1978 GM trucks and cars (Chevy/GMC C/K, Blazer, etc.)
// Part numbers: various AC Delco / SMP equivalents of this era
// Electrical: single-wire NTC, body-grounded, drives analog dashboard gauge
// Valid range: approx 40–120°C (low R at hot = NTC characteristic)
// Source: GM FSM resistance ranges + GM Square Body community measurements
// WARNING: Limited community data — table uses FSM endpoints and interpolated midpoints.
//          Accuracy estimated ±8°C. Full bench-verified table pending.
// Bias: SENSOR_BIAS_LOW_Z (100Ω) — sender range 46–350Ω
static const float acdelco_pre79_resistance[] PROGMEM = {
    350.0, 230.0, 150.0,  95.0,  68.0,  46.0
};
static const float acdelco_pre79_temperature[] PROGMEM = {
     40.0,  60.0,  80.0, 100.0, 110.0, 120.0
};

static const PROGMEM ThermistorLookupCalibration acdelco_pre79_temp_cal = {
    .bias_resistor = SENSOR_BIAS_LOW_Z,
    .resistance_table = acdelco_pre79_resistance,
    .temperature_table = acdelco_pre79_temperature,
    .table_size = 6
};

// ===== AC DELCO 1979+ GM TEMP GAUGE SENDER (1979–1990) =====
// OE gauge sender for 1979–1990 GM trucks and cars (C/K, S10, Blazer, etc.)
// Part numbers: various AC Delco / SMP equivalents of this era
// Electrical: single-wire NTC, body-grounded, drives analog dashboard gauge
// Valid range: approx 20–120°C
// Source: GM FSM resistance ranges + GM Square Body community measurements
// NOTE: The 1979+ sender is HIGHER impedance than its predecessor. It requires
//       SENSOR_BIAS_HIGH_Z (2.49kΩ), NOT the 100Ω VDO/low-impedance position.
//       Wiring it to the 100Ω bias position will give severely compressed readings.
// WARNING: Limited community data — table uses FSM endpoints and interpolated midpoints.
//          Accuracy estimated ±8°C. Full bench-verified table pending.
// Bias: SENSOR_BIAS_HIGH_Z (2.49kΩ) — sender range 240–1600Ω
static const float acdelco_post79_resistance[] PROGMEM = {
    1600.0, 1100.0,  720.0,  480.0,  330.0,  240.0
};
static const float acdelco_post79_temperature[] PROGMEM = {
      20.0,   40.0,   60.0,   80.0,  100.0,  120.0
};

static const PROGMEM ThermistorLookupCalibration acdelco_post79_temp_cal = {
    .bias_resistor = SENSOR_BIAS_HIGH_Z,
    .resistance_table = acdelco_post79_resistance,
    .temperature_table = acdelco_post79_temperature,
    .table_size = 6
};

#endif // ACDELCO_CALIBRATIONS_H
