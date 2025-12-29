/*
 * beta.cpp - Thermistor Reading (Beta Equation)
 *
 * Implements thermistor temperature reading using the Beta parameter equation.
 * Simpler than Steinhart-Hart but less accurate over wide temperature ranges.
 *
 * Beta equation: T(K) = 1 / (1/T₀(K) + (1/β) * ln(R/R₀))
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"
#include <math.h>

/**
 * Read thermistor using Beta equation
 *
 * Converts thermistor resistance to temperature using the simplified
 * Beta parameter equation. Good for moderate temperature ranges.
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - from EEPROM/serial config mode
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - Generic 10K NTC β=3950K at 25°C
 *
 * @note Returns NAN if ADC reading is invalid or resistance calculation fails
 */
void readThermistorBeta(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float R_bias, beta, R0, T0_celsius;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_THERMISTOR_BETA) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        R_bias = ptr->customCalibration.beta.bias_resistor;
        beta = ptr->customCalibration.beta.beta;
        R0 = ptr->customCalibration.beta.r0;
        T0_celsius = ptr->customCalibration.beta.t0;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_THERMISTOR_BETA) {
        // Read from preset calibration (PROGMEM)
        const BetaCalibration* cal = (const BetaCalibration*)ptr->presetCalibration;
        R_bias = pgm_read_float(&cal->bias_resistor);
        beta = pgm_read_float(&cal->beta);
        R0 = pgm_read_float(&cal->r0);
        T0_celsius = pgm_read_float(&cal->t0);
    } else {
        // Use defaults (typical 10K NTC thermistor at 25°C, β=3950K)
        R_bias = 10000.0;
        beta = 3950.0;
        R0 = 10000.0;
        T0_celsius = 25.0;
    }

    // Calculate thermistor resistance
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Beta equation: T(K) = 1 / (1/T0(K) + (1/β) * ln(R/R0))
    // Convert T0 from Celsius to Kelvin
    float T0_kelvin = T0_celsius + 273.15;

    // Calculate temperature in Kelvin
    float logR_ratio = log(R_thermistor / R0);
    float temp_kelvin = 1.0 / ((1.0 / T0_kelvin) + (logR_ratio / beta));

    ptr->value = temp_kelvin - 273.15;  // Store in Celsius
}
