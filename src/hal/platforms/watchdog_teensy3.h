/*
 * watchdog_teensy3.h - Teensy 3.x (Kinetis K20/K66) watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 */

#ifndef HAL_WATCHDOG_TEENSY3_H
#define HAL_WATCHDOG_TEENSY3_H

#include <Arduino.h>

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
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

inline void watchdogReset() {
    // Refresh sequence (must be exact order)
    noInterrupts();
    WDOG_REFRESH = 0xA602;
    WDOG_REFRESH = 0xB480;
    interrupts();
}

inline void watchdogDisable() {
    noInterrupts();
    WDOG_UNLOCK = 0xC520;
    WDOG_UNLOCK = 0xD928;
    delayMicroseconds(1);
    WDOG_STCTRLH = 0x0000;
    interrupts();
}

} // namespace hal

#endif // HAL_WATCHDOG_TEENSY3_H
