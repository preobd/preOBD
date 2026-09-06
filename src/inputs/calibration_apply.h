/*
 * calibration_apply.h - Validated custom-calibration writes (single source of truth)
 *
 * Every path that writes `Input::customCalibration` + `flags.useCustomCalibration`
 * goes through these functions, so the CLI (`SET <pin> CAL ...`, cmd_set.cpp) and
 * JSON import (`JSON IMPORT`, json_config.cpp) cannot disagree on what counts as a
 * valid calibration (#373).
 *
 * The core is deliberately message-free: it validates, writes on success, and
 * returns a status the caller renders in its own voice (the CLI prints its usual
 * `ERROR: ...` text; the import path fails the record). It touches nothing but the
 * Input it is handed, which also keeps it host-testable.
 *
 * Contract: on any status other than CAL_APPLY_OK the Input is left untouched.
 */

#ifndef CALIBRATION_APPLY_H
#define CALIBRATION_APPLY_H

#include <Arduino.h>
#include "input.h"

enum CalApplyStatus : uint8_t {
    CAL_APPLY_OK = 0,
    CAL_APPLY_ERR_NO_INPUT,          // null Input
    CAL_APPLY_ERR_BIAS_POSITIVE,     // bias resistor must be > 0
    CAL_APPLY_ERR_BIAS_LIMITS,       // bias resistor outside 10R..10M
    CAL_APPLY_ERR_BIAS_TYPE,         // this calibration type has no bias resistor
    CAL_APPLY_ERR_R0_POSITIVE,       // beta r0 must be > 0
    CAL_APPLY_ERR_VOLTAGE_ORDER,     // vmin >= vmax
    CAL_APPLY_ERR_VOLTAGE_RANGE,     // outside 0..SYSTEM_VOLTAGE
    CAL_APPLY_ERR_OUTPUT_ORDER,      // output min >= output max
    CAL_APPLY_ERR_OUTPUT_MIN,        // output min < 0
    CAL_APPLY_ERR_POLES,             // poles outside 2..32
    CAL_APPLY_ERR_PULLEY_RATIO,      // pulley ratio outside 0.5..10.0
    CAL_APPLY_ERR_CAL_MULT,          // calibration multiplier outside 0.5..2.0
    CAL_APPLY_ERR_TIMEOUT,           // timeout outside 100..10000 ms
    CAL_APPLY_ERR_RPM_ORDER,         // min_rpm >= max_rpm
    CAL_APPLY_ERR_DIVIDER_R,         // divider resistor out of range
    CAL_APPLY_ERR_DIVIDER_SCALE      // divider correction/offset out of range
};

// Thermistor, Steinhart-Hart. Sets calibrationType = CAL_THERMISTOR_STEINHART.
CalApplyStatus applyCalSteinhart(Input* input, float bias_r, float a, float b, float c);

// Thermistor, Beta equation. Sets calibrationType = CAL_THERMISTOR_BETA.
CalApplyStatus applyCalBeta(Input* input, float bias_r, float beta, float r0, float t0);

// Polynomial pressure (VDO). Sets calibrationType = CAL_PRESSURE_POLYNOMIAL.
CalApplyStatus applyCalPressurePoly(Input* input, float bias_r, float a, float b, float c);

// Linear sensor (voltage -> engineering units). Sets calibrationType = CAL_LINEAR.
CalApplyStatus applyCalLinear(Input* input, float vmin, float vmax,
                              float output_min, float output_max);

// Bias resistor only — keeps the input's existing calibrationType, which must be
// one that reads a bias resistor from the override (Steinhart / Beta / thermistor
// Table / Pressure Polynomial). Seeds the override from the PROGMEM preset first
// when the input was still on the preset, so overriding the bias alone doesn't
// zero the rest of the curve.
CalApplyStatus applyCalBias(Input* input, float bias_r);

// W-phase / tach RPM. Sets calibrationType = CAL_RPM.
CalApplyStatus applyCalRpm(Input* input, uint8_t poles, float pulley_ratio,
                           float calibration_mult, uint16_t timeout_ms,
                           uint16_t min_rpm, uint16_t max_rpm);

// Voltage divider. Sets calibrationType = CAL_VOLTAGE_DIVIDER.
// No CLI leaf writes this today (the divider override is reachable only from a
// stored config); JSON import uses it so an exported divider round-trips.
CalApplyStatus applyCalVoltageDivider(Input* input, float r1, float r2,
                                      float correction, float offset);

#endif // CALIBRATION_APPLY_H
