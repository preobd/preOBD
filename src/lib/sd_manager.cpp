/*
 * sd_manager.cpp - SD Card Initialization and Management
 *
 * Centralized SD card initialization for use by:
 * - SD logging (output_sdlog.cpp)
 * - JSON config save/load (json_config.cpp)
 */

#include "../config.h"

#if SUPPORTS_SD && !defined(SD_PIN)
#error "SD_PIN must be defined in the board profile when SUPPORTS_SD is set"
#endif
#if ENABLE_SD_LOGGING && !SUPPORTS_SD
#error "ENABLE_SD_LOGGING requires SUPPORTS_SD=1 in the board profile"
#endif

#if SUPPORTS_SD

#include "sd_manager.h"
#include "system_config.h"
#include "message_api.h"
#include "log_tags.h"
#include "watchdog.h"
#include "system_mode.h"
#include <SPI.h>
#include <SD.h>

// Use Arduino SD library across all platforms for consistency
// SD object is provided globally by SD.h

// Define BUILTIN_SDCARD if not already defined (for Teensy 4.x)
#ifndef BUILTIN_SDCARD
    #define BUILTIN_SDCARD 254
#endif

// Mount attempts before giving up. Each can take 2-3 s, so this must stay well
// inside the 20 s watchdog window opened in initSD().
#define SD_INIT_ATTEMPTS 3

// Track if SD has been successfully initialized
static bool sdInitialized = false;

// Nesting depth of the widened watchdog window (see sdWatchdogWindowEnter).
// Loop-context only: every SD call site runs from loop() or setup(), never from
// an ISR, so this needs no synchronisation. A future ISR-side caller would.
static uint8_t sdWindowDepth = 0;

// Latched once the card is confirmed gone; see sdMediaPresent().
static bool sdMediaGone = false;

void sdWatchdogWindowEnter() {
    if (sdWindowDepth++ == 0) {
        watchdogEnable(SD_WDT_WINDOW_MS);
        // The IMXRT watchdog loads a new timeout only when it is next serviced,
        // so widening without a feed leaves the old 2 s countdown running and
        // the window would do nothing. Same reason for the feed on the way out:
        // it starts the narrow window from now, rather than from wherever the
        // just-finished I/O left the counter.
        watchdogReset();
    }
}

void sdWatchdogWindowExit() {
    if (sdWindowDepth > 0 && --sdWindowDepth == 0) {
        // Restore what the current mode asks for, not a fixed 2 s. CONFIG mode
        // deliberately disables the watchdog (system_mode.cpp), and re-arming it
        // here would silently undo that on every platform where disable actually
        // works (AVR, ESP32, Teensy 3.6) — entering CONFIG with logging active is
        // enough to trigger it, and a long CONFIG operation would then reset the
        // board. Teensy 4.x cannot disable (#243), so there the window simply
        // stays wide in CONFIG, which is strictly safer than narrowing it.
        //
        // initSD() runs before setMode(), while currentMode is still its MODE_RUN
        // default, so boot behaviour is unchanged.
        if (isInConfigMode()) {
            watchdogDisable();
        } else {
            watchdogEnable(SD_WDT_NORMAL_MS);
            watchdogReset();
        }
    }
}

bool sdMediaPresent() {
    // A card that never mounted has nothing to probe — and on Teensy the probe
    // would try a full remount every call in that state, which is expensive and
    // is not this function's job (mounting happens once, in initSD).
    if (!sdInitialized) return false;

    // Terminal once it has said no, enforced here rather than trusted to every
    // caller: SYSTEM DIAG HW is RUN-allowed and would otherwise re-probe a slot
    // we know is empty, which on the SPI and Teensy 3.6 paths means a full
    // remount attempt — seconds of blocked loop() — on every invocation.
    if (sdMediaGone) return false;

#if SD_HAS_MEDIA_PROBE
    // Can itself talk to the card, so it belongs inside a widened window.
    SdWatchdogWindow window;
    if (SD.mediaPresent()) return true;

    // Second opinion before condemning the card, because the caller's response
    // is terminal for the session and one negative is not trustworthy: on the
    // builtin slot this resolves to a single CMD13, and any transient failure
    // reads as "removed". The retry costs nothing on a dead card (the first
    // negative made the library pull DAT3 low, so the second call reads it low
    // and returns immediately) and self-heals on a live one (a present card
    // holds DAT3 high, so the library remounts and reports present again).
    if (SD.mediaPresent()) return true;

    sdMediaGone = true;
    return false;
#else
    // No probe on this platform: report present and let the write fail.
    return true;
#endif
}

/**
 * Initialize SD card
 * Called once during setup(), used by both SD logging and JSON config
 */
void initSD() {
    msg.debug.info(TAG_SD, "Initializing SD card...");

    // Extend watchdog timeout - SD.begin() can take 2-3 seconds per attempt and
    // we make several. Note: Teensy 4.x cannot disable watchdog, only extend it.
    sdWatchdogWindowEnter();

    bool initSuccess = false;

#if SD_PIN == 254
        msg.debug.info(TAG_SD, "Using built-in SD (BUILTIN_SDCARD)");
#else
        pinMode(SD_PIN, OUTPUT);
        msg.debug.info(TAG_SD, "Using external SD, CS pin: %d", SD_PIN);
#endif

    // Retry: a card that is still powering up, or a slot with marginal contact,
    // frequently fails the first begin() and succeeds on a later one. The
    // comment here has always claimed retries; the code only ever made one
    // attempt, so a cold card came up unmounted for the whole session.
    for (uint8_t attempt = 1; attempt <= SD_INIT_ATTEMPTS && !initSuccess; attempt++) {
        msg.debug.debug(TAG_SD, "SD.begin() attempt %u/%u", attempt, SD_INIT_ATTEMPTS);
        delay(100);
#if SD_PIN == 254
        initSuccess = SD.begin(BUILTIN_SDCARD);
#else
        initSuccess = SD.begin(SD_PIN);
#endif
        watchdogReset();
    }

    // Restore normal watchdog timeout
    sdWatchdogWindowExit();

    if (initSuccess) {
        msg.debug.info(TAG_SD, "SD card initialized successfully");
        msg.debug.info(TAG_SD, "Card detected and ready");
        sdInitialized = true;
    } else {
        msg.debug.warn(TAG_SD, "SD card initialization failed");
        msg.debug.warn(TAG_SD, "Check: Is SD card inserted? Is it formatted FAT32?");
        sdInitialized = false;
    }
}

/**
 * Check if SD card is initialized and ready
 */
bool isSDInitialized() {
    return sdInitialized;
}

#endif // SUPPORTS_SD
