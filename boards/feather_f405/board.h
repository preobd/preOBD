/*
 * boards/feather_f405/board.h — Board definition for the Adafruit Feather
 * STM32F405 Express (STM32duino core, variant FEATHER_F405)
 *
 * Hardware description + feature selection. Discrete WIRING lives in the
 * gitignored boards/feather_f405/wiring.ini overlay. `-include`d before
 * boards/resolve.h. See boards/README.md.
 *
 * STM32F405RG: 168 MHz Cortex-M4F, 1 MB flash, 128 KB SRAM + 64 KB CCM (CCM
 * is not reachable by DMA). The v1 PCB candidate MCU (#100): this target also
 * measures the RAM gate in preOBD-hardware docs/v1-requirements.md.
 * Single bxCAN bus on D9/D10 with an external transceiver (SN65HVD230).
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAPABILITY (board facts) =====
#define PROFILE_HAS_NATIVE_CAN 1     // bxCAN1 (needs external transceiver)
#define EEPROM_TOTAL_BYTES     4096  // flash-emulated EEPROM (storage/eeprom_store.h)
#define HAS_BUILTIN_SD         1     // microSD slot on SDIO (STM32SD; resolves to SD_PIN=254)

// ===== PINS (what the core can't know; pinctrl/board_pins.h, #570) =====
// Feather pins wired to on-board parts, claimed at boot so every command
// refuses them: SPI flash D2-D4 + CS D7, SDIO D26-D31 + card detect D32,
// 32 kHz crystal D33/D34, USB D35/D36, SWD D37/D38. D8 (NeoPixel) is the
// status LED's (below); D13 (red LED) stays free for you to assign.
#define BOARD_RESERVED_PINS    2, 3, 4, 7, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38
#define BOARD_RESERVED_NAME    "Feather on-board"

// On-board NeoPixel (D8 = PC0, not on the header): the RGB status LED.
// resolve.h turns this into INCLUDE_LED; don't wire RGB_PIN_R/G/B as well.
#define RGB_NEOPIXEL_PIN       8

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
// teensy41 parity, so the RAM gate measures the full feature set.
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
//   (status LED: on-board NeoPixel above)   BUZZER_PIN (buzzer)   MODE_BUTTON_PIN → SUPPORTS_MODE_BUTTON
//   TEST_MODE_PIN (trigger)   HOST_NRF_CTS_PIN + HOST_NRF_RTS_PIN (nRF software flow control)

// ===== CONFIG STORE (storage/eeprom_store.h) =====
// The emulated EEPROM lives in the last 128 KB flash sector; board.ini caps
// the image at 896 KB so the linker never places code there.
#define EEPROM_FLASH_SECTOR    11            // FLASH_SECTOR_11
#define EEPROM_FLASH_ADDR      0x080E0000UL
