/*
 * profile_teensy41.h — Build profile for Teensy 4.1
 *
 * RAM budget (512 KB available):
 *   Input array  : 40 × ~100 B = ~4000 B
 *   CLI buffers  : CLI_BUFFER_SIZE(1280) + RX(128) + CMD(128) + HIST(64) = ~1600 B
 *   PID table    : 64 × 5 B = ~320 B
 *   Pin registry : 64 × 6 B = ~384 B
 *   CAN cache    : 16 × ~20 B = ~320 B
 *   Remaining    : >500 KB — ample headroom
 */

#pragma message "Building with profile: profile_teensy41"

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
// SERIAL_TX_BUFFER_SIZE / SERIAL_RX_BUFFER_SIZE must be real -D flags in platformio.ini
// (framework core TUs are not reached by -include).
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
