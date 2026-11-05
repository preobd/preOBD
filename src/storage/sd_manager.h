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

// Watchdog scope for card I/O. A single SD operation on a marginal or absent
// card blocks for seconds inside the library while it retries, and RUN holds a
// 2 s watchdog (#243), so every blocking card access sits inside
//     WatchdogScope wd(SD_WDT_WINDOW_MS, SD_WDT_TAG);
// or the board resets (#331). Scopes nest; see core/watchdog.h (#541).
#define SD_WDT_WINDOW_MS 20000
#define SD_WDT_TAG       "SD"

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
 * Card capacity, read once at mount (#536). False when no card mounted or the
 * platform's SD library has no capacity call (the stock Arduino one).
 *
 * Never re-read: on a FAT32 card the free count is a scan of the whole FAT,
 * and SdFat on Teensy re-scans exFAT on every call, so a client's status poll
 * must not be what triggers it. Free space now = this minus what the logger
 * has written since (sdLogBytesWritten()).
 */
bool sdCapacityAtMount(uint64_t& freeBytes, uint64_t& totalBytes);

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

// True once sdMediaPresent() has found the slot empty. Reads the latch only,
// never the card, so a status poll can call it.
bool sdMediaKnownGone();

#else

// Stub functions when SD is not available on this board. sdMediaPresent() is
// false here (there is no slot at all) where a supported board with no probe
// reports true (there is a slot and its state is unknown) — the two mean
// different things, and only the latter is a caller's cue to attempt I/O.
inline void initSD() {}
inline bool isSDInitialized() { return false; }
inline bool sdCapacityAtMount(uint64_t&, uint64_t&) { return false; }
inline bool sdMediaPresent() { return false; }
inline bool sdMediaKnownGone() { return false; }

#endif

#endif // SD_MANAGER_H
