/*
 * boards/mega2560/board.h — Board definition for Arduino Mega 2560
 *
 * Hardware description + feature selection. Discrete WIRING lives in the
 * gitignored boards/mega2560/wiring.ini overlay. `-include`d before
 * boards/resolve.h. See boards/README.md.
 *
 * RAM budget: 8 KB HARD limit, and static RAM % is NOT the budget: the stack
 * peaks at ~630 B (SAVE), so keep static ≲ 89% (#523; at 97.4% it crash-looped
 * at boot). PROGMEM must also end below 64 KB (post-link guard). Every CEILING below is a real
 * RAM/flash limit, not a preference — see docs/development/FEATURE_COSTS.md for measured
 * costs. SERIAL_*_BUFFER_SIZE live as -D flags in board.ini (core TUs aren't
 * reached by -include).
 */
#pragma once
#define PREOBD_BOARD_DEFINED 1

// ===== CAN CONTROLLER (none native on AVR) =====
// An MCP2515 shield or module is WIRING, not a fact of this board: its
// chip-select (CAN_CS_0, plus CAN_INT_0) goes in wiring.ini with the SPI bus it
// sits on (SPI_WIRED_BUS=0). No CAN_CS_0, no CAN in the image (#658).
#define PROFILE_HAS_NATIVE_CAN 0

// ===== CEILINGS (board genuinely can't — RAM/flash; see docs/development/FEATURE_COSTS.md) =====
#define PLATFORM_NO_SD_LOGGING 1     // SD lib + buffers blow the 8 KB budget (and SD.h isn't in lib_deps)
// The rest are vetoed by simple omission — a capability this board lacks has no
// line at all, so there is no negative to declare. RAM each WOULD cost if added:
//   SUPPORTS_ELM327 (+301 B), SUPPORTS_FRAMED_TRANSPORT (+1689 B),
//   SUPPORTS_TEST_MODE (+238 B) — each exceeds the 8 KB headroom.
//   SUPPORTS_REALDASH (+36 B) would fit; it is omitted by preference, not ceiling.

// ===== FEATURE CAPABILITY (what this board CAN do) =====
// Declared by presence: a board that can't simply omits the line. Never 0.
// boards/resolve.h turns each of these into the INCLUDE_* flag src/ tests.
// To reclaim a feature's flash/RAM on this board, comment its line out here —
// measured per-feature costs are in docs/development/FEATURE_COSTS.md. (For a one-off local
// build, put `-D INCLUDE_<X>=0` in your gitignored wiring.ini instead.)
// Savings shown are what commenting the line out reclaims on THIS board
// (flash / static RAM, measured — docs/development/FEATURE_COSTS.md).
#define SUPPORTS_CAN             1     // −11,568 B / −691 B — largest RAM consumer (needs CAN_CS_0 wired)
#define SUPPORTS_SERIAL_OUTPUT   1     //  −3,170 B /  −28 B
#define SUPPORTS_LCD             1     //  −4,746 B / −236 B
#define SUPPORTS_ALARMS          1     // −16,426 B / −455 B — gates the whole control engine on AVR
#define SUPPORTS_BME280          1     //  −7,766 B / −174 B

// ===== SIZING (tuned to the 8 KB budget) =====
#define RULES_RAM_RULES_MAX   2    // control caps are a REAL SRAM limit on AVR (ARCHITECTURE §11.9)
#define RULES_RAM_ACTIONS_MAX 2
#define RULES_RAM_LINKS_MAX   2    // 2 B/row; enough for one 2-rule action at cap 2 (#343)
#define MAX_INPUTS              8
#define MAX_OUTPUTS             2     // OBD and CSV are the only formats built here (#612)
#define MAX_PID_ENTRIES         8
#define MAX_PIN_REGISTRY        24
#define CAN_CACHE_SIZE          8
#define MAX_SCAN_RESULTS        4

// ===== CLI SIZES =====
// One line per console source (#624: two here, primary and secondary). The
// stack peaks at a measured 633 B (SAVE), so keep static RAM ≲ 89% (#523).
#define CLI_CMD_BUFFER_SIZE     128   // one line: 127 chars + NUL
#define CLI_MAX_ARGS            9     // longest cmd: set <name> rpm 4 <4 pairs> <max>

// ===== JSON CAPABILITY (CEILING — AVR RAM can't host it) =====
#define SUPPORTS_JSON_CONFIG        0
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG

// WIRED (set in wiring.ini, derived by boards/resolve.h):
//   RGB_PIN_R/G/B → INCLUDE_LED   USER_BUTTON_PIN → INCLUDE_USER_BUTTON   BUZZER_PIN
