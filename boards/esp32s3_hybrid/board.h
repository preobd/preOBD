/*
 * boards/esp32s3_hybrid/board.h — ESP32-S3 with hybrid CAN (TWAI + MCP2515)
 *
 * Hardware description + feature selection. WIRING lives in the gitignored
 * boards/esp32s3_hybrid/wiring.ini overlay. `-include`d before boards/resolve.h.
 * See boards/README.md.
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAN CONTROLLER (hybrid: TWAI bus 0 + MCP2515 bus 1) =====
#define PROFILE_HAS_NATIVE_CAN 1
#define SUPPORTS_CAN_HYBRID      1
#define CAN_BUS_0_TYPE         CanControllerType::TWAI
#define CAN_BUS_1_TYPE         CanControllerType::MCP2515
#define CAN_CS_0               9      // MCP2515 (bus 1)
#define CAN_INT_0              2
#define CAN_CS_1               0xFF   // disabled — one MCP2515 in hybrid mode
#define CAN_INT_1              0xFF
#define CAN_CS                 CAN_CS_0   // legacy aliases used by HAL/system code
#define CAN_INT                CAN_INT_0

// ===== CAPABILITY =====
#define EEPROM_TOTAL_BYTES     4096   // flash-emulated EEPROM (E2END undefined on ESP32)
// No HAS_BUILTIN_SD: external SD module — set SD_PIN in the overlay to enable.

// ===== FEATURE CAPABILITY (what this board CAN do) =====
// Declared by presence: a board that can't simply omits the line. Never 0.
// boards/resolve.h turns each of these into the INCLUDE_* flag src/ tests.
// To reclaim a feature's flash/RAM on this board, comment its line out here —
// measured per-feature costs are in docs/development/FEATURE_COSTS.md. (For a one-off local
// build, put `-D INCLUDE_<X>=0` in your gitignored wiring.ini instead.)
#define SUPPORTS_CAN              1
#define SUPPORTS_REALDASH         1
#define SUPPORTS_SERIAL_OUTPUT    1
#define SUPPORTS_CLI_EDITING      1    // embedded-cli: history + in-line editing (#147)
#define SUPPORTS_FRAMED_TRANSPORT 1
#define SUPPORTS_LCD              1
#define SUPPORTS_ALARMS           1    // control engine (Rules/Actions/Annunciator)
#define SUPPORTS_BME280           1
#define SUPPORTS_RELAY_OUTPUT     1    // deprecated shim (→ control-engine Actions)
#define SUPPORTS_ELM327           1
#define SUPPORTS_TEST_MODE        1

// ===== SIZING =====
#define MAX_INPUTS              32
#define MAX_PID_ENTRIES         64
#define MAX_PIN_REGISTRY        64
#define MAX_RELAYS              2
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
#define SUPPORTS_JSON_CONFIG        1
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
#define JSON_IMPORT_MAX_BYTES       16384

// WIRED (set in wiring.ini, derived by boards/resolve.h):
//   RGB_PIN_R/G/B → SUPPORTS_LED   MODE_BUTTON_PIN → SUPPORTS_MODE_BUTTON
//   BUZZER_PIN   external SD_PIN → SUPPORTS_SD_LOGGING   TEST_MODE_PIN (trigger)
