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
#if INCLUDE_SD_LOGGING && !SUPPORTS_SD
#error "INCLUDE_SD_LOGGING requires SUPPORTS_SD=1 in the board profile"
#endif

#if SUPPORTS_SD

#include "sd_manager.h"
#include "system_config.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../core/watchdog.h"
#include "../core/system_mode.h"
#include <SPI.h>
#include "sd_fs.h"   // SD library for this platform (STM32: SDIO)

#ifndef FLASHMEM
#define FLASHMEM
#endif

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

// Latched once the card is confirmed gone; see sdMediaPresent().
static bool sdMediaGone = false;

// Capacity snapshot taken by initSD(); see sdCapacityAtMount().
static bool     sdCapacityKnown = false;
static uint64_t sdFreeAtMount   = 0;
static uint64_t sdTotalAtMount  = 0;

#if defined(ARDUINO_ARCH_STM32)
// ---------------------------------------------------------------------------
// Bounded card handshake before SD.begin() (STM32 SDIO)
//
// ST's HAL power-up (SD_PowerON) polls ACMD41 "are you ready?" up to 65535
// times with no time limit: a card that keeps answering busy holds SD.begin()
// for tens of seconds, past the watchdog, and the board resets into the same
// wait on every boot. The bench hit it after every pass through the ROM DFU
// bootloader (which probes SPI3/USART3 on PC10-PC12, the SDIO D2/D3/CLK
// lines); a failing card in the car would hit it on every power-up.
//
// So the same handshake runs here first, bounded by the SD spec's 1 s (plus
// margin): reset (CMD0), interface check (CMD8), ACMD41 until ready. A card
// that comes ready is left ready, and SD.begin()'s own handshake then passes
// at once. One that doesn't is skipped for this session.
static constexpr uint32_t SD_READY_BUDGET_MS = 1500;

// Milliseconds the card took to report ready, or -1 if it stayed busy past
// the budget, or -2 if it didn't answer at all (no card, no response).
static int32_t sdioCardReadyMs() {
    pinmap_pinout(PinMap_SD_DATA0[0].pin, PinMap_SD_DATA0);
    pinmap_pinout(PinMap_SD_DATA1[0].pin, PinMap_SD_DATA1);
    pinmap_pinout(PinMap_SD_DATA2[0].pin, PinMap_SD_DATA2);
    pinmap_pinout(PinMap_SD_DATA3[0].pin, PinMap_SD_DATA3);
    pinmap_pinout(PinMap_SD_CMD[0].pin, PinMap_SD_CMD);
    pinmap_pinout(PinMap_SD_CK[0].pin, PinMap_SD_CK);
    __HAL_RCC_SDIO_CLK_ENABLE();

    SDIO_InitTypeDef init = {};
    init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    init.BusWide = SDIO_BUS_WIDE_1B;
    init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    init.ClockDiv = SDIO_INIT_CLK_DIV;   // 400 kHz identification clock
    SDIO_Init(SDIO, init);
    __SDIO_DISABLE(SDIO);
    SDIO_PowerState_ON(SDIO);
    delay(2);
    __SDIO_ENABLE(SDIO);
    delay(2);   // >= 74 clocks before the first command

    if (SDMMC_CmdGoIdleState(SDIO) != SDMMC_ERROR_NONE) return -2;
    // CMD8 answers only on SD 2.0+ cards, which may then claim high capacity.
    const bool v2 = SDMMC_CmdOperCond(SDIO) == SDMMC_ERROR_NONE;
    const uint32_t arg = SDMMC_VOLTAGE_WINDOW_SD | (v2 ? SDMMC_HIGH_CAPACITY : 0U);

    const uint32_t t0 = millis();
    do {
        if (SDMMC_CmdAppCommand(SDIO, 0) != SDMMC_ERROR_NONE) return -2;
        if (SDMMC_CmdAppOperCommand(SDIO, arg) != SDMMC_ERROR_NONE) return -2;
        if (SDIO_GetResponse(SDIO, SDIO_RESP1) >> 31) return (int32_t)(millis() - t0);
    } while (millis() - t0 < SD_READY_BUDGET_MS);
    return -1;
}
#endif

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
    WatchdogScope window(SD_WDT_WINDOW_MS, SD_WDT_TAG);
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

bool sdMediaKnownGone() {
    return sdMediaGone;
}

/**
 * Initialize SD card
 * Called once during setup(), used by both SD logging and JSON config
 */
FLASHMEM void initSD() {
    msg.debug.info(TAG_SD, "Initializing SD card...");

    bool initSuccess = false;
    {
        // SD.begin() can take 2-3 s per attempt and we make several, and the
        // capacity scan below reads the whole FAT: one scope across both.
        WatchdogScope wd(SD_WDT_WINDOW_MS, SD_WDT_TAG);

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
#if SD_PIN == 254 && defined(ARDUINO_ARCH_STM32)
            // SDIO slot. No card-detect pin: the Feather F405 Rev B routes
            // it wrong (Adafruit pinout page), so mount and see -- but only
            // after a bounded handshake (sdioCardReadyMs above).
            {
                const int32_t readyMs = sdioCardReadyMs();
                if (readyMs < 0) {
                    // Per attempt, so debug: a card fresh from the ROM
                    // bootloader fails the first one and passes the next.
                    // Every attempt failing is reported below.
                    msg.debug.debug(TAG_SD, readyMs == -1 ? "SD handshake: card busy past %lu ms"
                                                           : "SD handshake: no answer",
                                    (unsigned long)SD_READY_BUDGET_MS);
                    continue;
                }
                msg.debug.info(TAG_SD, "SD card ready after %ld ms", (long)readyMs);
            }
            initSuccess = SD.begin(SD_DETECT_NONE);
#elif SD_PIN == 254
            initSuccess = SD.begin(BUILTIN_SDCARD);
#else
            initSuccess = SD.begin(SD_PIN);
#endif
            watchdogReset();
        }

        // Capacity, while the window is still wide: the free count is a scan of
        // the FAT (FAT32) or allocation bitmap (exFAT), so it is read here once and
        // never from a status poll (#536).
#if defined(CORE_TEENSY) || defined(ESP32) || defined(ARDUINO_ARCH_STM32)
        // (The stock Arduino SD library has no capacity call; AVR also keeps
        // msg.debug format strings in RAM, so the timing line stays out of it.)
        if (initSuccess) {
            uint32_t scanStart = millis();
#if defined(CORE_TEENSY)
            sdTotalAtMount = SD.totalSize();
            sdFreeAtMount  = sdTotalAtMount - SD.usedSize();
#elif defined(ARDUINO_ARCH_STM32)
            // STM32SD has no capacity call; FatFs counts free clusters.
            DWORD freeClusters = 0;
            FATFS* fs = nullptr;
            if (f_getfree(SD.fatFs()->getRoot(), &freeClusters, &fs) == FR_OK && fs) {
                const uint64_t clusterBytes = (uint64_t)fs->csize * 512;   // SD sectors are 512 B
                sdTotalAtMount = (uint64_t)(fs->n_fatent - 2) * clusterBytes;
                sdFreeAtMount  = (uint64_t)freeClusters * clusterBytes;
            }
#else
            sdTotalAtMount = SD.totalBytes();
            sdFreeAtMount  = sdTotalAtMount - SD.usedBytes();
#endif
            sdCapacityKnown = sdTotalAtMount > 0;
            msg.debug.info(TAG_SD, "SD free-space scan: %lu ms",
                           (unsigned long)(millis() - scanStart));
            watchdogReset();
        }
#endif

    }   // end of the SD watchdog scope

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

FLASHMEM bool sdCapacityAtMount(uint64_t& freeBytes, uint64_t& totalBytes) {
    if (!sdInitialized || !sdCapacityKnown) return false;
    freeBytes  = sdFreeAtMount;
    totalBytes = sdTotalAtMount;
    return true;
}

#endif // SUPPORTS_SD
