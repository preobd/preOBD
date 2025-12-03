/*
 * config.h - User Configuration File
 *
 * QUICK START CHECKLIST:
 * □ Choose build mode (line 41): EEPROM or compile-time?
 * □ Set hardware pins (line 48): MODE_BUTTON, BUZZER, etc.
 * □ Enable output modules (line 65): CAN? Serial? SD logging?
 * □ Enable display (line 88): LCD or OLED?
 * □ Configure sensors (line 140+): Only if using USE_STATIC_CONFIG
 *
 * === BUILD MODE SELECTION ===
 * Choose ONE of the following modes:
 *
 * MODE 1: EEPROM/Serial Config
 *   - Runtime serial configuration via commands
 *   - EEPROM persistence for settings
 *   - Recommended for: Teensy 4.x, ESP32, Arduino Mega
 *   To enable: Leave USE_STATIC_CONFIG commented out
 *
 * MODE 2: Compile-Time Config
 *   - Configure sensors at compile time in config.h
 *   - No EEPROM, no serial config overhead
 *   - Smaller footprint than Mode 1
 *   - Recommended for: Arduino Uno, memory-constrained boards
 *   To enable: #define USE_STATIC_CONFIG
 *
 * Both modes use the same Input-based architecture and share all code.
 * The only difference is where configuration comes from:
 *   Mode 1: EEPROM (set via serial commands)
 *   Mode 2: config.h (set at compile time)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// STEP 1: BUILD MODE SELECTION
// ============================================================================

// Uncomment to use compile-time config instead of EEPROM:
#define USE_STATIC_CONFIG

// ============================================================================
// STEP 2: HARDWARE PIN ASSIGNMENTS
// ============================================================================

// ----- System Control Pins -----
#define MODE_BUTTON 5   // Multi-function: Hold during boot for CONFIG mode,
                        // press in RUN mode to silence alarm

#define BUZZER 3        // Alarm buzzer output

// ----- CAN Bus Pins (only needed if using external MCP2515 chip) -----
#define CAN_CS 9        // MCP2515 chip select
#define CAN_INT 2       // MCP2515 interrupt pin

// ----- SD Card Pins -----
#define SD_CS_PIN 4     // SD card chip select (adjust for your hardware)

// ============================================================================
// STEP 3: ENABLED MODULES
// ============================================================================

// ----- Output Modules -----
// Uncomment the modules you want to use
//#define ENABLE_CAN
//#define ENABLE_REALDASH
//#define ENABLE_SERIAL_OUTPUT
//#define ENABLE_SD_LOGGING

// ----- CAN Configuration (only relevant if ENABLE_CAN is defined) -----
// Choose CAN implementation:
//
// For Teensy 3.x/4.x boards:
//   - USE_FLEXCAN_NATIVE: Use built-in FlexCAN peripheral (no external chip needed)
//     * Teensy 4.0/4.1: CAN1 TX=22, RX=23 (also CAN2 and CAN3 available)
//     * Teensy 3.2/3.5/3.6: CAN1 TX=3, RX=4 (CAN2 available on 3.6)
//   - Leave undefined: Use external MCP2515 chip via SPI (requires CAN_CS and CAN_INT pins)
//
// For other boards (Arduino Mega, Uno, Due):
//   - Must use external MCP2515 chip (USE_FLEXCAN_NATIVE not supported)
//
//#define USE_FLEXCAN_NATIVE  // Uncomment to use built-in FlexCAN on Teensy boards

// ----- Display Modules -----
// Choose ONE display type (or comment both out for no display)
#define ENABLE_LCD
//#define ENABLE_OLED

// ----- BME280 Environmental Sensor Support -----
// BME280 is an I2C sensor that provides temperature, pressure, and humidity
// The library is always included, but unused code is optimized out by the linker
// if you don't configure any BME280 sensor inputs below
#define USE_BME280

// ============================================================================
// STEP 4: DISPLAY UNITS DEFAULTS
// ============================================================================

// Set your preferred units for each measurement type
// Individual sensors can override these defaults in the sensor definitions section

// Temperature default (CELSIUS or FAHRENHEIT)
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS

// Pressure default (BAR, PSI, or KPA)
#define DEFAULT_PRESSURE_UNITS     BAR

// Elevation default (METERS or FEET)
#define DEFAULT_ELEVATION_UNITS     FEET

// NOTE: Voltage is always displayed in VOLTS
// NOTE: Humidity is always displayed in PERCENT

// ============================================================================
// STEP 5: SENSOR CONFIGURATION (Compile-Time Mode Only)
// ============================================================================

// This section is ONLY used when USE_STATIC_CONFIG is defined above.
// For EEPROM mode (MODE 1), configure sensors via serial commands instead.
//
// Each input needs three defines:
//   INPUT_N_PIN         - Physical pin (A0-A15 for analog, or digital pin)
//   INPUT_N_APPLICATION - What you're measuring (from Application enum)
//   INPUT_N_SENSOR      - Hardware type (from Sensor enum)
//
// Optional per-input overrides:
//   INPUT_N_UNITS       - Override display units (e.g., FAHRENHEIT)
//
// The system auto-populates display names, alarm thresholds, OBD-II PIDs,
// and calibration data from the application presets and sensor library.
//
// For custom calibrations, see advanced_config.h

#ifdef USE_STATIC_CONFIG
    // Include the enums we need from the input-based architecture
    #include "inputs/input.h"

    // ----- Input Definitions -----

    // Input 0: CHT (Cylinder Head Temperature)
    #define INPUT_0_PIN         6
    #define INPUT_0_APPLICATION CHT
    #define INPUT_0_SENSOR      MAX6675

    // Input 1: EGT (Exhaust Gas Temperature)
    #define INPUT_1_PIN         7
    #define INPUT_1_APPLICATION EGT
    #define INPUT_1_SENSOR      MAX31855

    // Input 2: Coolant Temperature
    #define INPUT_2_PIN         A2
    #define INPUT_2_APPLICATION COOLANT_TEMP
    #define INPUT_2_SENSOR      VDO_120C_LOOKUP

    // Input 3: Oil Temperature
    #define INPUT_3_PIN         A0
    #define INPUT_3_APPLICATION OIL_TEMP
    #define INPUT_3_SENSOR      VDO_150C_STEINHART

    // Input 4: Primary Battery
    #define INPUT_4_PIN         A5
    #define INPUT_4_APPLICATION PRIMARY_BATTERY
    #define INPUT_4_SENSOR      STANDARD_12V_DIVIDER

    // Input 5: Ambient Temperature (BME280)
    #define INPUT_5_PIN         0        // BME280 is I2C, no analog pin
    #define INPUT_5_APPLICATION AMBIENT_TEMP
    #define INPUT_5_SENSOR      BME280_AMBIENT_TEMPERATURE

    // Input 6: Barometric Pressure (BME280)
    #define INPUT_6_PIN         0        // BME280 is I2C
    #define INPUT_6_APPLICATION BAROMETRIC_PRESSURE
    #define INPUT_6_SENSOR      BME280_BAROMETRIC_PRESSURE

    // Input 7: Humidity (BME280)
    #define INPUT_7_PIN         0        // BME280 is I2C
    #define INPUT_7_APPLICATION HUMIDITY
    #define INPUT_7_SENSOR      BME280_RELATIVE_HUMIDITY

    // ----- Optional: Unit Overrides -----
    // By default, units come from ApplicationPreset defaults
    // Uncomment to override for specific inputs:

    // #define INPUT_0_UNITS FAHRENHEIT  // Override CHT to F
    // #define INPUT_2_UNITS FAHRENHEIT  // Override Coolant to F
    #define INPUT_6_UNITS INHG        // Override baro to inHg

#endif // USE_STATIC_CONFIG

// ============================================================================
// STEP 6: ALARM CONFIGURATION
// ============================================================================

#define ENABLE_ALARMS               // Comment out to globally disable all alarms
#define SILENCE_DURATION 30000      // ms (how long MODE_BUTTON mutes alarm)

// ============================================================================
// STEP 7: CALIBRATION CONSTANTS
// ============================================================================

#define VDO_BIAS_RESISTOR 1000.0        // Default pull-down resistor for VDO sensors (Ω)
#define SEA_LEVEL_PRESSURE_HPA 1013.25  // Sea level pressure for altitude calculations

// ============================================================================
// STEP 8: TIMING / PERFORMANCE TUNING
// ============================================================================

// Controls update frequency for different system components
// Lower values = more frequent updates = higher CPU usage

#define SENSOR_READ_INTERVAL_MS 50       // Default read interval for fast sensors (20Hz)
                                         // Fast enough for responsive alarms

#define ALARM_CHECK_INTERVAL_MS 50      // Check alarms every 50ms (20Hz)
                                         // Safety critical - frequent checks

#define CAN_OUTPUT_INTERVAL_MS 100      // CAN bus updates every 100ms (10Hz)
                                         // Smooth dashboard updates

#define REALDASH_INTERVAL_MS 100        // RealDash updates every 100ms (10Hz)
                                         // Same as CAN for smooth mobile dashboard

#define LCD_UPDATE_INTERVAL_MS 500      // LCD display every 500ms (2Hz)
                                         // Our eyes can't read faster anyway

#define SERIAL_CSV_INTERVAL_MS 1000     // Serial CSV every 1000ms (1Hz)
                                         // Prevents flooding serial buffer

#define SD_LOG_INTERVAL_MS 5000         // SD card logging every 5000ms (0.2Hz)
                                         // Reduces file size and SD wear

// ============================================================================
// TEST MODE (Optional - for developers/testing only)
// ============================================================================

// Uncomment to enable test mode (allows testing outputs without physical sensors)
// Test mode uses function pointer substitution to inject simulated sensor values
// Memory overhead when enabled: 4.3KB flash, 185 bytes RAM
// Memory overhead when disabled: 0 bytes (completely removed by preprocessor)
#define ENABLE_TEST_MODE

#ifdef ENABLE_TEST_MODE
    // Test mode trigger pin (hold LOW during boot to activate test mode)
    #define TEST_MODE_TRIGGER_PIN 8

    // Default test scenario to run on startup (0-4, or 0xFF for none)
    // 0 = Normal Operation
    // 1 = Alarm Test - Overheating
    // 2 = Sensor Fault Simulation
    // 3 = Engine Startup Sequence
    // 4 = Dynamic Driving Conditions
    #define DEFAULT_TEST_SCENARIO 0
#endif

#endif