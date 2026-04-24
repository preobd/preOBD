/*
 * profile_debug.h — Build profile for debug environment (Teensy 4.1, -Og -g)
 *
 * Identical features and sizing to profile_teensy41; compiled with debug symbols
 * and extra warnings. Separate profile so future debug-only tuning has a home.
 */

#pragma message "Building with profile: profile_debug"

// ===== FEATURE FLAGS =====
#define ENABLE_CAN
#define ENABLE_REALDASH
#define ENABLE_SERIAL_OUTPUT
#define ENABLE_SD_LOGGING
#define ENABLE_LCD
#define ENABLE_ALARMS
#define ENABLE_LED
#define ENABLE_TEST_MODE
#define ENABLE_BME280
#define ENABLE_RELAY_OUTPUT
#define MAX_RELAYS 2
#define ENABLE_ELM327

// ===== SIZING =====
#define MAX_INPUTS              40
#define MAX_PID_ENTRIES         64
#define MAX_PIN_REGISTRY        64
#define CAN_CACHE_SIZE          16
#define MAX_SCAN_RESULTS        32

// ===== CLI BUFFER SIZES =====
#define CLI_RX_BUFFER_SIZE      128
#define CLI_CMD_BUFFER_SIZE     128
#define CLI_HISTORY_BUFFER_SIZE 64
#define CLI_MAX_BINDINGS        32
#define CLI_BUFFER_SIZE         1280
#define CLI_MAX_ARGS            16
#define CLI_MAX_ARG_LEN         32

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_IMPORT_STREAM 1
#define JSON_IMPORT_MAX_BYTES       16384
#define SUPPORTS_JSON_EXPORT        1
#define SUPPORTS_JSON_CONFIG        1
