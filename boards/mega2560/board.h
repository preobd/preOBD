/*
 * boards/mega2560/board.h — Board definition for Arduino Mega 2560
 *
 * Hardware description + feature selection. Discrete WIRING lives in the
 * gitignored boards/mega2560/wiring.ini overlay. `-include`d before
 * boards/resolve.h. See boards/README.md.
 *
 * RAM budget: 8 KB HARD limit (target ≤99%). Every CEILING below is a real
 * RAM/flash limit, not a preference — see docs/feature_costs.md for measured
 * costs. SERIAL_*_BUFFER_SIZE live as -D flags in board.ini (core TUs aren't
 * reached by -include).
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAN CONTROLLER (MCP2515 via SPI — no native CAN on AVR) =====
#define PROFILE_HAS_NATIVE_CAN 0
#define CAN_CS_0               9
#define CAN_INT_0              2
#define CAN_CS_1               0xFF   // disabled — single MCP2515 on Mega
#define CAN_INT_1              0xFF
#define CAN_CS                 CAN_CS_0   // legacy aliases used by HAL/system code
#define CAN_INT                CAN_INT_0

// ===== CEILINGS (board genuinely can't — RAM/flash; see docs/feature_costs.md) =====
#define PLATFORM_NO_SD_LOGGING 1     // SD lib + buffers blow the 8 KB budget (and SD.h isn't in lib_deps)
// Also off by ceiling, simply left undefined (CHOICE flags default off):
//   ENABLE_ELM327 (+301 B), ENABLE_FRAMED_TRANSPORT (+1689 B), ENABLE_TEST_MODE
//   (+238 B) — each exceeds the headroom. ENABLE_REALDASH is off by choice (+36 B).

// ===== FEATURE CHOICES (enabled) =====
#define ENABLE_CAN             1
#define ENABLE_SERIAL_OUTPUT   1
#define ENABLE_LCD             1
#define ENABLE_ALARMS          1     // control engine (Rules/Actions/Annunciator)
#define ENABLE_BME280          1

// ===== SIZING (tuned to the 8 KB budget) =====
#define CONTROL_RAM_RULES_MAX   2    // control caps are a REAL SRAM limit on AVR (ARCHITECTURE §11.9)
#define CONTROL_RAM_ACTIONS_MAX 2
#define MAX_INPUTS              8
#define MAX_PID_ENTRIES         8
#define MAX_PIN_REGISTRY        24
#define CAN_CACHE_SIZE          8
#define MAX_SCAN_RESULTS        4

// ===== CLI BUFFER SIZES =====
#define CLI_RX_BUFFER_SIZE      128
#define CLI_CMD_BUFFER_SIZE     128
#define CLI_HISTORY_BUFFER_SIZE 64
#define CLI_MAX_BINDINGS        32
#define CLI_BUFFER_SIZE         820   // single biggest RAM lever on this board
#define CLI_MAX_ARGS            9     // longest cmd: set <name> rpm 4 <4 pairs> <max>
#define CLI_MAX_ARG_LEN         20

// ===== JSON CAPABILITY (CEILING — AVR RAM can't host it) =====
#define SUPPORTS_JSON_CONFIG        0
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG

// WIRED (set in wiring.ini, derived by boards/resolve.h):
//   RGB_PIN_R/G/B → ENABLE_LED   MODE_BUTTON_PIN → ENABLE_MODE_BUTTON   BUZZER_PIN
