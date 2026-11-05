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
    static bool wdtStarted = false;

    // WDOG1_WCR.WT encodes the timeout as (WT + 1) * 0.5 s, so 0.5..128 s.
    inline uint8_t wdogTimeoutField(uint16_t timeout_s) {
        if (timeout_s < 1)   timeout_s = 1;
        if (timeout_s > 128) timeout_s = 128;
        return (uint8_t)(timeout_s * 2 - 1);
    }
}

/*
 * Arm the watchdog, or change its timeout. Either way the new timeout counts
 * from now.
 *
 * WDT_T4::begin() runs only once. It writes WDOG1_WCR in two steps,
 * `WCR = SRS | WT` and then `WCR |= WDE | WDA | ...`, with interrupts on, and
 * the first write clears WDA, which asserts WDOG_B. An interrupt between the
 * two holds WDOG_B long enough to reset the board, recorded as
 * SRSR 0x10 / WRSR 0x1 (SFTW), not a timeout (#537). That was harmless as a
 * one-off at boot, but the timeout is changed at runtime: on every RUN entry,
 * and twice per logged SD row (SdWatchdogWindow, #331). Bench: 4 resets in
 * 10 min at 1 s rows with a USB poll running, and one within ~2 s at 1 ms rows.
 *
 * So after the first call only the WT field changes, in one 16-bit write with
 * interrupts off. Every other WCR bit is written back as read: SRS and WDA
 * stay 1, and WDE/WDT/SRE are write-once and already set. The feed afterwards
 * matters too: the IMXRT loads a new WT only when it is next serviced.
 */
inline void watchdogEnable(uint16_t timeout_ms) {
    const uint16_t timeout_s = (timeout_ms + 999) / 1000;  // whole seconds, rounded up

    if (!detail::wdtStarted) {
        WDT_timings_t config;
        config.trigger = timeout_s;      // pre-timeout interrupt: unused (no callback)
        config.timeout = timeout_s;      // reset at timeout
        config.callback = nullptr;
        // IRQs off for the same two-step write: the first call is usually
        // initSD()'s window at boot, with USB and systick already running.
        // begin() doesn't block, and NVIC_ENABLE_IRQ is fine with PRIMASK set.
        __disable_irq();
        detail::wdt.begin(config);
        __enable_irq();
        detail::wdtStarted = true;
    } else {
        const uint16_t wt = WDOG_WCR_WT(detail::wdogTimeoutField(timeout_s));
        __disable_irq();
        WDOG1_WCR = (uint16_t)((WDOG1_WCR & ~WDOG_WCR_WT(0xFF)) | wt);
        __enable_irq();
    }
    detail::wdt.feed();
}

inline void watchdogReset() {
    detail::wdt.feed();
}

inline void watchdogDisable() {
    // WDOG1 cannot be disabled once enabled (WDE is write-once, #243).
}

} // namespace hal

#endif // HAL_WATCHDOG_TEENSY4_H
