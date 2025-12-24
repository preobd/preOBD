/*
 * config.h - openEMS Configuration
 *
 * QUICK START CHECKLIST:
 * □ Set hardware pins: MODE_BUTTON, BUZZER, etc.
 * □ Enable output modules: CAN? Serial? SD logging?
 * □ Enable display: LCD or OLED?
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// HARDWARE PIN ASSIGNMENTS
// ============================================================================

// ----- System Control Pins -----
#define MODE_BUTTON 5   // Multi-function: Hold during boot for CONFIG mode,
                        // hold briefly during RUN mode to toggle display on/off,
                        // press in RUN mode to silence alarm

#define BUZZER 3        // Alarm buzzer output

// ----- CAN Bus Pins (only needed if using external MCP2515 chip) -----
#define CAN_CS 9        // MCP2515 chip select
#define CAN_INT 2       // MCP2515 interrupt pin

// ----- SD Card Pins -----
#define SD_CS_PIN 4     // SD card chip select (adjust for your hardware)
//#define SD_CS_PIN BUILTIN_SDCARD  // Use built-in SD card on supported boards (Teensy 4.x)

// ============================================================================
// OUTPUT MODULES
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

// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================

// ----- Display Modules -----
// Choose ONE display type (or comment both out for no display)
#define ENABLE_LCD
//#define ENABLE_OLED

// Set your preferred units for each measurement type
// Individual sensors can override these defaults in the sensor definitions section
// These are resolved to indices at boot time via the units registry

// Temperature default ("CELSIUS" or "FAHRENHEIT")
#define DEFAULT_TEMPERATURE_UNITS  "CELSIUS"

// Pressure default ("BAR", "PSI", or "KPA")
#define DEFAULT_PRESSURE_UNITS     "BAR"

// Elevation default ("METERS" or "FEET")
#define DEFAULT_ELEVATION_UNITS    "FEET"

// NOTE: Voltage is always displayed in VOLTS
// NOTE: Humidity is always displayed in PERCENT

// ============================================================================
// ALARM CONFIGURATION
// ============================================================================

#define ENABLE_ALARMS               // Comment out to globally disable all alarms
#define SILENCE_DURATION 30000      // ms (how long MODE_BUTTON mutes alarm)

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

#define DEFAULT_BIAS_RESISTOR 1000.0    // Default pull-down resistor for thermistor and polynomial pressure sensors (Ω)
#define SEA_LEVEL_PRESSURE_HPA 1013.25  // Sea level pressure for altitude calculations

// ============================================================================
// TIMING / PERFORMANCE TUNING
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
// WARNING: Do NOT enable for Uno builds - uses 4.3KB flash that Uno doesn't have
//#define ENABLE_TEST_MODE

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

// ============================================================================
// STATIC BUILD CONFIGURATION (Optional)
// ============================================================================
// This section is generated by tools/configure.py
// DO NOT EDIT MANUALLY - Run configure.py to regenerate
//
// To create a static build:
//   python3 tools/configure.py
//
// See docs/advanced/STATIC_BUILDS_GUIDE.md for details.
// Uncomment USE_STATIC_CONFIG to enable static builds.
// When enabled, sensors are configured at compile time (below)
// and serial configuration commands are disabled.
// 
// Most users should leave this commented out and use serial commands.
// 
// #define USE_STATIC_CONFIG

#ifdef USE_STATIC_CONFIG
// ============================================================================
// STATIC SENSOR CONFIGURATION
// This block is auto-generated by tools/configure.py
// Run: python3 tools/configure.py
// ============================================================================

// No static configuration defined.
// Run tools/configure.py to generate sensor configuration.

#endif // USE_STATIC_CONFIG

#endif // CONFIG_H
