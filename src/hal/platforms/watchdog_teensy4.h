/*
 * watchdog_teensy4.h - Teensy 4.x (IMXRT1062) watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 * Uses WDT_T4 library for IMXRT watchdog
 */

#ifndef HAL_WATCHDOG_TEENSY4_H
#define HAL_WATCHDOG_TEENSY4_H

#include <Watchdog_t4.h>

namespace hal {

namespace detail {
    // Static instance in anonymous namespace to avoid ODR issues
    static WDT_T4<WDT1> wdt;  // Using WDOG1
}

inline void watchdogEnable(uint16_t timeout_ms) {
    // Convert ms to seconds (WDT_T4 uses seconds)
    uint16_t timeout_s = (timeout_ms + 999) / 1000;  // Round up

    WDT_timings_t config;
    config.trigger = timeout_s;      // Interrupt at timeout
    config.timeout = timeout_s;      // Reset at timeout
    config.callback = nullptr;       // No callback needed
    detail::wdt.begin(config);
}

inline void watchdogReset() {
    detail::wdt.feed();
}

inline void watchdogDisable() {
    // WDT_T4 doesn't provide disable - use feed() continuously
    // For production automotive use, disabling is not recommended
}

} // namespace hal

#endif // HAL_WATCHDOG_TEENSY4_H
