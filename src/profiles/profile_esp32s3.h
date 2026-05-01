/*
 * profile_esp32s3.h — Build profile for ESP32-S3
 *
 * This file is the authoritative board definition for the ESP32-S3.
 * It owns all feature flags AND all hardware pin assignments.
 * config.h contains only application-level constants (timing, thresholds, etc.).
 *
 * Pin convention:
 *   ENABLE_X = 1          feature enabled (boolean; no hardware pin)
 *   ENABLE_X = 1 + X_PIN  feature enabled with a physical pin
 *   ENABLE_X not defined   feature disabled
 *
 * RAM budget (512 KB available, ~320 KB usable after IDF overhead):
 *   Input array  : 32 × ~100 B = ~3200 B
 *   CLI buffers  : ~1600 B
 *   Remaining    : >310 KB — comfortable headroom
 */


// ===== FEATURE FLAGS =====
#define ENABLE_CAN             1
#define ENABLE_REALDASH        1
#define ENABLE_SERIAL_OUTPUT   1
#define ENABLE_SD_LOGGING      1
#define ENABLE_LCD             1
#define ENABLE_ALARMS          1
#define ENABLE_LED             1
#define ENABLE_TEST_MODE       1
#define ENABLE_BME280          1
#define ENABLE_RELAY_OUTPUT    1
#define MAX_RELAYS             2
#define ENABLE_ELM327          1

// ===== HARDWARE PIN ASSIGNMENTS =====
// Customize these to match your physical wiring.
// ESP32-S3 GPIO numbering — verify PWM capability for LED pins.

// Mode button — hold during boot for CONFIG mode, press to silence alarm
#define ENABLE_MODE_BUTTON     1
#define MODE_BUTTON_PIN        5

// Alarm buzzer output
#define ALARMS_PIN             3

// SD card — external module CS pin
#define SUPPORTS_SD            1
#define SD_PIN                 4

// RGB LED status indicator (PWM-capable GPIO required)
#define RGB_PIN_R              11
#define RGB_PIN_G              12
#define RGB_PIN_B              13
// #define RGB_COMMON_ANODE       // uncomment for common-anode LED wiring

// Test mode trigger — hold LOW during boot to activate
#define TEST_MODE_PIN          8


// ===== SIZING =====
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

// ===== CAN CONTROLLER =====
#define PROFILE_HAS_NATIVE_CAN 1

// ===== EEPROM =====
// ESP32 uses flash-emulated EEPROM; E2END is not defined. Set the size here.
#define EEPROM_TOTAL_BYTES 4096

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_CONFIG        1
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
#define JSON_IMPORT_MAX_BYTES       16384
