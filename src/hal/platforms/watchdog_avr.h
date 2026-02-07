/*
 * watchdog_avr.h - AVR watchdog implementation (Arduino Uno/Mega)
 * Part of the preOBD Hardware Abstraction Layer
 */

#ifndef HAL_WATCHDOG_AVR_H
#define HAL_WATCHDOG_AVR_H

#include <avr/wdt.h>

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
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

inline void watchdogReset() {
    wdt_reset();
}

inline void watchdogDisable() {
    wdt_disable();
}

} // namespace hal

#endif // HAL_WATCHDOG_AVR_H
