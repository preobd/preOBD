/*
 * config.h - preOBD Application Constants
 *
 * This file contains ONLY application-level constants: timing, thresholds,
 * calibration defaults, and protocol configuration.
 *
 * Hardware pin assignments live in your board's wiring overlay
 * (boards/<board>/wiring.ini), with board facts/feature flags in
 * boards/<board>/board.h — both included first via -include in platformio.ini.
 * See boards/README.md for the model.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Migrated boards (boards/<board>/board.h, #283) define PREOBD_BOARD_DEFINED and
// rely on boards/resolve.h to derive their INCLUDE_* flags. Catch an env
// that -includes the board but forgot the resolver (or got the order wrong).
// Legacy src/profiles/*.h boards don't set PREOBD_BOARD_DEFINED → guard skipped.
#if defined(PREOBD_BOARD_DEFINED) && !defined(PREOBD_RESOLVED)
#  error "board.h was -included without boards/resolve.h — check this env's build_flags order in platformio.ini"
#endif

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

// Default acknowledgement window (ms) — how long ALARM SILENCE / the mode
// button mutes an alarm. 0 = hold until that rule stops sounding, which is
// what a broken sensor on a long trip needs: one press, not one every 30 s
// (#433 follow-up). A timed window survives a clear-and-retrip inside it, so
// a flapping sensor is quieter under `ALARM DURATION <secs>`; that policy is
// runtime-configurable and persisted, this is only what an unset config gets.
#define SILENCE_DURATION 0
#define WARNING_THRESHOLD_PERCENT 90 // Warning triggers at 90% of alarm threshold

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

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
#define LCD_PAGE_INTERVAL_MS 4000        // >8 shown inputs: rotate pages of 8 this often (#480)
#define SERIAL_CSV_INTERVAL_MS 1000      // Serial CSV (1Hz) - prevents buffer flooding
#define SD_LOG_INTERVAL_MS 5000          // SD logging (0.2Hz) - reduces wear

// ============================================================================
// TEST MODE CONFIGURATION
// ============================================================================
// Test mode allows testing outputs without physical sensors.
// WARNING: Adds ~4KB flash.
// TEST_MODE_PIN (the trigger pin) is defined in the board profile.

#if INCLUDE_TEST_MODE
    #define DEFAULT_TEST_SCENARIO 0  // 0=Normal, 1=Alarms, 2=Faults, 3=Startup, 4=Driving
#endif

// ============================================================================
// DIAG INTEGRITY CHECK  (gate kept named INCLUDE_SELFTEST for profile compat)
// ============================================================================
// Backs the `SYSTEM DIAG INTEGRITY` sub-command: walks every dispatch table and
// validates invariants (token strings non-null, uppercase, in-bounds; handlers
// non-null). Catches PROGMEM-access regressions the moment it's run after
// flashing. Adds ~50 bytes RAM and ~700 bytes flash; disable on RAM-tight
// builds (DIAG INTEGRITY then reports "disabled in this build").
#ifndef INCLUDE_SELFTEST
    #define INCLUDE_SELFTEST 1
#endif

// ============================================================================
// WATCHDOG BENCH STALL (#543)
// ============================================================================
// `SYSTEM DIAG WATCHDOG STALL <ms> [SD]` busy-waits without feeding the
// watchdog, to check on the bench that it still resets the board (and that
// the SD window widens it). It resets the board on purpose, so release images
// never include it: it's on in the debug env, and for a normal bench build
//   PLATFORMIO_BUILD_FLAGS="-D INCLUDE_WATCHDOG_STALL=1" pio run -e teensy41
// (the debug env's -Og image didn't boot on a Teensy 4.1 when tried; #544).
#ifndef INCLUDE_WATCHDOG_STALL
    #if defined(DEBUG)
        #define INCLUDE_WATCHDOG_STALL 1
    #else
        #define INCLUDE_WATCHDOG_STALL 0
    #endif
#endif

// ============================================================================
// OBD-II CONFIGURATION
// ============================================================================
// OBD-II request/response support for ELM327 adapters and apps like Torque.
// Works in hybrid mode: simultaneous broadcast (RealDash) and request/response.

#if INCLUDE_CAN
    // Optional: Minimum interval between OBD-II requests (ms) to prevent bus flooding.
    // Most scanners query sequentially at ~1-10 Hz, so this is rarely needed.
    // #define OBD2_MIN_REQUEST_INTERVAL_MS 10  // Uncomment to enable rate limiting
#endif

#endif // CONFIG_H
