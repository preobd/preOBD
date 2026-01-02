/*
 * generic_calibrations.h - Generic Sensor Calibration Data
 *
 * Contains calibrations for generic/aftermarket sensors without specific
 * manufacturer designation. Includes common automotive sensors for pressure,
 * temperature, and speed.
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

// ===== SPEED SENSOR CALIBRATION =====

// Generic Hall Effect Speed Sensor calibration
// Compatible with VDO (YBE100530), OEM sensors, and generic 3-wire hall effect sensors
// User must determine pulses_per_rev empirically by counting teeth or testing
// This provides a reasonable starting point for common configurations
static const PROGMEM SpeedCalibration hall_speed_sensor_cal = {
    .pulses_per_rev = 100,          // Common transmission gear tooth count (user must verify)
    .tire_circumference_mm = 2000,  // ~205/55R16 tire (user should measure actual tire)
    .final_drive_ratio = 3.73,      // Common differential ratio (user must verify)
    .calibration_mult = 1.0,        // Fine-tuning multiplier (adjust after testing)
    .timeout_ms = 2000,             // 2 seconds without pulse = stopped
    .max_speed_kph = 300            // Maximum valid speed (safety check)
};

#endif // GENERIC_CALIBRATIONS_H
