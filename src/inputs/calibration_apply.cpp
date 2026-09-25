/*
 * calibration_apply.cpp - Validated custom-calibration writes
 *
 * See calibration_apply.h. Ranges here are the ones the CLI has always enforced
 * (SET <pin> CAL ...); they moved out of cmd_set.cpp so JSON import enforces the
 * same ones (#373).
 */

#include "calibration_apply.h"
#include "../core/platform.h"   // SYSTEM_VOLTAGE

// Bias resistor sanity: 10R to 10MR covers every practical thermistor /
// resistive-sender bias resistor.
static const float BIAS_MIN_OHMS = 10.0f;
static const float BIAS_MAX_OHMS = 10000000.0f;

CalApplyStatus applyCalSteinhart(Input* input, float bias_r, float a, float b, float c) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_THERMISTOR_STEINHART) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (bias_r <= 0) return CAL_APPLY_ERR_BIAS_POSITIVE;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_THERMISTOR_STEINHART;
    input->customCalibration.steinhart.bias_resistor = bias_r;
    input->customCalibration.steinhart.steinhart_a = a;
    input->customCalibration.steinhart.steinhart_b = b;
    input->customCalibration.steinhart.steinhart_c = c;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalBeta(Input* input, float bias_r, float beta, float r0, float t0) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_THERMISTOR_BETA) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (bias_r <= 0) return CAL_APPLY_ERR_BIAS_POSITIVE;
    if (r0 <= 0) return CAL_APPLY_ERR_R0_POSITIVE;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_THERMISTOR_BETA;
    input->customCalibration.beta.bias_resistor = bias_r;
    input->customCalibration.beta.beta = beta;
    input->customCalibration.beta.r0 = r0;
    input->customCalibration.beta.t0 = t0;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalPressurePoly(Input* input, float bias_r, float a, float b, float c) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_PRESSURE_POLYNOMIAL) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (bias_r <= 0) return CAL_APPLY_ERR_BIAS_POSITIVE;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_PRESSURE_POLYNOMIAL;
    input->customCalibration.pressurePolynomial.bias_resistor = bias_r;
    input->customCalibration.pressurePolynomial.poly_a = a;
    input->customCalibration.pressurePolynomial.poly_b = b;
    input->customCalibration.pressurePolynomial.poly_c = c;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalLinear(Input* input, float vmin, float vmax,
                              float output_min, float output_max) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_LINEAR) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (vmin >= vmax) return CAL_APPLY_ERR_VOLTAGE_ORDER;
    if (vmin < 0.0f || vmax > SYSTEM_VOLTAGE) return CAL_APPLY_ERR_VOLTAGE_RANGE;
    if (output_min >= output_max) return CAL_APPLY_ERR_OUTPUT_ORDER;
    if (output_min < 0.0f) return CAL_APPLY_ERR_OUTPUT_MIN;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_LINEAR;
    input->customCalibration.pressureLinear.voltage_min = vmin;
    input->customCalibration.pressureLinear.voltage_max = vmax;
    input->customCalibration.pressureLinear.output_min = output_min;
    input->customCalibration.pressureLinear.output_max = output_max;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalBias(Input* input, float bias_r) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;

    // Only these four read a bias resistor out of the override. CAL_PRESSURE_TABLE
    // and CAL_LEVEL_TABLE deliberately are NOT here: their read paths take the bias
    // from the PROGMEM preset unconditionally (sensors/pressure/table.cpp:46,
    // sensors/level/table.cpp:46), so an override would be silently inert.
    if (input->calibrationType != CAL_THERMISTOR_STEINHART &&
        input->calibrationType != CAL_THERMISTOR_TABLE &&
        input->calibrationType != CAL_THERMISTOR_BETA &&
        input->calibrationType != CAL_PRESSURE_POLYNOMIAL) {
        return CAL_APPLY_ERR_BIAS_TYPE;
    }
    if (bias_r < BIAS_MIN_OHMS || bias_r > BIAS_MAX_OHMS) {
        return CAL_APPLY_ERR_BIAS_LIMITS;
    }

    // Seed the custom override from the preset before diverging, so overriding
    // only the bias preserves the rest of the curve. Steinhart/Beta/Polynomial
    // read every coefficient from the custom struct once useCustomCalibration is
    // set; without seeding, A/B/C (etc.) stay zero and the equation degenerates
    // to 1/T = 0 -> INF. Table keeps its lookup arrays in PROGMEM, so only its
    // bias lives in the override -- nothing to seed. (Skip if already custom: the
    // user may have tuned coefficients we must not clobber.)
    if (!input->flags.useCustomCalibration && input->presetCalibration != nullptr) {
        switch (input->calibrationType) {
            case CAL_THERMISTOR_STEINHART:
                memcpy_P(&input->customCalibration.steinhart, input->presetCalibration,
                         sizeof(ThermistorSteinhartCalibration));
                break;
            case CAL_THERMISTOR_BETA:
                memcpy_P(&input->customCalibration.beta, input->presetCalibration,
                         sizeof(BetaCalibration));
                break;
            case CAL_PRESSURE_POLYNOMIAL:
                memcpy_P(&input->customCalibration.pressurePolynomial, input->presetCalibration,
                         sizeof(PressurePolynomialCalibration));
                break;
            default:
                break;  // Table: only bias is overridable; nothing to seed.
        }
    } else if (!input->flags.useCustomCalibration) {
        // No preset (the generic NTC sensors, #465): seed from the defaults the
        // reader would otherwise use, not from a zeroed/stale union — A=B=C=0
        // reads INF, beta=r0=t0=0 reads -273.15 °C.
        if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
            input->customCalibration.steinhart.steinhart_a = NTC_DEFAULT_STEINHART_A;
            input->customCalibration.steinhart.steinhart_b = NTC_DEFAULT_STEINHART_B;
            input->customCalibration.steinhart.steinhart_c = NTC_DEFAULT_STEINHART_C;
        } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
            input->customCalibration.beta.beta = NTC_DEFAULT_BETA;
            input->customCalibration.beta.r0 = NTC_DEFAULT_R0_OHMS;
            input->customCalibration.beta.t0 = NTC_DEFAULT_T0_C;
        }
    }

    input->flags.useCustomCalibration = true;

    if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
        input->customCalibration.steinhart.bias_resistor = bias_r;
    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
        input->customCalibration.beta.bias_resistor = bias_r;
    } else if (input->calibrationType == CAL_THERMISTOR_TABLE) {
        input->customCalibration.lookup.bias_resistor = bias_r;
    } else {  // CAL_PRESSURE_POLYNOMIAL
        input->customCalibration.pressurePolynomial.bias_resistor = bias_r;
    }
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalRpm(Input* input, uint8_t poles, float pulley_ratio,
                           float calibration_mult, uint16_t timeout_ms,
                           uint16_t min_rpm, uint16_t max_rpm) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_RPM) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (poles < 2 || poles > 32) return CAL_APPLY_ERR_POLES;
    if (pulley_ratio < 0.5f || pulley_ratio > 10.0f) return CAL_APPLY_ERR_PULLEY_RATIO;
    if (calibration_mult < 0.5f || calibration_mult > 2.0f) return CAL_APPLY_ERR_CAL_MULT;
    if (timeout_ms < 100 || timeout_ms > 10000) return CAL_APPLY_ERR_TIMEOUT;
    if (min_rpm >= max_rpm) return CAL_APPLY_ERR_RPM_ORDER;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_RPM;
    input->customCalibration.rpm.poles = poles;
    input->customCalibration.rpm.pulley_ratio = pulley_ratio;
    input->customCalibration.rpm.calibration_mult = calibration_mult;
    input->customCalibration.rpm.timeout_ms = timeout_ms;
    input->customCalibration.rpm.min_rpm = min_rpm;
    input->customCalibration.rpm.max_rpm = max_rpm;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalVoltageDivider(Input* input, float r1, float r2,
                                      float correction, float offset) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_VOLTAGE_DIVIDER) return CAL_APPLY_ERR_SENSOR_TYPE;
    // r1 may be 0 (no divider, ratio 1.0); r2 divides, so it must be positive.
    if (r1 < 0.0f || r1 > BIAS_MAX_OHMS) return CAL_APPLY_ERR_DIVIDER_R;
    if (r2 <= 0.0f || r2 > BIAS_MAX_OHMS) return CAL_APPLY_ERR_DIVIDER_R;
    if (correction < 0.1f || correction > 10.0f) return CAL_APPLY_ERR_DIVIDER_SCALE;
    if (offset < -10.0f || offset > 10.0f) return CAL_APPLY_ERR_DIVIDER_SCALE;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_VOLTAGE_DIVIDER;
    input->customCalibration.voltageDivider.r1 = r1;
    input->customCalibration.voltageDivider.r2 = r2;
    input->customCalibration.voltageDivider.correction = correction;
    input->customCalibration.voltageDivider.offset = offset;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalSpeed(Input* input, uint8_t pulses_per_rev,
                             uint16_t tire_circumference_mm, float final_drive_ratio,
                             float calibration_mult, uint16_t timeout_ms,
                             uint16_t max_speed_kph) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_SPEED) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (pulses_per_rev < 1 || pulses_per_rev > 250) return CAL_APPLY_ERR_SPEED_PPR;
    if (tire_circumference_mm < 500 || tire_circumference_mm > 5000) return CAL_APPLY_ERR_SPEED_TIRE;
    if (final_drive_ratio < 0.5f || final_drive_ratio > 10.0f) return CAL_APPLY_ERR_SPEED_RATIO;
    if (calibration_mult < 0.5f || calibration_mult > 2.0f) return CAL_APPLY_ERR_SPEED_MULT;
    if (timeout_ms < 100 || timeout_ms > 10000) return CAL_APPLY_ERR_SPEED_TIMEOUT;
    if (max_speed_kph < 50 || max_speed_kph > 500) return CAL_APPLY_ERR_SPEED_MAX;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_SPEED;
    input->customCalibration.speed.pulses_per_rev = pulses_per_rev;
    input->customCalibration.speed.tire_circumference_mm = tire_circumference_mm;
    input->customCalibration.speed.final_drive_ratio = final_drive_ratio;
    input->customCalibration.speed.calibration_mult = calibration_mult;
    input->customCalibration.speed.timeout_ms = timeout_ms;
    input->customCalibration.speed.max_speed_kph = max_speed_kph;
    return CAL_APPLY_OK;
}

CalApplyStatus applyCalCanImport(Input* input, uint16_t source_can_id, uint8_t source_pid,
                                 uint8_t data_offset, uint8_t data_length, bool is_big_endian,
                                 float scale_factor, float offset, uint16_t timeout_ms) {
    if (input == nullptr) return CAL_APPLY_ERR_NO_INPUT;
    if (input->calibrationType != CAL_CAN_IMPORT) return CAL_APPLY_ERR_SENSOR_TYPE;
    if (data_length < 1 || data_length > 4) return CAL_APPLY_ERR_CAN_LENGTH;
    if (data_offset > 7) return CAL_APPLY_ERR_CAN_OFFSET;
    // 0 is a legitimate value: CANSensorCalibration.timeout_ms treats 0 as
    // "use CAN_DEFAULT_TIMEOUT_MS" (sensors/calibration_types.h). Anything else must fall
    // in the same range SET <pin> CAN_TIMEOUT has always enforced.
    if (timeout_ms != 0 && (timeout_ms < 100 || timeout_ms > 30000)) return CAL_APPLY_ERR_CAN_TIMEOUT;

    input->flags.useCustomCalibration = true;
    input->calibrationType = CAL_CAN_IMPORT;
    input->customCalibration.can.source_can_id = source_can_id;
    input->customCalibration.can.source_pid = source_pid;
    input->customCalibration.can.data_offset = data_offset;
    input->customCalibration.can.data_length = data_length;
    input->customCalibration.can.is_big_endian = is_big_endian;
    input->customCalibration.can.scale_factor = scale_factor;
    input->customCalibration.can.offset = offset;
    input->customCalibration.can.timeout_ms = timeout_ms;
    return CAL_APPLY_OK;
}
