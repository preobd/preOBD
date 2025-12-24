/*
 * watchdog.cpp - Platform-specific watchdog implementations
 */

#include "watchdog.h"

// ===== AVR (Arduino Uno/Mega) =====
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)

    #include <avr/wdt.h>

    void watchdogEnable(uint16_t timeout_ms) {
        // Map timeout to AVR prescaler values
        if (timeout_ms <= 15) wdt_enable(WDTO_15MS);
        else if (timeout_ms <= 30) wdt_enable(WDTO_30MS);
        else if (timeout_ms <= 60) wdt_enable(WDTO_60MS);
        else if (timeout_ms <= 120) wdt_enable(WDTO_120MS);
        else if (timeout_ms <= 250) wdt_enable(WDTO_250MS);
        else if (timeout_ms <= 500) wdt_enable(WDTO_500MS);
        else if (timeout_ms <= 1000) wdt_enable(WDTO_1S);
        else if (timeout_ms <= 2000) wdt_enable(WDTO_2S);
        else if (timeout_ms <= 4000) wdt_enable(WDTO_4S);
        else wdt_enable(WDTO_8S);
    }

    void watchdogReset() {
        wdt_reset();
    }

    void watchdogDisable() {
        wdt_disable();
    }

// ===== Teensy 3.x (Kinetis K20/K66) =====
#elif defined(__MK20DX256__) || defined(__MK20DX128__) || \
      defined(__MK64FX512__) || defined(__MK66FX1M0__)

    void watchdogEnable(uint16_t timeout_ms) {
        // Kinetis watchdog implementation
        // Unlock sequence (critical - must be exact timing)
        noInterrupts();
        WDOG_UNLOCK = 0xC520;
        WDOG_UNLOCK = 0xD928;
        delayMicroseconds(1);  // Required delay

        // Calculate timeout value
        // WDOG runs at ~1kHz after prescaler, timeout = ms value
        uint32_t timeout = timeout_ms;
        WDOG_TOVALH = (timeout >> 16) & 0xFFFF;
        WDOG_TOVALL = timeout & 0xFFFF;

        // Set prescaler (0x400 = divide by 1024, ~7.2MHz -> ~7kHz)
        WDOG_PRESC = 0x400;

        // Enable watchdog with update allowed
        WDOG_STCTRLH = WDOG_STCTRLH_ALLOWUPDATE |
                       WDOG_STCTRLH_WDOGEN |
                       WDOG_STCTRLH_WAITEN |
                       WDOG_STCTRLH_STOPEN |
                       WDOG_STCTRLH_CLKSRC;
        interrupts();
    }

    void watchdogReset() {
        // Refresh sequence (must be exact order)
        noInterrupts();
        WDOG_REFRESH = 0xA602;
        WDOG_REFRESH = 0xB480;
        interrupts();
    }

    void watchdogDisable() {
        noInterrupts();
        WDOG_UNLOCK = 0xC520;
        WDOG_UNLOCK = 0xD928;
        delayMicroseconds(1);
        WDOG_STCTRLH = 0x0000;
        interrupts();
    }

// ===== Teensy 4.x (IMXRT1062 - using WDT_T4 library) =====
#elif defined(__IMXRT1062__)

    #include <Watchdog_t4.h>

    static WDT_T4<WDT1> wdt;  // Using WDOG1

    void watchdogEnable(uint16_t timeout_ms) {
        // Convert ms to seconds (WDT_T4 uses seconds)
        uint16_t timeout_s = (timeout_ms + 999) / 1000;  // Round up

        WDT_timings_t config;
        config.trigger = timeout_s;      // Interrupt at timeout
        config.timeout = timeout_s;      // Reset at timeout
        config.callback = nullptr;       // No callback needed
        wdt.begin(config);
    }

    void watchdogReset() {
        wdt.feed();
    }

    void watchdogDisable() {
        // WDT_T4 doesn't provide disable - use feed() continuously
        // For production automotive use, disabling is not recommended
    }

// ===== Arduino Due (SAM3X) =====
#elif defined(ARDUINO_SAM_DUE)

    void watchdogEnable(uint16_t timeout_ms) {
        // SAM3X watchdog (approximate timeout calculation)
        // WDT runs at ~32kHz, timeout = (WDV * 128) / 32768 seconds
        uint32_t wdv = (timeout_ms * 32768UL) / (128UL * 1000UL);
        if (wdv > 0xFFF) wdv = 0xFFF;

        WDT->WDT_MR = WDT_MR_WDV(wdv) |
                      WDT_MR_WDRSTEN |     // Enable reset
                      WDT_MR_WDD(wdv);     // Delta value
    }

    void watchdogReset() {
        WDT->WDT_CR = WDT_CR_KEY_PASSWD | WDT_CR_WDRSTT;
    }

    void watchdogDisable() {
        WDT->WDT_MR = WDT_MR_WDDIS;
    }

// ===== ESP32 =====
#elif defined(ESP32)

    #include <esp_task_wdt.h>

    void watchdogEnable(uint16_t timeout_ms) {
        uint32_t timeout_s = (timeout_ms + 999) / 1000;  // Round up to seconds
        esp_task_wdt_init(timeout_s, true);  // Panic on timeout
        esp_task_wdt_add(NULL);  // Add current task
    }

    void watchdogReset() {
        esp_task_wdt_reset();
    }

    void watchdogDisable() {
        esp_task_wdt_delete(NULL);
        esp_task_wdt_deinit();
    }

// ===== Fallback for unknown platforms =====
#else

    void watchdogEnable(uint16_t timeout_ms) {
        // No-op for unknown platforms
        (void)timeout_ms;
    }

    void watchdogReset() {
        // No-op
    }

    void watchdogDisable() {
        // No-op
    }

#endif
