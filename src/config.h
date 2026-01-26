/*
 * config.h - openEMS User Configuration
 *
 * IMPORTANT: Feature compilation (ENABLE_*) is controlled in platformio.ini
 * This file contains only runtime configuration values.
 *
 * QUICK START CHECKLIST:
 * □ Set hardware pins for your wiring
 * □ Adjust timing intervals if needed
 * □ Configure default units (Celsius/Fahrenheit, PSI/Bar, etc.)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// HARDWARE PIN ASSIGNMENTS
// ============================================================================
// Customize these for your specific wiring

// ----- System Control Pins -----
#define MODE_BUTTON 5   // Multi-function button:
                        //   Hold during boot → CONFIG mode
                        //   Press in RUN mode → Silence alarm
                        //   Hold in RUN mode → Toggle display

#define BUZZER 3        // Alarm buzzer output pin

// ----- LED Status Indicator Pins -----
// Only active when ENABLE_LEDS is defined in platformio.ini
#ifdef ENABLE_LEDS
    #define GREEN_LED 30      // Normal operation indicator
    #define YELLOW_LED 31     // Warning level indicator
    #define RED_LED 32       // Alarm level indicator
#endif

// ----- CAN Bus Pins -----
// Only needed if using external MCP2515 chip (not needed for Teensy native FlexCAN)
#ifndef USE_FLEXCAN_NATIVE
    #define CAN_CS 9        // MCP2515 chip select
    #define CAN_INT 2       // MCP2515 interrupt pin
#endif

// ----- SD Card Pins -----
// Can be overridden in platformio.ini with -D SD_CS_PIN=x
// Teensy 4.1 uses SD_CS_PIN=254 for built-in SD card (set in platformio.ini)
#ifndef SD_CS_PIN
    #define SD_CS_PIN 4     // Default SD card chip select for external modules
#endif

// ============================================================================
// DEFAULT UNITS
// ============================================================================
// Set your preferred display units
// Individual sensors can override these defaults

#define DEFAULT_TEMPERATURE_UNITS  "CELSIUS"     // or "FAHRENHEIT"
#define DEFAULT_PRESSURE_UNITS     "BAR"         // or "PSI", "KPA"
#define DEFAULT_ELEVATION_UNITS    "FEET"        // or "METERS"
#define DEFAULT_SPEED_UNITS        "MPH"         // or "KPH"

// NOTE: Voltage is always in VOLTS
// NOTE: Humidity is always in PERCENT
// NOTE: RPM is always in RPM

// ============================================================================
// ALARM CONFIGURATION
// ============================================================================

#define SILENCE_DURATION 30000       // ms (how long MODE_BUTTON mutes alarm)
#define WARNING_THRESHOLD_PERCENT 90 // Warning triggers at 90% of alarm threshold

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

#define DEFAULT_BIAS_RESISTOR 1000.0    // Default pull-down/up resistor (Ω)
                                         // for thermistor and polynomial sensors
#define SEA_LEVEL_PRESSURE_HPA 1013.25  // Sea level pressure for altitude calculations

// ============================================================================
// TIMING / PERFORMANCE TUNING
// ============================================================================
// Controls update frequency for different system components
// Lower values = more frequent updates = higher CPU usage

#define SENSOR_READ_INTERVAL_MS 50       // Fast sensors (20Hz) - responsive alarms
#define ALARM_CHECK_INTERVAL_MS 50       // Alarm checks (20Hz) - safety critical
#define CAN_OUTPUT_INTERVAL_MS 100       // CAN bus updates (10Hz) - smooth dashboards
#define REALDASH_INTERVAL_MS 100         // RealDash updates (10Hz)
#define LCD_UPDATE_INTERVAL_MS 500       // LCD display (2Hz) - human readable
#define SERIAL_CSV_INTERVAL_MS 1000      // Serial CSV (1Hz) - prevents buffer flooding
#define SD_LOG_INTERVAL_MS 5000          // SD logging (0.2Hz) - reduces wear

// ============================================================================
// TEST MODE CONFIGURATION
// ============================================================================
// Test mode allows testing outputs without physical sensors
// WARNING: Adds ~4KB flash - do NOT enable for Arduino Uno builds

#ifdef ENABLE_TEST_MODE
    #define TEST_MODE_TRIGGER_PIN 8      // Hold LOW during boot to activate
    #define DEFAULT_TEST_SCENARIO 0      // 0=Normal, 1=Alarms, 2=Faults, 3=Startup, 4=Driving
#endif

// ============================================================================
// OBD-II CONFIGURATION
// ============================================================================
// OBD-II request/response support for ELM327 Bluetooth adapters and apps like Torque
// Works in hybrid mode: simultaneous broadcast (RealDash) and request/response (scanners)

#ifdef ENABLE_CAN
    // Request/response mode is always enabled when CAN is enabled
    // No additional configuration needed - auto-responds to 0x7DF and 0x7E0 requests

    // Optional: Minimum interval between OBD-II requests (ms) to prevent bus flooding
    // Most scanners query sequentially at ~1-10 Hz, so this is rarely needed
    // #define OBD2_MIN_REQUEST_INTERVAL_MS 10  // Uncomment to enable rate limiting
#endif

// ============================================================================
// STATIC BUILD CONFIGURATION (Advanced)
// ============================================================================
// Generated by tools/configure.py - DO NOT EDIT MANUALLY
// Only used when USE_STATIC_CONFIG is defined in platformio.ini
// See docs/advanced/STATIC_BUILDS_GUIDE.md

#ifdef USE_STATIC_CONFIG
// ============================================================================
// STATIC SENSOR CONFIGURATION
// This block is auto-generated by tools/configure.py
// ============================================================================

// No static configuration defined.
// Run: python3 tools/configure.py

#endif // USE_STATIC_CONFIG

#endif // CONFIG_H
