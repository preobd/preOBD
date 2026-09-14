/*
 * sd_manager.h - SD Card Management
 *
 * Centralized SD card initialization for use by multiple modules
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include "../config.h"

#if SUPPORTS_SD

// Watchdog window for card I/O. A single SD operation on a marginal or absent
// card blocks for seconds inside the library while it retries, and RUN mode
// holds a 2 s watchdog that Teensy 4.x cannot disarm (#243) — so every blocking
// card access has to widen the window around itself or the board resets (#331).
#define SD_WDT_WINDOW_MS 20000
#define SD_WDT_NORMAL_MS 2000

// Does this platform's SD library expose a cheap liveness probe? Teensy's SD
// wrapper does (SDClass::mediaPresent(), an SDIO status read on the builtin
// slot); the stock Arduino and ESP32 libraries do not.
#if defined(CORE_TEENSY)
    #define SD_HAS_MEDIA_PROBE 1
#else
    #define SD_HAS_MEDIA_PROBE 0
#endif

// Initialize SD card (called once during setup)
void initSD();

// Check if SD card is initialized
bool isSDInitialized();

/**
 * Is the card still physically there?
 *
 * Cheap enough to call once per logged row on platforms that support it.
 * Returns true on platforms with no probe, which is a real gap and not just a
 * conservative default: bench-testing a probe-disabled build proved that a
 * pulled card still resets the board there (writes bank up in the block cache
 * reporting success, then one flush blocks past even a 20 s watchdog window).
 * Any new SD-capable platform wants a probe here, not just a wider window.
 *
 * Not idempotent on Teensy: the library latches "card was present" internally,
 * and once this has returned false the next call attempts a full remount. Call
 * it while the answer is still actionable, and stop calling once it says no.
 */
bool sdMediaPresent();

// Widen/restore the watchdog around card I/O. Nestable — only the outermost
// scope restores, so a helper that opens its own window inside a caller's does
// not drop the caller back to 2 s mid-operation.
void sdWatchdogWindowEnter();
void sdWatchdogWindowExit();

// Scoped form of the pair above.
class SdWatchdogWindow {
public:
    SdWatchdogWindow()  { sdWatchdogWindowEnter(); }
    ~SdWatchdogWindow() { sdWatchdogWindowExit(); }
private:
    SdWatchdogWindow(const SdWatchdogWindow&);
    SdWatchdogWindow& operator=(const SdWatchdogWindow&);
};

#else

// Stub functions when SD is not available on this board. sdMediaPresent() is
// false here (there is no slot at all) where a supported board with no probe
// reports true (there is a slot and its state is unknown) — the two mean
// different things, and only the latter is a caller's cue to attempt I/O.
inline void initSD() {}
inline bool isSDInitialized() { return false; }
inline bool sdMediaPresent() { return false; }
inline void sdWatchdogWindowEnter() {}
inline void sdWatchdogWindowExit() {}
class SdWatchdogWindow {
public:
    SdWatchdogWindow() {}
};

#endif

#endif // SD_MANAGER_H
