/*
 * boards/teensy40/board.h — Board definition for Teensy 4.0
 *
 * Hardware description + feature selection. Discrete WIRING lives in the
 * gitignored boards/teensy40/wiring.ini overlay, not here. `-include`d before
 * boards/resolve.h, which derives the INCLUDE_* flags src/ tests. See boards/README.md.
 *
 * vs Teensy 4.1: no built-in SD slot (SD logging is off unless you wire an
 * external card and set SD_PIN in the overlay — it's capable, just not built-in).
 * RAM budget 512 KB. EEPROM is only 1080 B, so the persistence cap is ~10 inputs.
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAPABILITY (board facts) =====
#define PROFILE_HAS_NATIVE_CAN 1     // integrated FlexCAN
// No HAS_BUILTIN_SD: the 4.0 has no built-in slot. SD logging stays off unless
// an external card is wired (set SD_PIN in wiring.ini → resolve.h enables it).

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
#define SUPPORTS_FRAMED_TRANSPORT 1    // nRF52840 coprocessor framed link + broadcasters
#define SUPPORTS_LCD              1
#define SUPPORTS_ALARMS           1    // control engine (Rules/Actions/Annunciator) — NOT the buzzer
#define SUPPORTS_BME280           1
#define SUPPORTS_RELAY_OUTPUT     1    // deprecated shim (→ control-engine Actions)
#define SUPPORTS_ELM327           1
#define SUPPORTS_TEST_MODE        1    // CLI test injection (optional trigger pin lives in the overlay)

// ===== SIZING =====
#define MAX_INPUTS              40
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
//   RGB_PIN_R/G/B → SUPPORTS_LED   BUZZER_PIN (buzzer)   external SD_PIN → SUPPORTS_SD_LOGGING
//   MODE_BUTTON_PIN → SUPPORTS_MODE_BUTTON   TEST_MODE_PIN (trigger)
//   HOST_NRF_CTS_PIN + HOST_NRF_RTS_PIN (nRF software flow control)
