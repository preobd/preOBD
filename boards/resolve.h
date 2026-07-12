/*
 * boards/resolve.h — feature resolver (Kconfig-in-a-header).
 *
 * `-include`d AFTER boards/<board>/board.h. Derives the WIRED ENABLE_* flags the
 * code reads from board capability (board.h) + pin presence (your local overlay),
 * and fails the build loudly on impossible combinations. The single source of
 * dependency truth — see boards/README.md.
 *
 * Inputs it reads: HAS_BUILTIN_SD, PLATFORM_NO_*, and the overlay pins
 * (RGB_PIN_*, MODE_BUTTON_PIN, SD_PIN, BUZZER_PIN, TEST_MODE_PIN).
 * Outputs it defines: ENABLE_LED, ENABLE_MODE_BUTTON, ENABLE_SD_LOGGING,
 * SUPPORTS_SD, SD_PIN (built-in). All #ifndef-guarded so an explicit override
 * (e.g. -D ENABLE_LED=0) still wins.
 */
#pragma once

#ifndef PREOBD_BOARD_DEFINED
#  error "boards/resolve.h included without a board definition. Check the env's build_flags: -include boards/<board>/board.h must come BEFORE -include boards/resolve.h."
#endif

// ---- WIRED peripherals: on iff their pin(s) present and the board doesn't veto ----

// RGB status LED — active when all three channels are wired.
#if defined(RGB_PIN_R) && defined(RGB_PIN_G) && defined(RGB_PIN_B)
#  ifndef ENABLE_LED
#    define ENABLE_LED 1
#  endif
#endif

// Mode / silence button.
#if defined(MODE_BUTTON_PIN)
#  ifndef ENABLE_MODE_BUTTON
#    define ENABLE_MODE_BUTTON 1
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
#  ifndef ENABLE_SD_LOGGING
#    define ENABLE_SD_LOGGING 1
#  endif
#endif

// Buzzer has no ENABLE flag — code keys on defined(BUZZER_PIN) directly.

// ---- Sanity: fail loud on impossible combinations ----
// Test the flag VALUE, not just its presence, so an explicit off-override
// (e.g. -D ENABLE_LED=0) is honored rather than tripping these errors.
#if ENABLE_SD_LOGGING && defined(PLATFORM_NO_SD_LOGGING)
#  error "ENABLE_SD_LOGGING on a board that vetoes SD (PLATFORM_NO_SD_LOGGING)."
#endif
#if ENABLE_LED && !(defined(RGB_PIN_R) && defined(RGB_PIN_G) && defined(RGB_PIN_B))
#  error "ENABLE_LED set without all of RGB_PIN_R/G/B — define them in boards/<board>/wiring.ini."
#endif
#if SUPPORTS_SD && !defined(SD_PIN)
#  error "SUPPORTS_SD without SD_PIN."
#endif

#define PREOBD_RESOLVED 1
