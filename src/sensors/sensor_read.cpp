/*
 * sensor_read.cpp - Sensor Reading Orchestrator
 *
 * This file orchestrates all sensor reading implementations by including
 * modular sensor files organized by type. The actual sensor implementations
 * have been extracted to separate files for better organization and maintainability.
 *
 * Directory structure:
 * - drivers/sensor_utils.cpp          - Shared utility functions
 * - drivers/linear/                   - Shared linear sensor implementation
 * - drivers/thermocouples/            - SPI thermocouple sensors
 * - drivers/thermistors/              - NTC thermistor sensors
 * - drivers/pressure/                 - Pressure sensors
 * - drivers/voltage/                  - Voltage measurement
 * - drivers/rpm/                      - RPM sensing
 * - drivers/environmental/            - Environmental sensors (BME280)
 * - drivers/digital/                  - Digital inputs (float switch)
 */

#include "../config.h"
#include "../core/platform.h"
#include "../inputs/input.h"
#include "calibration_types.h"
#include "sensor_library.h"
#include "units_registry.h"
#include <SPI.h>
#include <Wire.h>

// ===== SENSOR IMPLEMENTATIONS =====
// Include modular sensor files organized by type

// Shared utilities (interpolate, readAnalogPin, calculateResistance)
#include "drivers/sensor_utils.cpp"

// Shared linear sensor (used by both pressure and temperature)
#include "drivers/linear/linear_sensor.cpp"

// Thermocouples (SPI-based)
#include "drivers/thermocouples/thermocouple_common.cpp"
#include "drivers/thermocouples/max6675.cpp"
#include "drivers/thermocouples/max31855.cpp"

// Thermistors (NTC resistance-based)
#include "drivers/thermistors/steinhart.cpp"
#include "drivers/thermistors/beta.cpp"
#include "drivers/thermistors/table.cpp"

// Pressure sensors
#include "drivers/pressure/polynomial.cpp"
#include "drivers/pressure/table.cpp"

// Voltage sensors
#include "drivers/voltage/divider.cpp"
#include "drivers/voltage/direct.cpp"

// RPM sensors
#include "drivers/rpm/w_phase.cpp"

// Speed sensors
#include "drivers/speed/hall_speed.cpp"

// Environmental sensors (e.g. BME280)
#include "drivers/environmental/bme280.cpp"

// Digital sensors
#include "drivers/digital/float_switch.cpp"

// Level sensors
#include "drivers/level/table.cpp"

// CAN sensors (CAN bus imported sensors)
#if INCLUDE_CAN
#include "drivers/can/can_frame_cache.cpp"
#include "drivers/can/can_read.cpp"
#include "drivers/can/can_scan.cpp"
#endif

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

// #509 types. OBD encoding follows a measurement type, not a PID, so these
// match the one J1979 PID each type comes from: 0x10 MAF (256A+B)/100 g/s,
// 0x5E fuel rate (256A+B)/20 L/h; distance (0x21/0x31, km) and time (0x1F,
// s) are raw. An output PID of another layout needs its own formula.
float obdConvertMassFlow(float gps) {
    return gps * 100.0f;
}

float obdConvertFuelRate(float lph) {
    return lph * 20.0f;
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
        case MEASURE_PERCENT: return obdConvertHumidity;  // 0-100% same encoding as humidity
        case MEASURE_DISTANCE: return obdConvertDirect;   // km
        case MEASURE_TIME: return obdConvertDirect;       // s
        case MEASURE_MASS_FLOW: return obdConvertMassFlow;
        case MEASURE_FUEL_RATE: return obdConvertFuelRate;
        default: return obdConvertVoltage;
    }
}
