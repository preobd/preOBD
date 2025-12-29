/*
 * divider.cpp - Voltage Divider Reading
 *
 * Implements voltage reading through a resistor divider network.
 * Commonly used for battery voltage monitoring where input voltage
 * exceeds the ADC reference voltage.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"

/**
 * Read voltage through resistor divider
 *
 * Measures voltage at the midpoint of a resistor divider and calculates
 * the original voltage before division.
 *
 * @param ptr  Pointer to Input structure to store voltage reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - r1, r2, correction factor, offset
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - Uses VOLTAGE_DIVIDER_RATIO from platform.h
 *
 * Formula: V = ADC * (AREF / ADC_MAX) * divider_ratio * correction + offset
 */
void readVoltageDivider(Input *ptr) {
    int reading = analogRead(ptr->pin);

    if (reading < 10) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float r1, r2, correction, offset;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        r1 = ptr->customCalibration.voltageDivider.r1;
        r2 = ptr->customCalibration.voltageDivider.r2;
        correction = ptr->customCalibration.voltageDivider.correction;
        offset = ptr->customCalibration.voltageDivider.offset;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
        // Read from preset calibration (PROGMEM)
        const VoltageDividerCalibration* cal = (const VoltageDividerCalibration*)ptr->presetCalibration;
        r1 = pgm_read_float(&cal->r1);
        r2 = pgm_read_float(&cal->r2);
        correction = pgm_read_float(&cal->correction);
        offset = pgm_read_float(&cal->offset);
    } else {
        // Use defaults from platform.h
        // Calculate r1 and r2 from VOLTAGE_DIVIDER_RATIO
        // If VOLTAGE_DIVIDER_RATIO = (r1 + r2) / r2, we can use any r2 and calculate r1
        r2 = 1000.0;  // Arbitrary base value
        r1 = (VOLTAGE_DIVIDER_RATIO - 1.0) * r2;
        correction = 1.0;
        offset = 0.0;
    }

    // Calculate divider ratio from resistor values
    float divider_ratio = (r1 + r2) / r2;

    // Calculate voltage: V = ADC * (AREF / ADC_MAX) * divider_ratio * correction + offset
    float voltage = (reading * AREF_VOLTAGE / (float)ADC_MAX_VALUE) * divider_ratio * correction + offset;

    ptr->value = voltage;
}
