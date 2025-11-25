/*
 * sensor_types.h - Sensor type definitions and structures
 * Now unified for Input-based architecture only
 */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <Arduino.h>

// Sensor enum is defined in input.h
// Units enum defined below

// Measurement type enumeration
// Defines the physical quantity being measured (what, not how)
// This determines which conversion functions to use for display and OBD output
enum MeasurementType {
    MEASURE_TEMPERATURE,  // Celsius -> F/C
    MEASURE_PRESSURE,     // Bar -> PSI/kPa/inHg/bar
    MEASURE_VOLTAGE,      // Volts
    MEASURE_RPM,          // RPM
    MEASURE_HUMIDITY,     // Percent
    MEASURE_ELEVATION,    // Meters -> feet/meters
    MEASURE_DIGITAL       // Digital on/off (float switch)
};

// Display units enumeration
enum DisplayUnits {
    CELSIUS,
    FAHRENHEIT,
    BAR,
    PSI,
    KPA,
    INHG,
    VOLTS,
    RPM,
    PERCENT,
    METERS,
    FEET
};

// Type alias for compatibility with new Input-based architecture
typedef DisplayUnits Units;

// Calibration type enumeration (for type safety)
enum CalibrationType {
    CAL_NONE,
    CAL_THERMISTOR_STEINHART,
    CAL_THERMISTOR_LOOKUP,
    CAL_PRESSURE_POLYNOMIAL,
    CAL_PRESSURE_LINEAR,
    CAL_VOLTAGE_DIVIDER,
    CAL_RPM,
};

// ===== CALIBRATION STRUCTURES =====

// Thermistor calibration using Steinhart-Hart equation
// 1/T = A + B*ln(R) + C*(ln(R))^3
typedef struct {
    float bias_resistor;    // Pull-down resistor in ohms
    float steinhart_a;      // Steinhart-Hart A coefficient
    float steinhart_b;      // Steinhart-Hart B coefficient
    float steinhart_c;      // Steinhart-Hart C coefficient
} ThermistorSteinhartCalibration;

// Thermistor calibration using lookup table interpolation
typedef struct {
    float bias_resistor;           // Pull-down resistor in ohms
    const float* resistance_table; // Pointer to resistance array (ohms)
    const float* temperature_table;// Pointer to temperature array (°C)
    byte table_size;              // Number of entries in tables
} ThermistorLookupCalibration;

// Pressure sensor calibration - Linear (most common)
// P = (V - V_min) / (V_max - V_min) * (P_max - P_min) + P_min
typedef struct {
    float voltage_min;      // Minimum sensor voltage (V)
    float voltage_max;      // Maximum sensor voltage (V)
    float pressure_min;     // Pressure at V_min (bar)
    float pressure_max;     // Pressure at V_max (bar)
} PressureLinearCalibration;

// Pressure sensor calibration - Polynomial (VDO sensors)
// Uses quadratic formula to solve VDO's pressure-to-resistance polynomial
typedef struct {
    float bias_resistor;   // Pull-down resistor in ohms
    float poly_a;          // Polynomial coefficient A
    float poly_b;          // Polynomial coefficient B
    float poly_c;          // Polynomial coefficient C
} PressurePolynomialCalibration;

// Type aliases for compatibility with new Input-based architecture
typedef PressureLinearCalibration LinearCalibration;
typedef PressurePolynomialCalibration PolynomialCalibration;

// Voltage divider calibration
typedef struct {
    float r1;              // High-side resistor (ohms)
    float r2;              // Low-side resistor (ohms)
    float correction;      // Correction factor (multiplier, typically 1.0)
    float offset;          // Voltage offset (typically 0.0)
} VoltageDividerCalibration;

// ===== RPM CALIBRATION STRUCTURES =====
typedef struct {
    byte poles;              // Number of alternator poles
    float pulses_per_rev;    // Calculated from poles
    uint16_t timeout_ms;     // Timeout for zero RPM (ms)
    uint16_t min_rpm;        // Minimum valid RPM
    uint16_t max_rpm;        // Maximum valid RPM
} RPMCalibration;

// Legacy code removed: StaticSensor struct, wrapper conversion logic, and old sensor array system.
// All code now uses unified Input-based architecture with compile-time or runtime configuration.

// ===== UTILITY FUNCTIONS =====

// Get string representation of display unit
const char* getUnitString(DisplayUnits units);

#endif