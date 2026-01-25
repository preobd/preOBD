/*
 * sensor_read.cpp - Sensor Reading Orchestrator
 *
 * This file orchestrates all sensor reading implementations by including
 * modular sensor files organized by type. The actual sensor implementations
 * have been extracted to separate files for better organization and maintainability.
 *
 * Directory structure:
 * - sensors/sensor_utils.cpp          - Shared utility functions
 * - sensors/linear/                   - Shared linear sensor implementation
 * - sensors/thermocouples/            - SPI thermocouple sensors
 * - sensors/thermistors/              - NTC thermistor sensors
 * - sensors/pressure/                 - Pressure sensors
 * - sensors/voltage/                  - Voltage measurement
 * - sensors/rpm/                      - RPM sensing
 * - sensors/environmental/            - Environmental sensors (BME280)
 * - sensors/digital/                  - Digital inputs (float switch)
 */

#include "../config.h"
#include "../lib/platform.h"
#include "input.h"
#include "../lib/sensor_types.h"
#ifdef USE_STATIC_CONFIG
#include "../lib/generated/sensor_library_static.h"
#else
#include "../lib/sensor_library.h"
#endif
#include "../lib/units_registry.h"
#include <SPI.h>
#include <Wire.h>

// ===== SENSOR IMPLEMENTATIONS =====
// Include modular sensor files organized by type

// Shared utilities (interpolate, readAnalogPin, calculateResistance)
#include "sensors/sensor_utils.cpp"

// Shared linear sensor (used by both pressure and temperature)
#include "sensors/linear/linear_sensor.cpp"

// Thermocouples (SPI-based)
#include "sensors/thermocouples/thermocouple_common.cpp"
#include "sensors/thermocouples/max6675.cpp"
#include "sensors/thermocouples/max31855.cpp"

// Thermistors (NTC resistance-based)
#include "sensors/thermistors/steinhart.cpp"
#include "sensors/thermistors/beta.cpp"
#include "sensors/thermistors/table.cpp"

// Pressure sensors
#include "sensors/pressure/polynomial.cpp"
#include "sensors/pressure/table.cpp"

// Voltage sensors
#include "sensors/voltage/divider.cpp"
#include "sensors/voltage/direct.cpp"

// RPM sensors
#include "sensors/rpm/w_phase.cpp"

// Speed sensors
#include "sensors/speed/hall_speed.cpp"

// Environmental sensors (e.g. BME280)
#include "sensors/environmental/bme280.cpp"

// Digital sensors
#include "sensors/digital/float_switch.cpp"

// Note: thermistors/linear.cpp and pressure/linear.cpp are wrapper files
// that point to linear/linear_sensor.cpp (already included above)

// ===== CONVERSION FUNCTIONS =====
// Registry-based conversion - uses data from UNITS_REGISTRY

/**
 * Convert from base units to display units using registry conversion factors
 *
 * Base units by measurement type:
 * - Temperature: Celsius
 * - Pressure: Bar
 * - Voltage: Volts
 * - RPM: RPM
 * - Humidity: Percent
 * - Elevation: Meters
 *
 * @param baseValue  Value in base units
 * @param unitsIndex Index into UNITS_REGISTRY (0-10)
 * @return           Value in display units
 */
float convertFromBaseUnits(float baseValue, uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return baseValue;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    float factor = pgm_read_float(&info->conversionFactor);
    float offset = pgm_read_float(&info->conversionOffset);

    return baseValue * factor + offset;
}

/**
 * Convert from display units to base units (inverse of convertFromBaseUnits)
 *
 * @param displayValue Value in display units
 * @param unitsIndex   Index into UNITS_REGISTRY (0-10)
 * @return             Value in base units
 */
float convertToBaseUnits(float displayValue, uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return displayValue;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    float factor = pgm_read_float(&info->conversionFactor);
    float offset = pgm_read_float(&info->conversionOffset);

    return (displayValue - offset) / factor;
}

// ===== OBDII CONVERSION FUNCTIONS =====

float obdConvertTemperature(float celsius) {
    return celsius + 40.0;  // OBDII format: A-40
}

float obdConvertPressure(float bar) {
    return bar * 10.0;  // OBDII format: A/10
}

float obdConvertVoltage(float volts) {
    return volts * 10.0;  // OBDII format: A/10
}

float obdConvertDirect(float value) {
    return value;
}

float obdConvertRPM(float rpm) {
    return rpm / 4.0;  // OBDII format: RPM = ((A×256)+B)/4
}

float obdConvertHumidity(float humidity) {
    return humidity * 2.55;  // Convert 0-100% to 0-255
}

float obdConvertElevation(float meters) {
    return meters;
}

float obdConvertFloatSwitch(float value) {
    return value * 255.0;  // OBDII format: 0 or 255
}

float obdConvertSpeed(float kph) {
    // OBD-II PID 0x0D format: Vehicle Speed in km/h (range 0-255)
    // Direct value, no conversion needed
    if (kph > 255.0) return 255.0;  // Clamp to max
    return kph;
}

// ===== MEASUREMENT TYPE CONVERSION HELPERS =====

ObdConvertFunc getObdConvertFunc(MeasurementType type) {
    switch (type) {
        case MEASURE_TEMPERATURE: return obdConvertTemperature;
        case MEASURE_PRESSURE: return obdConvertPressure;
        case MEASURE_VOLTAGE: return obdConvertVoltage;
        case MEASURE_RPM: return obdConvertRPM;
        case MEASURE_HUMIDITY: return obdConvertHumidity;
        case MEASURE_ELEVATION: return obdConvertElevation;
        case MEASURE_DIGITAL: return obdConvertFloatSwitch;
        case MEASURE_SPEED: return obdConvertSpeed;
        default: return obdConvertVoltage;
    }
}
