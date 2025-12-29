/*
 * steinhart.cpp - Thermistor Reading (Steinhart-Hart Equation)
 *
 * Implements thermistor temperature reading using the Steinhart-Hart equation.
 * More accurate than Beta equation across wider temperature ranges.
 *
 * Steinhart-Hart equation: 1/T(K) = A + B*ln(R) + C*(ln(R))³
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"
#include <math.h>

/**
 * Read thermistor using Steinhart-Hart equation
 *
 * Converts thermistor resistance to temperature using the three-coefficient
 * Steinhart-Hart equation, which provides better accuracy than the Beta method.
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - from EEPROM/serial config mode
 * 2. Preset calibration (PROGMEM) - from sensor library (e.g., VDO sensors)
 * 3. Default fallback - Generic 10K NTC thermistor coefficients
 *
 * @note Returns NAN if ADC reading is invalid or resistance calculation fails
 */
void readThermistorSteinhart(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float R_bias, A, B, C;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        R_bias = ptr->customCalibration.steinhart.bias_resistor;
        A = ptr->customCalibration.steinhart.steinhart_a;
        B = ptr->customCalibration.steinhart.steinhart_b;
        C = ptr->customCalibration.steinhart.steinhart_c;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
        // Read from preset calibration (PROGMEM)
        const ThermistorSteinhartCalibration* cal = (const ThermistorSteinhartCalibration*)ptr->presetCalibration;
        R_bias = pgm_read_float(&cal->bias_resistor);
        A = pgm_read_float(&cal->steinhart_a);
        B = pgm_read_float(&cal->steinhart_b);
        C = pgm_read_float(&cal->steinhart_c);
    } else {
        // Use defaults
        R_bias = 10000.0;
        A = 1.129241e-3;
        B = 2.341077e-4;
        C = 8.775468e-8;
    }

    // Calculate thermistor resistance
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Steinhart-Hart equation: 1/T = A + B*ln(R) + C*(ln(R))^3
    float logR = log(R_thermistor);
    float logR3 = logR * logR * logR;
    float temp_kelvin = 1.0 / (A + (B * logR) + (C * logR3));

    ptr->value = temp_kelvin - 273.15;  // Store in Celsius
}
