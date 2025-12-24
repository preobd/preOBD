/*
 * sensor_types.h - Sensor type definitions and structures
 */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <Arduino.h>

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

// Calibration type enumeration (for type safety)
enum CalibrationType {
    CAL_NONE,
    CAL_THERMISTOR_STEINHART,
    CAL_THERMISTOR_LOOKUP,
    CAL_THERMISTOR_BETA,
    CAL_PRESSURE_POLYNOMIAL,
    CAL_LINEAR,              // Linear sensor (temperature, pressure, etc.)
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

// Thermistor calibration using Beta parameter equation
// T(K) = 1 / (1/T0(K) + (1/β) * ln(R/R0))
typedef struct {
    float bias_resistor;    // Pull-down resistor in ohms
    float beta;             // Beta coefficient in Kelvin (typically 3000-5000K)
    float r0;               // Reference resistance in ohms (typically at 25°C)
    float t0;               // Reference temperature in Celsius (user input, converted to K for calc)
} BetaCalibration;

// Thermistor calibration using lookup table interpolation
typedef struct {
    float bias_resistor;           // Pull-down resistor in ohms
    const float* resistance_table; // Pointer to resistance array (ohms)
    const float* temperature_table;// Pointer to temperature array (°C)
    byte table_size;              // Number of entries in tables
} ThermistorLookupCalibration;

// Linear sensor calibration (works for temperature, pressure, voltage, etc.)
// Y = (V - V_min) / (V_max - V_min) * (Y_max - Y_min) + Y_min
typedef struct {
    float voltage_min;      // Minimum sensor voltage (V)
    float voltage_max;      // Maximum sensor voltage (V)
    float output_min;       // Output value at V_min (units depend on measurementType)
    float output_max;       // Output value at V_max (units depend on measurementType)
} LinearCalibration;

// Pressure sensor calibration - Polynomial (VDO sensors)
// Uses quadratic formula to solve VDO's pressure-to-resistance polynomial
typedef struct {
    float bias_resistor;   // Pull-down resistor in ohms
    float poly_a;          // Polynomial coefficient A
    float poly_b;          // Polynomial coefficient B
    float poly_c;          // Polynomial coefficient C
} PressurePolynomialCalibration;
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
    byte poles;              // Number of alternator poles (8, 10, 12, 14, 16)
    float pulley_ratio;      // Alternator/Engine pulley ratio (e.g., 3.0 for 3:1)
    float calibration_mult;  // Fine-tuning multiplier (default 1.0, adjust empirically)
    uint16_t timeout_ms;     // Timeout for zero RPM (ms, default 2000)
    uint16_t min_rpm;        // Minimum valid RPM (default 100)
    uint16_t max_rpm;        // Maximum valid RPM (default 10000)
} RPMCalibration;

#endif