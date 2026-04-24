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

// ===== CLI BUFFER SIZES =====
// serial_config.cpp is excluded when USE_STATIC_CONFIG is set; these are defined
// for completeness but are never compiled.
#define CLI_RX_BUFFER_SIZE      64
#define CLI_CMD_BUFFER_SIZE     64
#define CLI_HISTORY_BUFFER_SIZE 32
#define CLI_MAX_BINDINGS        16
#define CLI_BUFFER_SIZE         512
#define CLI_MAX_ARGS            6
#define CLI_MAX_ARG_LEN         16

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_IMPORT_STREAM 0
#define SUPPORTS_JSON_EXPORT        0
#define SUPPORTS_JSON_CONFIG        0
