/*
 * profile_mega2560.h — Build profile for Arduino Mega 2560
 *
 * This file is the authoritative board definition for the Mega 2560.
 * It owns all feature flags AND all hardware pin assignments.
 * config.h contains only application-level constants (timing, thresholds, etc.).
 *
 * Pin convention:
 *   ENABLE_X = 1          feature enabled (boolean; no hardware pin)
 *   ENABLE_X = 1 + X_PIN  feature enabled with a physical pin
 *   ENABLE_X not defined   feature disabled
 *
 * RAM budget (8 KB hard limit, target ≤99%):
 *   Input array  :  8 × ~100 B = ~800 B
 *   CLI buffers  : CLI_BUFFER_SIZE(820) + RX(128) + CMD(128) + HIST(64) = ~1140 B
 *   PID table    :  8 × 5 B   = ~40 B
 *   Pin registry : 24 × 6 B   = ~144 B
 *   CAN cache    :  8 × ~20 B = ~160 B
 *   Remaining    : ~5.7 KB for stack, globals, framework overhead
 *
 * Disabled features and why:
 *   ENABLE_SD_LOGGING    — SD + ArduinoJson push RAM above 8 KB limit
 *   ENABLE_REALDASH      — requires CAN broadcast structs not needed on Mega
 *   ENABLE_ELM327        — BLE serial stack too large for AVR RAM
 *   ENABLE_RELAY_OUTPUT  — saves ~200 B RAM; relay pins can be wired directly
 *   ENABLE_TEST_MODE     — saves ~4 KB flash; not needed in production Mega builds
 */


// ===== CAN CONTROLLER =====
// MCP2515 via SPI — no native CAN peripheral on AVR.
#define PROFILE_HAS_NATIVE_CAN 0

// ===== FEATURE FLAGS =====
#define ENABLE_CAN             1
#define ENABLE_SERIAL_OUTPUT   1
#define ENABLE_LCD             1
#define ENABLE_ALARMS          1
#define ENABLE_LED             1
#define ENABLE_BME280          1

// ===== HARDWARE PIN ASSIGNMENTS =====
// Customize these to match your physical wiring.

// Mode button — hold during boot for CONFIG mode, press to silence alarm
#define ENABLE_MODE_BUTTON     1
#define MODE_BUTTON_PIN        5

// Alarm buzzer output
#define ALARMS_PIN             3

// RGB LED status indicator (PWM-capable pins required)
#define RGB_PIN_R              11
#define RGB_PIN_G              12
#define RGB_PIN_B              13
// #define RGB_COMMON_ANODE       // uncomment for common-anode LED wiring

// SPI CAN controller (MCP2515) — bus 0 (primary)
#define CAN_CS_0               9
#define CAN_INT_0              2

// SPI CAN controller — bus 1 (secondary); set CAN_CS_1 to 0xFF to disable
#define CAN_CS_1               0xFF   // disabled — single MCP2515 on Mega
#define CAN_INT_1              0xFF

// Legacy aliases used by HAL and system code
#define CAN_CS                 CAN_CS_0
#define CAN_INT                CAN_INT_0


// ===== SIZING =====
// All values tuned to keep firmware within the 8 KB RAM budget.
// CLI_MAX_ARGS=9: enough for the longest command (set <name> rpm 4 <4 pairs> <max>).
#define MAX_INPUTS              8
#define MAX_PID_ENTRIES         8
#define MAX_PIN_REGISTRY        24
#define CAN_CACHE_SIZE          8
#define MAX_SCAN_RESULTS        4

// ===== CLI BUFFER SIZES =====
// Framework ring-buffer sizes are -D flags in platformio.ini (not here) because
// -include does not reach framework core TUs. Authoritative values for this env:
//   SERIAL_TX_BUFFER_SIZE=16, SERIAL_RX_BUFFER_SIZE=32
// Wire BUFFER_LENGTH is not tunable — AVR Wire.h hard-codes it without #ifndef.
#define CLI_RX_BUFFER_SIZE      128
#define CLI_CMD_BUFFER_SIZE     128
#define CLI_HISTORY_BUFFER_SIZE 64
#define CLI_MAX_BINDINGS        32
#define CLI_BUFFER_SIZE         820
#define CLI_MAX_ARGS            9
#define CLI_MAX_ARG_LEN         20

// ===== JSON CAPABILITY =====
// AVR has insufficient RAM for JSON config, import, or export.
#define SUPPORTS_JSON_CONFIG        0
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
