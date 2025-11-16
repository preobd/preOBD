/*
 * sensor_types.h - Sensor type definitions and structures
 */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <Arduino.h>

// Forward declaration
struct Sensor;

// Sensor type enumeration
enum SensorType {
    MAX6675,
    MAX31855,
    THERMISTOR_LOOKUP,
    THERMISTOR_STEINHART,
    GENERIC_BOOST,
    MPX4250AP,
    VDO_2BAR,
    VDO_5BAR,
    VOLTAGE_DIVIDER,
    W_PHASE_RPM,
    BME280_TEMP,
    BME280_PRESSURE,
    BME280_HUMIDITY,
    BME280_ALTITUDE,
    DIGITAL_FLOAT_SWITCH
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

// Calibration type enumeration (for type safety)
enum CalibrationType {
    CAL_NONE,
    CAL_THERMISTOR_STEINHART,
    CAL_THERMISTOR_LOOKUP,
    CAL_PRESSURE_POLYNOMIAL,
    CAL_PRESSURE_LINEAR,
    CAL_PRESSURE,
    CAL_VOLTAGE_DIVIDER,
    CAL_RPM
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

// ===== SENSOR STRUCTURE =====

typedef struct Sensor {
    // Hardware
    byte input;
    
    // OBDII
    byte obd2pid;
    byte obd2length;
    
    // Data
    float value;              // Stored in standard units (C, bar, volts, %, meters)
    
    // Type and identification
    SensorType sensorType;
    const char *abbrName;
    const char *displayName;
    
    // Display
    DisplayUnits displayUnits;
    
    // Thresholds
    float minValue;
    float maxValue;
    
    // Flags
    bool alarm;
    bool display;
    bool isEnabled;
    
    // Function pointers
    void (*readFunction)(struct Sensor*);
    float (*displayConvert)(float value, DisplayUnits units);
    float (*obdConvert)(float value);  // Convert to OBDII format
    
    // Calibration data (type-safe via calibrationType)
    void* calibrationData;
    CalibrationType calibrationType;
    
} Sensor;

// Function pointer type definitions for clarity
typedef void (*SensorReadFunc)(Sensor*);
typedef float (*ConvertFunc)(float, DisplayUnits);
typedef float (*OBDConvertFunc)(float);

// ===== HELPER FUNCTIONS FOR TYPE SAFETY =====

// Safely get thermistor Steinhart calibration
inline ThermistorSteinhartCalibration* getThermistorSteinhartCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_THERMISTOR_STEINHART) {
        return nullptr;
    }
    return (ThermistorSteinhartCalibration*)ptr->calibrationData;
}

// Safely get thermistor lookup calibration
inline ThermistorLookupCalibration* getThermistorLookupCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_THERMISTOR_LOOKUP) {
        return nullptr;
    }
    return (ThermistorLookupCalibration*)ptr->calibrationData;
}

// Safely get pressure linear calibration
inline PressureLinearCalibration* getPressureLinearCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_PRESSURE) {
        return nullptr;
    }
    return (PressureLinearCalibration*)ptr->calibrationData;
}

// Safely get pressure linear calibration
inline PressurePolynomialCalibration* getPressurePolynomialCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_PRESSURE) {
        return nullptr;
    }
    return (PressurePolynomialCalibration*)ptr->calibrationData;
}

// Safely get voltage divider calibration
inline VoltageDividerCalibration* getVoltageDividerCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_VOLTAGE_DIVIDER) {
        return nullptr;
    }
    return (VoltageDividerCalibration*)ptr->calibrationData;
}

// Safely get RPM calibration
inline RPMCalibration* getRPMCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_RPM) {
        return nullptr;
    }
    return (RPMCalibration*)ptr->calibrationData;
}

// ===== UTILITY FUNCTIONS =====

// Get string representation of display unit
const char* getUnitString(DisplayUnits units);

#endif