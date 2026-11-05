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
 * Pre-timeout interrupt (#542): WICR is armed in the same one-time setup
 * (WIE and WICT are write-once) to interrupt 0.5 s before the reset. The
 * handler reads the interrupted PC/LR from the exception frame and passes them
 * to the hook core/watchdog.cpp registers, which records them where the next
 * boot can read them. It runs at the highest priority so it still fires when
 * the hang is in a lower-priority ISR; a hang with interrupts masked can't be
 * caught this way.
 */

#ifndef HAL_WATCHDOG_TEENSY4_H
#define HAL_WATCHDOG_TEENSY4_H

#include <Arduino.h>

namespace hal {

// WICR fields (i.MX RT1060 RM, WDOG_WICR; imxrt.h has no macros for them).
#define PREOBD_WDOG_WICR_WIE      ((uint16_t)(1u << 15))   // interrupt enable, write-once
#define PREOBD_WDOG_WICR_WTIS     ((uint16_t)(1u << 14))   // interrupt status, write 1 to clear
#define PREOBD_WDOG_WICR_WICT(n)  ((uint16_t)((n) & 0xFF)) // 0.5 s units before timeout, write-once

namespace detail {
    static bool wdtStarted = false;
    static void (*preTimeoutHook)(uint32_t pc, uint32_t lr) = nullptr;

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

} // namespace hal

// Defined in this header (extern "C", no inline) because only core/watchdog.cpp
// includes it; a second includer gets a multiple-definition link error.
// Pre-timeout handler. Naked so the exception frame is still where the
// hardware put it: pick MSP or PSP from EXC_RETURN and pass the frame on.
// Frame: r0 r1 r2 r3 r12 lr pc xpsr.
extern "C" void preobdWdogPreTimeoutC(const uint32_t* frame) {
    WDOG1_WICR = (uint16_t)(WDOG1_WICR | PREOBD_WDOG_WICR_WTIS);   // ack
    if (hal::detail::preTimeoutHook) hal::detail::preTimeoutHook(frame[6], frame[5]);
}
extern "C" __attribute__((naked)) void preobdWdogPreTimeoutIsr() {
    __asm__ volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b preobdWdogPreTimeoutC \n");
}

namespace hal {

// Teensy 4 only: called from the pre-timeout interrupt with the interrupted
// PC and LR. Set it before the first watchdogEnable().
inline void watchdogSetPreTimeoutHook(void (*hook)(uint32_t pc, uint32_t lr)) {
    detail::preTimeoutHook = hook;
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
        // Pre-timeout interrupt 0.5 s (WICT = 1) before the reset (#542).
        WDOG1_WICR = (uint16_t)(PREOBD_WDOG_WICR_WIE | PREOBD_WDOG_WICR_WTIS |
                                PREOBD_WDOG_WICR_WICT(1));
        attachInterruptVector(IRQ_WDOG1, preobdWdogPreTimeoutIsr);
        NVIC_SET_PRIORITY(IRQ_WDOG1, 0);
        NVIC_ENABLE_IRQ(IRQ_WDOG1);
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
