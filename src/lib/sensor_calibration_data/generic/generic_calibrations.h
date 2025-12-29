/*
 * generic_calibrations.h - Generic Sensor Calibration Data
 *
 * Contains calibrations for generic/aftermarket sensors without specific
 * manufacturer designation. Includes common automotive pressure and
 * temperature sensor specifications.
 */

#ifndef GENERIC_CALIBRATIONS_H
#define GENERIC_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== PRESSURE SENSOR CALIBRATIONS =====

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

// Generic 0-150 PSI (0-10.34 bar) linear pressure sensor (0.5V-4.5V)
// Common for oil pressure and fuel pressure monitoring
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration generic_pressure_150psi_cal = {
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

#endif // GENERIC_CALIBRATIONS_H
