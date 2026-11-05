/*
 * boards/resolve.h — feature resolver (Kconfig-in-a-header).
 *
 * `-include`d AFTER boards/<board>/board.h. The single source of dependency
 * truth — see boards/README.md.
 *
 * THREE LAYERS, three vocabularies. Keeping them distinct is the whole point:
 *
 *   SUPPORTS_X   board.h    "this board CAN do X." Declared by presence — a
 *                           board that can't simply omits the line. Never 0.
 *   INCLUDE_X    here       "X's code is IN THE IMAGE." Capability AND wiring
 *                           AND no veto. What src/ tests for feature code.
 *                           (INCLUDE_SELFTEST and INCLUDE_RULES_ENGINE are
 *                           derived in src/ instead — the native test build
 *                           has no board.h, so they can't live here.)
 *   (runtime)    EEPROM     "X is actually ON." outputEnabled[], displayType,
 *                           framed_enabled_mask, an I2C probe. NEVER a macro —
 *                           the compiler cannot know this, and pretending it
 *                           could is what caused #351.
 *
 * So `#if INCLUDE_LCD` means the driver is linked in, NOT that a screen is
 * fitted or lit — that is systemConfig.displayType. Six of these features have
 * a runtime gate as well, and code that means "is it on?" must test that, not
 * the macro.
 *
 * Inputs it reads: SUPPORTS_*, HAS_BUILTIN_SD, PLATFORM_NO_*, and the overlay
 * pins (RGB_PIN_*, MODE_BUTTON_PIN, SD_PIN, BUZZER_PIN, TEST_MODE_PIN).
 * KNOWN EXCEPTION: SD and JSON predate this split and are still gated in src/
 * on SUPPORTS_SD / SUPPORTS_JSON_CONFIG / _EXPORT / _IMPORT_STREAM directly
 * (~55 sites, e.g. sd_manager.h:13, json_config.cpp:12) — those SUPPORTS_ names
 * are doing INCLUDE_-layer work. Not renamed here because they are also real
 * ceilings a board sets to 0 (mega2560), so the two roles have to be separated
 * before the names can be. Don't grep only for INCLUDE_ and conclude SD or JSON
 * is ungated.
 *
 * Outputs it defines: INCLUDE_* for every feature, SUPPORTS_SD, SD_PIN
 * (built-in). All #ifndef-guarded, so an explicit override in your gitignored
 * wiring.ini (e.g. -D INCLUDE_REALDASH=0) still wins — that is the local
 * escape hatch, as against commenting the SUPPORTS_* line out of board.h,
 * which is a tracked decision for everyone building that board.
 */
#pragma once

#ifndef PREOBD_BOARD_DEFINED
#  error "boards/resolve.h included without a board definition. Check the env's build_flags: -include boards/<board>/board.h must come BEFORE -include boards/resolve.h."
#endif

// ---- WIRED peripherals: in the image iff their pin(s)/bus are declared ----

// RGB status LED — active when all three channels are wired, or when the
// board has a single addressable pixel (RGB_NEOPIXEL_PIN, board.h).
#if (defined(RGB_PIN_R) && defined(RGB_PIN_G) && defined(RGB_PIN_B)) || defined(RGB_NEOPIXEL_PIN)
#  ifndef INCLUDE_LED
#    define INCLUDE_LED 1
#  endif
#endif

// Mode / silence button.
#if defined(MODE_BUTTON_PIN)
#  ifndef INCLUDE_MODE_BUTTON
#    define INCLUDE_MODE_BUTTON 1
#  endif
#endif

// SD logging — capable via a built-in slot OR a wired CS pin, unless RAM-vetoed.
#if !defined(PLATFORM_NO_SD_LOGGING) && (defined(HAS_BUILTIN_SD) || defined(SD_PIN))
#  if defined(HAS_BUILTIN_SD) && !defined(SD_PIN)
#    define SD_PIN 254               // BUILTIN_SDCARD
#  endif
#  ifndef SUPPORTS_SD
#    define SUPPORTS_SD 1
#  endif
#  ifndef INCLUDE_SD_LOGGING
#    define INCLUDE_SD_LOGGING 1
#  endif
#endif

// Buzzer has no INCLUDE flag — code keys on defined(BUZZER_PIN) directly.

// ---- Feature code: in the image iff the board declares the capability ----
// These need no wiring beyond what the board already has, so capability is the
// only gate. Comment a SUPPORTS_* line out of board.h to reclaim its flash;
// see docs/development/FEATURE_COSTS.md for the measured cost of each.
//
// Tested by VALUE, not defined(). The convention is "declare by presence, never
// write 0" — but if someone writes `#define SUPPORTS_TEST_MODE 0` anyway (an
// easy mistake, since SUPPORTS_JSON_CONFIG=0 in the same file legitimately does
// mean off), defined() would silently read that as ON and pull the feature in.
// Undefined still evaluates to 0 here, so presence-declaration is unaffected.

#if SUPPORTS_CAN
#  ifndef INCLUDE_CAN
#    define INCLUDE_CAN 1
#  endif
#endif

#if SUPPORTS_REALDASH
#  ifndef INCLUDE_REALDASH
#    define INCLUDE_REALDASH 1
#  endif
#endif

#if SUPPORTS_SERIAL_OUTPUT
#  ifndef INCLUDE_SERIAL_OUTPUT
#    define INCLUDE_SERIAL_OUTPUT 1
#  endif
#endif

// Without it the CLI is a plain line reader (src/cli/line_reader.h): same
// commands, prompt and quoting, no history or cursor editing (#147).
#if SUPPORTS_CLI_EDITING
#  ifndef INCLUDE_CLI_EDITING
#    define INCLUDE_CLI_EDITING 1
#  endif
#endif

#if SUPPORTS_FRAMED_TRANSPORT
#  ifndef INCLUDE_FRAMED_TRANSPORT
#    define INCLUDE_FRAMED_TRANSPORT 1
#  endif
#endif

#if SUPPORTS_ALARMS
#  ifndef INCLUDE_ALARMS
#    define INCLUDE_ALARMS 1
#  endif
#endif

// The LCD is deliberately NOT wiring-gated. Whether a screen is FITTED is
// systemConfig.displayType, a runtime fact no compile-time flag can know, and
// that is what gates bring-up (#351/#355) — a compile-time gate would only
// mean you had to reflash to attach a display. See #356 for why that issue
// was closed. (This is a claim about the DISPLAY's presence, not about which
// I2C bus INSTANCE it runs on: that half moved to a build-time fact in #377 —
// I2C_WIRED_BUS in wiring.ini/board.h — the same WIRED pattern this file uses
// for LED/mode-button/SD. A declaration only grants permission, though: `BUS
// I2C <n>` still decides, and remains the one place a display's bus is
// actually selected.)
#if SUPPORTS_LCD
#  ifndef INCLUDE_LCD
#    define INCLUDE_LCD 1
#  endif
#endif

// The OLED follows the LCD's rule above: in the image when the board can drive
// it, fitted or not (systemConfig.displayType = DISPLAY_OLED brings it up).
#if SUPPORTS_OLED
#  ifndef INCLUDE_OLED
#    define INCLUDE_OLED 1
#  endif
#endif

// Either character display: the shared cell renderer, DISPLAY STATUS paging
// and the loop's redraw are built in when at least one driver is.
#if INCLUDE_LCD || INCLUDE_OLED
#  ifndef INCLUDE_DISPLAY
#    define INCLUDE_DISPLAY 1
#  endif
#endif

#if SUPPORTS_BME280
#  ifndef INCLUDE_BME280
#    define INCLUDE_BME280 1
#  endif
#endif

#if SUPPORTS_RELAY_OUTPUT
#  ifndef INCLUDE_RELAY_OUTPUT
#    define INCLUDE_RELAY_OUTPUT 1
#  endif
#endif

#if SUPPORTS_ELM327
#  ifndef INCLUDE_ELM327
#    define INCLUDE_ELM327 1
#  endif
#endif

#if SUPPORTS_TEST_MODE
#  ifndef INCLUDE_TEST_MODE
#    define INCLUDE_TEST_MODE 1
#  endif
#endif

// ---- Sanity: fail loud on impossible combinations ----
// Test the flag VALUE, not just its presence, so an explicit off-override
// (e.g. -D INCLUDE_LED=0) is honored rather than tripping these errors.
#if INCLUDE_SD_LOGGING && defined(PLATFORM_NO_SD_LOGGING)
#  error "INCLUDE_SD_LOGGING on a board that vetoes SD (PLATFORM_NO_SD_LOGGING)."
#endif
#if INCLUDE_LED && !(defined(RGB_PIN_R) && defined(RGB_PIN_G) && defined(RGB_PIN_B)) && !defined(RGB_NEOPIXEL_PIN)
#  error "INCLUDE_LED set without all of RGB_PIN_R/G/B — define them in boards/<board>/wiring.ini."
#endif
#if defined(RGB_NEOPIXEL_PIN) && (defined(RGB_PIN_R) || defined(RGB_PIN_G) || defined(RGB_PIN_B))
#  error "RGB_PIN_R/G/B wired on a board with an on-board NeoPixel (RGB_NEOPIXEL_PIN): the status LED drives one or the other. Drop the RGB_PIN_* lines from wiring.ini."
#endif
#if SUPPORTS_SD && !defined(SD_PIN)
#  error "SUPPORTS_SD without SD_PIN."
#endif
#define PREOBD_RESOLVED 1
