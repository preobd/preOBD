/*
 * config.h - preOBD Application Constants
 *
 * This file contains ONLY application-level constants: timing, thresholds,
 * calibration defaults, and protocol configuration.
 *
 * Hardware pin assignments live in the board profile (src/profiles/profile_*.h),
 * which is included first via -include in platformio.ini. The profile is the
 * single source of truth for what hardware is wired and where.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// DEFAULT UNITS
// ============================================================================
// Set your preferred display units.
// Individual sensors can override these defaults.

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

#define SILENCE_DURATION 30000       // ms (how long MODE_BUTTON_PIN mutes alarm)
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
// Controls update frequency for different system components.
// Lower values = more frequent updates = higher CPU usage.

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
// Test mode allows testing outputs without physical sensors.
// WARNING: Adds ~4KB flash.
// TEST_MODE_PIN (the trigger pin) is defined in the board profile.

#if ENABLE_TEST_MODE
    #define DEFAULT_TEST_SCENARIO 0  // 0=Normal, 1=Alarms, 2=Faults, 3=Startup, 4=Driving
#endif

// ============================================================================
// OBD-II CONFIGURATION
// ============================================================================
// OBD-II request/response support for ELM327 adapters and apps like Torque.
// Works in hybrid mode: simultaneous broadcast (RealDash) and request/response.

#if ENABLE_CAN
    // Optional: Minimum interval between OBD-II requests (ms) to prevent bus flooding.
    // Most scanners query sequentially at ~1-10 Hz, so this is rarely needed.
    // #define OBD2_MIN_REQUEST_INTERVAL_MS 10  // Uncomment to enable rate limiting
#endif

#endif // CONFIG_H
