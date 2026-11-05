/*
 * watchdog_teensy4.h - Teensy 4.x (IMXRT1062) watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 *
 * Drives WDOG1 directly. It used to go through WDT_T4::begin(), which writes
 * WDOG1_WCR in two steps, `WCR = SRS | WT` and then `WCR |= WDE | WDA | ...`.
 * The first write clears WDA, asserting WDOG_B, and the board resets if that
 * gap is long enough to be seen. An interrupt in it always is (#537:
 * SRSR 0x10 / WRSR 0x1, SFTW, not a timeout).
 *
 * #539 called begin() only once (IRQs masked) and changed only WT afterwards.
 * One unexplained SFTW reset still turned up during a boot on the bench, and
 * begin()'s two-step write was the only remaining candidate. So now every
 * write to WCR is a single 16-bit store with WDA held at 1 (#541):
 *   - arming: SRS | WDA | WT | WDE | WDT | SRE, all at once from the reset
 *     value (WDE, WDT and SRE are write-once; this is that one write);
 *   - retuning: read WCR, replace WT, write it back.
 * The IMXRT loads a new WT only when the watchdog is next serviced, so every
 * enable ends with a feed and the new timeout counts from now.
 *
 * WICR is left at its reset value: WIE is write-once, and #542 needs it free
 * to arm a pre-timeout interrupt that records where the firmware hung.
 */

#ifndef HAL_WATCHDOG_TEENSY4_H
#define HAL_WATCHDOG_TEENSY4_H

#include <Arduino.h>

namespace hal {

namespace detail {
    static bool wdtStarted = false;

    // WDOG1_WCR.WT encodes the timeout as (WT + 1) * 0.5 s, so 0.5..128 s.
    inline uint8_t wdogTimeoutField(uint16_t timeout_s) {
        if (timeout_s < 1)   timeout_s = 1;
        if (timeout_s > 128) timeout_s = 128;
        return (uint8_t)(timeout_s * 2 - 1);
    }

    inline void wdogFeed() {
        WDOG1_WSR = 0x5555;
        WDOG1_WSR = 0xAAAA;
    }
}

inline void watchdogEnable(uint16_t timeout_ms) {
    const uint16_t timeout_s = (timeout_ms + 999) / 1000;  // whole seconds, rounded up
    const uint16_t wt = WDOG_WCR_WT(detail::wdogTimeoutField(timeout_s));

    __disable_irq();
    if (!detail::wdtStarted) {
        CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);   // WDOG1 clock
        IOMUXC_GPR_GPR16 |= 0x200000;   // carried over from WDT_T4 ("fixes reset", PaulS)
        // Power-down counter off: otherwise it asserts WDOG_B 16 s after reset.
        WDOG1_WMCR = 0;
        WDOG1_WCR = (uint16_t)(WDOG_WCR_SRS | WDOG_WCR_WDA | wt |
                               WDOG_WCR_WDE | WDOG_WCR_WDT | WDOG_WCR_SRE);
        detail::wdtStarted = true;
    } else {
        // Every other bit written back as read: SRS and WDA stay 1, and the
        // write-once WDE/WDT/SRE are already set.
        WDOG1_WCR = (uint16_t)((WDOG1_WCR & ~WDOG_WCR_WT(0xFF)) | wt);
    }
    detail::wdogFeed();
    __enable_irq();
}

inline void watchdogReset() {
    detail::wdogFeed();
}

inline void watchdogDisable() {
    // WDOG1 cannot be disabled once enabled (WDE is write-once, #243).
}

} // namespace hal

#endif // HAL_WATCHDOG_TEENSY4_H
