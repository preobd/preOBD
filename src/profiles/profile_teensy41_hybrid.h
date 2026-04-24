/*
 * profile_teensy41_hybrid.h — Build profile for Teensy 4.1 hybrid CAN
 *
 * Hybrid mode: FlexCAN buses 0–2 (native) + MCP2515 (bus 3, SPI).
 * Same feature set as profile_teensy41 with ENABLE_CAN_HYBRID added.
 */

#pragma message "Building with profile: profile_teensy41_hybrid"

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
#define CAN_BUS_0_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_1_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_2_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_3_TYPE CanControllerType::MCP2515

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
