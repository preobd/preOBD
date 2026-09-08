/*
 * input.h - Input-based sensor configuration
 *
 * Core structure for runtime-configurable sensor inputs.
 * Each Input represents a physical pin that can be assigned an Application (preset)
 * and Sensor (hardware device) at runtime via serial commands or EEPROM.
 *
 * ============================================================================
 * PIN ALLOCATION — inputs[i].pin encoding (SINGLE SOURCE OF TRUTH, #368)
 * ============================================================================
 *
 * Physical pins:      0x00-0x7F (0-127)   - Hardware GPIO pins, analog inputs
 * CAN virtual pins:    0xC0-0xDF (192-223) - CAN:0 to CAN:31 (32 sensors max)
 * I2C virtual pins:    0xF0-0xFC (240-252) - I2C:0 to I2C:12 (13 sensors max)
 * BUILTIN_SDCARD:      0xFE (254)          - Teensy SDIO reserved id
 *                                            (system_config.cpp, sd_manager.cpp).
 *                                            NEVER a valid Input pin.
 * Empty-slot sentinel: 0xFF (255)          - inputs[i].pin == 0xFF means the
 *                                            slot is free. Doubles as "this pin
 *                                            does not exist on this platform"
 *                                            in bus_defaults.h/pin_owner.cpp
 *                                            (ARCHITECTURE.md §12.5). NEVER a
 *                                            valid Input pin.
 *
 * 0xFD is unused/reserved padding between the I2C range and BUILTIN_SDCARD.
 *
 * The I2C cap is 13 slots (0xF0-0xFC), not 14 (0xF0-0xFD): both the free-slot
 * allocator (command_helpers.cpp parsePin(), "I2C" branch) and the "I2C:n"
 * lookup branch in the same function MUST agree with the PIN_I2C_* constants
 * below — a prior mismatch (allocator capped at 0xFC/13, "I2C:n" accepted up
 * to 0xFD/14) meant "I2C:13" could be *typed* but never *allocated* (#368).
 *
 * Virtual pins don't correspond to physical GPIO - they represent data sources
 * from bus protocols (CAN frames, I2C sensors with addresses, etc.)
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

// ===== PIN ENCODING CONSTANTS =====
// Single source of truth for the inputs[i].pin ranges documented above
// (#368). Consumers that need a literal should use these rather than a bare
// 0xC0/0xF0/0xFF, but the constants exist primarily so the static_asserts
// below catch a future range edit that overlaps two meanings.
static constexpr uint8_t PIN_PHYSICAL_MAX   = 0x7F;  // last hardware GPIO pin
static constexpr uint8_t PIN_CAN_BASE       = 0xC0;  // CAN:0
static constexpr uint8_t PIN_CAN_MAX        = 0xDF;  // CAN:31
static constexpr uint8_t PIN_I2C_BASE       = 0xF0;  // I2C:0
static constexpr uint8_t PIN_I2C_MAX        = 0xFC;  // I2C:12 (13 slots total)
static constexpr uint8_t PIN_BUILTIN_SDCARD = 0xFE;  // Teensy SDIO id, not an Input pin
static constexpr uint8_t PIN_SENTINEL_EMPTY = 0xFF;  // inputs[] free-slot sentinel

static_assert(PIN_PHYSICAL_MAX < PIN_CAN_BASE,
              "physical pin range must not overlap CAN virtual pins");
static_assert(PIN_CAN_MAX < PIN_I2C_BASE,
              "CAN virtual pin range must not overlap I2C virtual pins");
static_assert(PIN_I2C_MAX < PIN_BUILTIN_SDCARD,
              "I2C virtual pin range must not overlap BUILTIN_SDCARD");
static_assert(PIN_BUILTIN_SDCARD < PIN_SENTINEL_EMPTY,
              "BUILTIN_SDCARD must not collide with the empty-slot sentinel");

// ===== CALIBRATION OVERRIDE UNION =====
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
    
    RPMCalibration    rpm;
    SpeedCalibration  speed;
    CANSensorCalibration can;

    // Raw bytes for memset/EEPROM operations
    byte raw[16];
};
#if defined(__AVR__)
static_assert(sizeof(CalibrationOverride) == 16, "CalibrationOverride size changed on AVR — bump EEPROM_VERSION");
#else
static_assert(sizeof(CalibrationOverride) == 20, "CalibrationOverride size changed on ARM — bump EEPROM_VERSION");
#endif

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
    uint8_t pin;                    // Physical pin (A0-A15, or digital, or 0xF0-0xFC for I2C virtual; see PIN ALLOCATION above)
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

    // === Output Routing ===
    uint8_t outputMask;            // Per-input output routing (bits 0-3: CAN, RealDash, Serial, SD)

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

    // === Hardware Signal Conditioning ===
    // Voltage divider ratio = V_at_pin / V_at_sensor. 1.0 = no divider.
    // Set when a divider scales a 5V sensor down for a 3.3V ADC (e.g. 0.6
    // for a 2.2k/3.3k divider). 0 is treated as 1.0 for backwards compat
    // with EEPROM data written before this field existed.
    float divider_ratio;
};

#endif // INPUT_H