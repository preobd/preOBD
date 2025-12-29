/*
 * linear_sensor.cpp - Generic Linear Sensor Reading
 *
 * Implements linear voltage-to-value conversion for sensors with linear
 * voltage output characteristics. Used by both:
 * - Linear pressure sensors (generic boost, 150 PSI, AEM, MPX4250AP)
 * - Linear temperature sensors (generic temp linear)
 *
 * The sensor type (pressure/temperature) is determined by the measurementType
 * field in the Input structure, not by this function.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"

/**
 * Read linear sensor (generic method)
 *
 * Works for any linear sensor: temperature, pressure, voltage, etc.
 * Units are determined by the measurementType field in the Input structure.
 *
 * @param ptr  Pointer to Input structure containing sensor configuration
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - from EEPROM/serial config mode
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - 0.5V-4.5V → 0-5 bar (common automotive pressure sensor)
 */
void readLinearSensor(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float V_min, V_max, output_min, output_max;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_LINEAR) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        V_min = ptr->customCalibration.pressureLinear.voltage_min;
        V_max = ptr->customCalibration.pressureLinear.voltage_max;
        output_min = ptr->customCalibration.pressureLinear.output_min;
        output_max = ptr->customCalibration.pressureLinear.output_max;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_LINEAR) {
        // Read from preset calibration (PROGMEM)
        const LinearCalibration* cal = (const LinearCalibration*)ptr->presetCalibration;
        V_min = pgm_read_float(&cal->voltage_min);
        V_max = pgm_read_float(&cal->voltage_max);
        output_min = pgm_read_float(&cal->output_min);
        output_max = pgm_read_float(&cal->output_max);
    } else {
        // Default: 0.5V-4.5V → 0-5 bar (common automotive pressure sensor)
        V_min = 0.5;
        V_max = 4.5;
        output_min = 0.0;
        output_max = 5.0;
    }

    // Convert ADC reading to voltage
    float voltage = reading * (AREF_VOLTAGE / (float)ADC_MAX_VALUE);

    // Clamp voltage to valid range
    if (voltage < V_min) voltage = V_min;
    if (voltage > V_max) voltage = V_max;

    // Linear interpolation: Y = (V - V_min) / (V_max - V_min) * (Y_max - Y_min) + Y_min
    float outputValue = ((voltage - V_min) / (V_max - V_min)) * (output_max - output_min) + output_min;

    ptr->value = outputValue;  // Store in base units (°C for temp, bar for pressure, etc.)
}
