/*
 * polynomial.cpp - VDO Polynomial Pressure Sensor Reading
 *
 * Implements pressure reading for VDO sensors using quadratic polynomial
 * resistance-to-pressure conversion. Used by VDO 2-bar and 5-bar sensors.
 *
 * Polynomial: R = A*P² + B*P + C
 * Solved for P using quadratic formula: P = (-B - sqrt(B² - 4*A*(C-R))) / (2*A)
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"
#include <math.h>

/**
 * Read pressure sensor using polynomial calibration
 *
 * Converts sensor resistance to pressure using a quadratic polynomial equation.
 * Specific to VDO pressure sensors with non-linear resistance curves.
 *
 * @param ptr  Pointer to Input structure to store pressure reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - from EEPROM/serial config mode
 * 2. Preset calibration (PROGMEM) - from sensor library (VDO 2-bar, 5-bar)
 * 3. No fallback - returns NAN if coefficients not available
 *
 * @note Returns NAN if calibration data missing or ADC reading invalid
 */
void readPressurePolynomial(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float bias_resistor, a, b, c;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        bias_resistor = ptr->customCalibration.pressurePolynomial.bias_resistor;
        a = ptr->customCalibration.pressurePolynomial.poly_a;
        b = ptr->customCalibration.pressurePolynomial.poly_b;
        c = ptr->customCalibration.pressurePolynomial.poly_c;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        // Read from preset calibration (PROGMEM)
        const PolynomialCalibration* cal = (const PolynomialCalibration*)ptr->presetCalibration;
        bias_resistor = pgm_read_float(&cal->bias_resistor);
        a = pgm_read_float(&cal->poly_a);
        b = pgm_read_float(&cal->poly_b);
        c = pgm_read_float(&cal->poly_c);
    } else {
        ptr->value = NAN;  // Can't calculate without coefficients
        return;
    }

    // VDO sensors use quadratic equation: V = A*P² + B*P + C
    // We need to solve for P: A*P² + B*P + (C - R) = 0
    // Using quadratic formula: P = (-B ± sqrt(B² - 4*A*(C-R))) / (2*A)
    float R_sensor = calculateResistance(reading, bias_resistor);

    if (isnan(R_sensor) || R_sensor <= 0) {
        ptr->value = NAN;
        return;
    }

    c = c - R_sensor;

    float discriminant = (b * b) - (4.0 * a * c);

    if (discriminant < 0) {
        ptr->value = NAN;  // No real solution
        return;
    }

    // Take the positive root (pressure is always positive)
    float pressure = (-b - sqrt(discriminant)) / (2.0 * a);

    ptr->value = pressure;  // Store in bar
}
