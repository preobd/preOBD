/*
 * sensor_utils.cpp - Shared Sensor Utility Functions
 *
 * Contains common utility functions used across multiple sensor types.
 */

#include "sensor_utils.h"

// Helper macros to read calibration data from PROGMEM
#define READ_FLOAT_PROGMEM(addr) pgm_read_float(&(addr))

/**
 * Linear interpolation in a PROGMEM lookup table.
 *
 * Performs linear interpolation to find a Y value for a given X value in a
 * pair of lookup tables stored in flash memory (PROGMEM).
 *
 * Commonly used for thermistor resistance-to-temperature conversion, where
 * resistance values are stored in descending order (high resistance = low temp).
 *
 * @param value       The X value to look up
 * @param tableSize   Number of entries in the lookup tables
 * @param xTable      X values in PROGMEM (must be sorted, typically descending)
 * @param yTable      Corresponding Y values in PROGMEM
 * @return Interpolated Y value, or NAN if lookup fails
 *
 * @note X table is traversed backwards assuming descending order.
 *       For ascending tables, modify the iteration direction.
 */
float interpolate(float value, byte tableSize, const float* xTable, const float* yTable) {
    // Handle edge cases - read from PROGMEM
    float x0 = READ_FLOAT_PROGMEM(xTable[0]);
    float xLast = READ_FLOAT_PROGMEM(xTable[tableSize-1]);

    if (value >= x0) return READ_FLOAT_PROGMEM(yTable[0]);
    if (value <= xLast) return READ_FLOAT_PROGMEM(yTable[tableSize-1]);

    // Find the right segment
    // Iterate backwards since tables are typically in descending order
    for (int i = tableSize-1; i >= 0; i--) {
        float xi = READ_FLOAT_PROGMEM(xTable[i]);
        float xi_prev = READ_FLOAT_PROGMEM(xTable[i-1]);

        if (value >= xi && value <= xi_prev) {
            // Linear interpolation: y = y1 + ((x – x1) / (x2 – x1)) * (y2 – y1)
            float yi = READ_FLOAT_PROGMEM(yTable[i]);
            float xi_next = READ_FLOAT_PROGMEM(xTable[i+1]);
            float yi_next = READ_FLOAT_PROGMEM(yTable[i+1]);

            return yi + ((value - xi) / (xi_next - xi)) * (yi_next - yi);
        }
    }
    return NAN;
}

/**
 * Linear interpolation in a PROGMEM lookup table (ascending X order).
 *
 * Performs linear interpolation to find a Y value for a given X value in a
 * pair of lookup tables stored in flash memory (PROGMEM).
 *
 * Used for pressure sensors where resistance increases with pressure
 * (unlike thermistors where resistance decreases with temperature).
 *
 * @param value       The X value to look up
 * @param tableSize   Number of entries in the lookup tables
 * @param xTable      X values in PROGMEM (must be sorted in ASCENDING order)
 * @param yTable      Corresponding Y values in PROGMEM
 * @return Interpolated Y value, or NAN if lookup fails
 */
float interpolateAscending(float value, byte tableSize, const float* xTable, const float* yTable) {
    // Handle edge cases - read from PROGMEM
    float x0 = READ_FLOAT_PROGMEM(xTable[0]);
    float xLast = READ_FLOAT_PROGMEM(xTable[tableSize-1]);

    // Clamp to table boundaries
    if (value <= x0) return READ_FLOAT_PROGMEM(yTable[0]);
    if (value >= xLast) return READ_FLOAT_PROGMEM(yTable[tableSize-1]);

    // Find the right segment (iterate forward for ascending order)
    for (int i = 0; i < tableSize - 1; i++) {
        float xi = READ_FLOAT_PROGMEM(xTable[i]);
        float xi_next = READ_FLOAT_PROGMEM(xTable[i+1]);

        if (value >= xi && value <= xi_next) {
            // Linear interpolation: y = y1 + ((x - x1) / (x2 - x1)) * (y2 - y1)
            float yi = READ_FLOAT_PROGMEM(yTable[i]);
            float yi_next = READ_FLOAT_PROGMEM(yTable[i+1]);

            return yi + ((value - xi) / (xi_next - xi)) * (yi_next - yi);
        }
    }
    return NAN;
}

// Readings within this margin of 0 or ADC_MAX are considered "railed"
// (sensor disconnected, shorted, or out of range)
#define ADC_RAIL_MARGIN 3

/**
 * Centralized ADC reading with validation
 *
 * Reads analog pin twice (discarding first reading) and validates range.
 *
 * @param pin      Analog pin number to read
 * @param isValid  Pointer to bool that will be set to false if reading is out of range
 * @return         ADC reading value (0-ADC_MAX_VALUE)
 *
 * @note First reading after switching pins may be inaccurate due to ADC multiplexer
 *       settling and sample-and-hold capacitor charging. We read twice and keep the second.
 */
int readAnalogPin(int pin, bool* isValid) {
    // First reading after switching pins may be inaccurate due to ADC multiplexer
    // settling and sample-and-hold capacitor charging. Read twice, keep second.
    // Note: 10ms delay between reads improves stability but is currently disabled
    // to minimize loop time. Uncomment if experiencing noisy readings.
    analogRead(pin);              // Discard first reading (multiplexer settling)
    //delay(10);                  // Optional: Allow ADC input to stabilize
    int reading = analogRead(pin); // Actual measurement

    // Check if reading is within valid range (not stuck at rails)
    *isValid = (reading < (ADC_MAX_VALUE - ADC_RAIL_MARGIN) && reading > ADC_RAIL_MARGIN);
    return reading;
}

/**
 * Calculate resistance from ADC reading using voltage divider formula
 *
 * @param reading       ADC reading value (0-ADC_MAX_VALUE)
 * @param biasResistor  Known bias resistor value in ohms
 * @return              Calculated sensor resistance in ohms, or NAN if invalid
 *
 * Formula: R_sensor = reading * R_bias / (ADC_MAX - reading)
 */
float calculateResistance(int reading, float biasResistor) {
    if (reading >= ADC_MAX_VALUE) {
        return NAN;  // Avoid division by zero
    }
    return reading * biasResistor / (ADC_MAX_VALUE - reading);
}
