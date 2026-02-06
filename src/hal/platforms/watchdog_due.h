/*
 * watchdog_due.h - Arduino Due (SAM3X) watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 */

#ifndef HAL_WATCHDOG_DUE_H
#define HAL_WATCHDOG_DUE_H

#include <Arduino.h>

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
    // SAM3X watchdog (approximate timeout calculation)
    // WDT runs at ~32kHz, timeout = (WDV * 128) / 32768 seconds
    uint32_t wdv = (timeout_ms * 32768UL) / (128UL * 1000UL);
    if (wdv > 0xFFF) wdv = 0xFFF;

    WDT->WDT_MR = WDT_MR_WDV(wdv) |
                  WDT_MR_WDRSTEN |     // Enable reset
                  WDT_MR_WDD(wdv);     // Delta value
}

inline void watchdogReset() {
    WDT->WDT_CR = WDT_CR_KEY_PASSWD | WDT_CR_WDRSTT;
}

inline void watchdogDisable() {
    WDT->WDT_MR = WDT_MR_WDDIS;
}

} // namespace hal

#endif // HAL_WATCHDOG_DUE_H
