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
    VDO120,
    VDO150,
    GENERIC_BOOST,
    MPX4250AP,
    VDO_2BAR,
    VDO_5BAR,
    STEINHART,
    VOLTAGE_DIVIDER,
    BME280_TEMP,
    BME280_PRESSURE
};

// Display units enumeration
enum DisplayUnits { 
    CELSIUS,
    FAHRENHEIT,
    BAR,
    PSI,
    KPA,
    VOLTS
};

// Sensor structure with function pointers
typedef struct Sensor {
    // Hardware
    byte input;
    
    // OBDII
    byte obd2pid;
    byte obd2length;
    
    // Data
    float value;              // Stored in standard units (C, bar, volts)
    
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
    
} Sensor;

// Function pointer type definitions for clarity
typedef void (*SensorReadFunc)(Sensor*);
typedef float (*ConvertFunc)(float, DisplayUnits);
typedef float (*OBDConvertFunc)(float);

#endif
