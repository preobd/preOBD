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

    // Clocked from the 1 kHz LPO (CLKSRC clear) with no prescaling, so TOVAL
    // is in milliseconds. This used to select the bus clock (CLKSRC set) with
    // PRESC 0x400, i.e. divide by 5: ~12 MHz, so "2000 ms" was ~0.17 ms. It
    // went unnoticed because nothing ran a Teensy 3.6 in RUN; since #541 the
    // watchdog starts at boot on every board and would have reset every boot.
    // Not yet verified on Teensy 3.6 hardware.
    const uint32_t timeout = timeout_ms;
    WDOG_TOVALH = (timeout >> 16) & 0xFFFF;
    WDOG_TOVALL = timeout & 0xFFFF;
    WDOG_PRESC = 0;

    // Enable watchdog with update allowed (so it can be retuned later)
    WDOG_STCTRLH = WDOG_STCTRLH_ALLOWUPDATE |
                   WDOG_STCTRLH_WDOGEN |
                   WDOG_STCTRLH_WAITEN |
                   WDOG_STCTRLH_STOPEN;
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
