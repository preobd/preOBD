/*
 * input.h - Input-based sensor configuration
 *
 * Core structure for runtime-configurable sensor inputs.
 * Each Input represents a physical pin that can be assigned an Application (preset)
 * and Sensor (hardware device) at runtime via serial commands or EEPROM.
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW
 * ============================================================================
 *
 * The system uses a two-level hierarchy:
 *
 *   APPLICATION (what you're measuring)
 *       │
 *       ├── CHT (Cylinder Head Temperature)
 *       ├── OIL_PRESSURE
 *       ├── COOLANT_TEMP
 *       └── ... (see Application enum)
 *
 *   SENSOR (hardware device)
 *       │
 *       ├── MAX6675 (K-type thermocouple amplifier)
 *       ├── VDO_120C_LOOKUP (VDO thermistor, lookup table)
 *       ├── VDO_5BAR (VDO pressure sender)
 *       └── ... (see Sensor enum)
 *
 * When you assign an Application to a pin, it loads defaults from
 * ApplicationPreset (application_presets.h):
 *   - Default sensor type
 *   - Display name and abbreviation
 *   - Alarm thresholds
 *   - OBD-II PID mapping
 *
 * You can then override the sensor type if using different hardware.
 * The Sensor determines which read function and calibration data to use.
 *
 * ============================================================================
 * MEMORY LAYOUT
 * ============================================================================
 *
 * Input struct: ~100 bytes each
 * - Stored in RAM (inputs[] array)
 * - Calibration data pointed to PROGMEM (not copied)
 * - Custom calibration stored in RAM only if overridden
 *
 * For Arduino Uno (2KB RAM), practical limit is ~10 inputs.
 * For Teensy/Mega, can support 20+ inputs easily.
 *
 * ============================================================================
 */

#ifndef INPUT_H
#define INPUT_H

// Flag indicating unified Input-based architecture is in use (always defined)
#define USE_INPUT_BASED_ARCHITECTURE

#include <Arduino.h>
#include "../lib/sensor_types.h"

// Forward declarations
struct Input;

// ===== APPLICATION =====
// High-level application presets (CHT, OIL_PRESSURE, etc.)
enum Application {
    APP_NONE = 0,

    // Temperature measurements
    CHT,                    // Cylinder Head Temperature
    EGT,                    // Exhaust Gas Temperature
    COOLANT_TEMP,           // Engine coolant / water temperature
    OIL_TEMP,               // Engine oil temperature
    TCASE_TEMP,             // Transfer case / transmission temperature
    AMBIENT_TEMP,           // Outside air temperature

    // Pressure measurements
    OIL_PRESSURE,           // Engine oil pressure
    BOOST_PRESSURE,         // Turbo/supercharger boost
    FUEL_PRESSURE,          // Fuel system pressure
    BAROMETRIC_PRESSURE,    // Atmospheric pressure (altitude)

    // Electrical measurements
    PRIMARY_BATTERY,        // Main vehicle battery
    AUXILIARY_BATTERY,      // Secondary/aux battery

    // Other measurements
    COOLANT_LEVEL,          // Coolant reservoir level (float switch)
    HUMIDITY,               // Relative humidity (BME280)
    ELEVATION,              // Calculated altitude (BME280)
    ENGINE_RPM              // Engine speed via W-phase
};

// ===== SENSOR (HARDWARE DEVICE) =====
// Physical sensor hardware types
// Using original enum names for backward compatibility
enum Sensor {
    SENSOR_NONE = 0,
    // Thermocouples
    MAX6675,
    K_TYPE_THERMOCOUPLE_MAX6675 = MAX6675,  // Descriptive alias
    MAX31855,
    K_TYPE_THERMOCOUPLE_MAX31855 = MAX31855,  // Descriptive alias
    // VDO Thermistors
    VDO_120C_LOOKUP,
    VDO_150C_LOOKUP,
    VDO_120C_STEINHART,
    VDO_150C_STEINHART,
    // Generic Thermistors
    THERMISTOR_LOOKUP,
    THERMISTOR_STEINHART,
    // Pressure Sensors
    GENERIC_BOOST,
    MPX4250AP,
    VDO_2BAR,
    VDO_5BAR,
    // Voltage
    VOLTAGE_DIVIDER,
    STANDARD_12V_DIVIDER = VOLTAGE_DIVIDER,  // Descriptive alias for 12V battery monitoring
    // RPM
    W_PHASE_RPM,
    // BME280
    BME280_TEMP,
    BME280_AMBIENT_TEMPERATURE = BME280_TEMP,  // Descriptive alias
    BME280_PRESSURE,
    BME280_BAROMETRIC_PRESSURE = BME280_PRESSURE,  // Descriptive alias
    BME280_HUMIDITY,
    BME280_RELATIVE_HUMIDITY = BME280_HUMIDITY,  // Descriptive alias
    BME280_ELEVATION,
    // Digital
    FLOAT_SWITCH
};

// ===== CALIBRATION OVERRIDE UNION =====
// Custom calibration storage (16 bytes)
// Used when useCustomCalibration == true
union CalibrationOverride {
    // Thermistor Steinhart-Hart (16 bytes)
    struct {
        float bias_resistor;
        float steinhart_a;
        float steinhart_b;
        float steinhart_c;
    } steinhart;
    
    // Thermistor Lookup (4 bytes + padding)
    // Tables stay in flash, only bias resistor is overridable
    struct {
        float bias_resistor;
        byte padding[12];
    } lookup;
    
    // Linear pressure sensor (16 bytes)
    struct {
        float voltage_min;
        float voltage_max;
        float pressure_min;
        float pressure_max;
    } pressureLinear;
    
    // Polynomial pressure sensor (16 bytes)
    struct {
        float bias_resistor;
        float poly_a;
        float poly_b;
        float poly_c;
    } pressurePolynomial;
    
    // Voltage divider (16 bytes)
    struct {
        float r1;
        float r2;
        float correction;
        float offset;
    } voltageDivider;
    
    // RPM sensor (10 bytes + padding)
    struct {
        byte poles;
        float pulses_per_rev;
        uint16_t timeout_ms;
        uint16_t min_rpm;
        uint16_t max_rpm;
        byte padding[2];
    } rpm;
    
    // Raw bytes for memset/EEPROM operations
    byte raw[16];
};

// ===== INPUT STRUCTURE =====
// Runtime configuration for a physical input pin
// Size: ~100 bytes per input
struct Input {
    // === Hardware (1 byte) ===
    uint8_t pin;                    // Physical pin (A0-A15, or digital)

    // === User Configuration ===
    char abbrName[8];               // "CHT", "OIL" (for LCD display)
    char displayName[24];           // "Cylinder Head Temp" (full name)
    Application application;        // What we're measuring (CHT, OIL_PRESSURE, etc.)
    Sensor sensor;                  // Hardware device (MAX6675, VDO_5BAR, etc.)
    Units displayUnits;             // Display units (CELSIUS, PSI, etc.)
    
    // === Alarm Thresholds (stored in STANDARD UNITS) ===
    // Temperature: Celsius
    // Pressure: bar
    // Voltage: volts
    float minValue;                 // Alarm minimum (standard units)
    float maxValue;                 // Alarm maximum (standard units)
    
    // === OBDII ===
    uint8_t obd2pid;               // OBD-II PID
    uint8_t obd2length;            // OBD-II response length

    // === Runtime Data ===
    float value;                    // Current sensor reading

    // === Flags (packed into 1 byte) ===
    struct {
        uint8_t isEnabled : 1;      // Input enabled/disabled
        uint8_t alarm : 1;          // Alarm enabled
        uint8_t display : 1;        // Show on LCD
        uint8_t useCustomCalibration : 1;  // Use custom or preset calibration
        uint8_t reserved : 4;       // Reserved for future use
    } flags;

    // === Function Pointers ===
    void (*readFunction)(Input*);
    MeasurementType measurementType;

    // === Calibration Data ===
    CalibrationType calibrationType;
    const void* presetCalibration;       // Pointer to PROGMEM preset
    CalibrationOverride customCalibration; // Custom calibration (16 bytes)
};

#endif // INPUT_H