/*
 * boards/esp32s3/board.h — Board definition for ESP32-S3 (BLE only)
 *
 * Hardware description + feature selection. Discrete WIRING lives in the
 * gitignored boards/esp32s3/wiring.ini overlay. `-include`d before
 * boards/resolve.h. See boards/README.md. ~320 KB usable RAM after IDF overhead.
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAPABILITY (board facts) =====
#define PROFILE_HAS_NATIVE_CAN 1     // TWAI (needs external transceiver)
#define EEPROM_TOTAL_BYTES     4096  // flash-emulated EEPROM (E2END undefined on ESP32)
// No HAS_BUILTIN_SD: SD is an external module — wire it and set SD_PIN in the
// overlay to enable logging (resolve.h derives SUPPORTS_SD / SUPPORTS_SD_LOGGING).

// ===== PINS (what the core can't know; pinctrl/board_pins.h, #570) =====
// Which pins exist and which pin each A-name is come from the Arduino core.
// The core counts 20 analog inputs; A10..A19 are ADC2, which can't be read
// while the radio runs, and this board runs BLE.
#define BOARD_ANALOG_USABLE    10
// GPIOs wired to the ESP32-S3-DevKitC-1-N8R8 module itself: SPI flash 26..32
// and octal PSRAM 33..37. Claimed in the pin registry at boot, so SET, BUS and
// JSON IMPORT refuse them like any other taken pin. USB D-/D+ (GPIO 19/20)
// are left free: they are ordinary GPIOs when the USB port isn't in use.
#define BOARD_RESERVED_PINS    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37
#define BOARD_RESERVED_NAME    "Module flash/PSRAM"

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
//   BUZZER_PIN (buzzer)   external SD_PIN → SUPPORTS_SD_LOGGING   TEST_MODE_PIN (trigger)
