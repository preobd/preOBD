/*
 * sensor_utils.h - Shared Sensor Utility Functions
 *
 * This header provides common utility functions used across multiple sensors:
 * - interpolate(): Linear interpolation for lookup tables
 * - readAnalogPin(): ADC reading with validation
 * - calculateResistance(): Voltage divider resistance calculation
 */

#ifndef SENSOR_UTILS_H
#define SENSOR_UTILS_H

#include <Arduino.h>
#include "../../lib/platform.h"
#include "../input.h"

// Linear interpolation in PROGMEM lookup tables (descending X order - thermistors)
float interpolate(float value, byte tableSize, const float* xTable, const float* yTable);

// Linear interpolation in PROGMEM lookup tables (ascending X order - pressure sensors)
float interpolateAscending(float value, byte tableSize, const float* xTable, const float* yTable);

// Centralized ADC reading with validation
int readAnalogPin(int pin, bool* isValid);

// Calculate thermistor resistance from ADC reading
float calculateResistance(int reading, float biasResistor);

#endif // SENSOR_UTILS_H
