/*
 * profile_esp32s3_hybrid.h — Build profile for ESP32-S3 hybrid CAN
 *
 * Hybrid mode: ESP32 TWAI (bus 0, native) + MCP2515 (bus 1, SPI).
 * Same feature set as profile_esp32s3 with ENABLE_CAN_HYBRID added.
 * CAN bus type assignment stays here so platformio.ini stays clean.
 */

#pragma message "Building with profile: profile_esp32s3_hybrid"

// ===== FEATURE FLAGS =====
#define ENABLE_CAN
#define ENABLE_CAN_HYBRID
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

// ===== CAN BUS CONTROLLER TYPES =====
#define CAN_BUS_0_TYPE CanControllerType::TWAI
#define CAN_BUS_1_TYPE CanControllerType::MCP2515

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

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_IMPORT_STREAM 1
#define JSON_IMPORT_MAX_BYTES       16384
#define SUPPORTS_JSON_EXPORT        1
#define SUPPORTS_JSON_CONFIG        1
