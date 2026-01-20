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
 *       └── ... (see Application Presets)
 *
 *   SENSOR (hardware device)
 *       │
 *       ├── MAX6675 (K-type thermocouple amplifier)
 *       ├── VDO_120C_TABLE (VDO thermistor, lookup table)
 *       ├── VDO_5BAR_CURVE (VDO pressure sender, curve fit)
 *       └── ... (see Sensor Library)
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

#include <Arduino.h>
#include "../lib/sensor_types.h"

// Forward declarations
struct Input;

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

    // Thermistor Beta (16 bytes)
    struct {
        float bias_resistor;
        float beta;
        float r0;
        float t0;
    } beta;

    // Thermistor Lookup (4 bytes + padding)
    // Tables stay in flash, only bias resistor is overridable
    struct {
        float bias_resistor;
        byte padding[12];
    } lookup;
    
    // Linear sensor - temperature, pressure, etc. (16 bytes)
    struct {
        float voltage_min;
        float voltage_max;
        float output_min;
        float output_max;
    } pressureLinear;  // Named for backwards compatibility, works for all linear sensors
    
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
    
    // RPM sensor (15 bytes + padding)
    struct {
        byte poles;
        float pulley_ratio;
        float calibration_mult;
        uint16_t timeout_ms;
        uint16_t min_rpm;
        uint16_t max_rpm;
        byte padding[1];
    } rpm;

    // Speed sensor (16 bytes)
    struct {
        uint8_t pulses_per_rev;
        uint16_t tire_circumference_mm;
        float final_drive_ratio;
        float calibration_mult;
        uint16_t timeout_ms;
        uint16_t max_speed_kph;
        byte padding[3];
    } speed;

    // Raw bytes for memset/EEPROM operations
    byte raw[16];
};

// ===== ALARM STATE MACHINE =====
// Alarm state machine states
enum AlarmState : uint8_t {
    ALARM_DISABLED = 0,  // Alarm feature disabled for this input
    ALARM_INIT,          // Initial state after configuration/boot
    ALARM_WARMUP,        // Sensor warming up, alarm blocked
    ALARM_READY,         // Normal operation, alarm checking active
    ALARM_ACTIVE         // Currently in alarm condition
};

// Alarm severity levels (hierarchical)
enum AlarmSeverity : uint8_t {
    SEVERITY_NORMAL = 0,   // No alarm or warning
    SEVERITY_WARNING = 1,  // Warning threshold exceeded
    SEVERITY_ALARM = 2     // Alarm threshold exceeded
};

// Per-input alarm runtime context (12 bytes)
struct AlarmContext {
    AlarmState state;           // Current alarm state (1 byte)
    uint32_t stateEntryTime;    // When current state was entered (4 bytes)
    uint32_t faultStartTime;    // When threshold violation started (4 bytes, 0 = no violation)
    uint16_t warmupTime_ms;     // Warmup duration in milliseconds (2 bytes)
    uint16_t persistTime_ms;    // Fault persistence time in milliseconds (2 bytes)
};

// ===== INPUT STRUCTURE =====
// Runtime configuration for a physical input pin
// Size: ~100 bytes per input
struct Input {
    // === Hardware (1 byte) ===
    uint8_t pin;                    // Physical pin (A0-A15, or digital, or 0xF0-0xFE for I2C virtual)
    // Note: Bus selection is global via SystemConfig.buses (not per-input)

    // === User Configuration ===
    char abbrName[8];               // "CHT", "OIL" (for LCD display)
    char displayName[32];           // "Cylinder Head Temperature" (full name)
    uint8_t applicationIndex;       // Index into APPLICATION_PRESETS[] array
    uint8_t sensorIndex;            // Index into SENSOR_LIBRARY[] array
    uint8_t unitsIndex;             // Index into UNITS_REGISTRY[] array
    
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

    // === Alarm State Management ===
    AlarmContext alarmContext;      // Alarm state machine context (12 bytes)
    AlarmSeverity currentSeverity;  // Current severity level (1 byte)

    // === Flags (packed into 1 byte) ===
    struct {
        uint8_t isEnabled : 1;      // Input enabled/disabled
        uint8_t alarm : 1;          // Alarm enabled
        uint8_t display : 1;        // Show on LCD
        uint8_t isInAlarm : 1;      // Currently in alarm state
        uint8_t useCustomCalibration : 1;  // Use custom or preset calibration
        uint8_t reserved : 3;       // Reserved for future use
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