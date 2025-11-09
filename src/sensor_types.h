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
    VDO120,              // Kept for backward compatibility
    VDO150,              // Kept for backward compatibility
    THERMISTOR_LOOKUP,   // Generic thermistor with lookup table
    THERMISTOR_STEINHART,// Generic thermistor with Steinhart-Hart
    GENERIC_BOOST,
    MPX4250AP,
    VDO_2BAR,
    VDO_5BAR,
    STEINHART,           // Deprecated - use THERMISTOR_STEINHART
    VOLTAGE_DIVIDER,
    BME280_TEMP,
    BME280_PRESSURE,
    BME280_HUMIDITY,
    BME280_ALTITUDE   
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
    PERCENT,
    METERS,
    FEET
};

// Calibration type enumeration (for type safety)
enum CalibrationType {
    CAL_NONE,
    CAL_THERMISTOR_STEINHART,
    CAL_THERMISTOR_LOOKUP,
    CAL_PRESSURE,
    CAL_VOLTAGE_DIVIDER
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

// Pressure sensor calibration
typedef struct {
    float offset;          // Zero offset
    float scale;           // Scale factor
    float polynomial_a;    // For polynomial calibration
    float polynomial_b;
    float polynomial_c;
} PressureCalibration;

// Voltage divider calibration
typedef struct {
    float r1;              // High-side resistor (ohms)
    float r2;              // Low-side resistor (ohms)
    float correction;      // Correction factor for ADC non-linearity
} VoltageDividerCalibration;

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

// Safely get pressure calibration
inline PressureCalibration* getPressureCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_PRESSURE) {
        return nullptr;
    }
    return (PressureCalibration*)ptr->calibrationData;
}

// Safely get voltage divider calibration
inline VoltageDividerCalibration* getVoltageDividerCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_VOLTAGE_DIVIDER) {
        return nullptr;
    }
    return (VoltageDividerCalibration*)ptr->calibrationData;
}

#endif