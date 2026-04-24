/*
 * profile_teensy36.h — Build profile for Teensy 3.6
 *
 * RAM budget (256 KB available):
 *   Input array  : 32 × ~100 B = ~3200 B
 *   CLI buffers  : ~1600 B
 *   Remaining    : >250 KB — comfortable headroom
 */

#pragma message "Building with profile: profile_teensy36"

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
// Teensy 3.5/3.6 has 32 analog-capable pins; fewer inputs than T4.x to stay
// within 256 KB RAM.
#define MAX_INPUTS              32
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
