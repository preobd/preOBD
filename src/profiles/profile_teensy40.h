/*
 * profile_teensy40.h — Build profile for Teensy 4.0
 *
 * Identical feature set to Teensy 4.1; no built-in SD (SD_CS_PIN not set here —
 * hardware pin config stays in platformio.ini / config.h).
 * RAM budget same as Teensy 4.1 (512 KB).
 */


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

// ===== CAN CONTROLLER =====
#define PROFILE_HAS_NATIVE_CAN 1

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_CONFIG        1
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
#define JSON_IMPORT_MAX_BYTES       16384
