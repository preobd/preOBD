/*
 * profile_uno_static.h — Build profile for Arduino Uno (static config only)
 *
 * RAM budget (2 KB hard limit):
 *   Input array  :  6 × ~100 B = ~600 B
 *   Remaining    : ~1.4 KB for stack, globals, framework overhead
 *
 * USE_STATIC_CONFIG is set via platformio.ini -D (not here) because it gates
 * serial_config.cpp compilation and must be visible to build_src_filter logic.
 *
 * Disabled features and why:
 *   ENABLE_CAN         — MCP2515 + HAL too large for 32 KB flash / 2 KB RAM
 *   ENABLE_SD_LOGGING  — SD + ArduinoJson far exceed 2 KB RAM
 *   ENABLE_REALDASH    — requires CAN
 *   ENABLE_ELM327      — requires CAN
 *   ENABLE_BME280      — saves flash on extremely constrained board
 *   ENABLE_RELAY_OUTPUT— saves RAM
 *   ENABLE_TEST_MODE   — saves flash
 *   ENABLE_LED         — saves flash; LED pin can be driven directly
 */

#pragma message "Building with profile: profile_uno_static"

// ===== FEATURE FLAGS =====
#define ENABLE_LCD
#define ENABLE_SERIAL_OUTPUT
#define ENABLE_ALARMS

// ===== SIZING =====
#define MAX_INPUTS              6
#define MAX_PID_ENTRIES         6
#define MAX_PIN_REGISTRY        16
#define CAN_CACHE_SIZE          4
#define MAX_SCAN_RESULTS        4

// ===== CLI =====
// serial_config.cpp is excluded when USE_STATIC_CONFIG is set; no CLI on this build.

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_CONFIG        0
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
