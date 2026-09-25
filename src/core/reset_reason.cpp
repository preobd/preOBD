/*
 * reset_reason.cpp - Why did the MCU last reset? (#454)
 *
 * The labels are PROGMEM arrays rather than F() literals: on Teensy 4.x F()
 * is a bare cast and would park every label in RAM1.
 */

#include "reset_reason.h"

#if defined(ESP32)
#include <esp_system.h>
#endif

#ifndef FLASHMEM
#define FLASHMEM
#endif

namespace {

enum ResetReason : uint8_t {
    RR_UNKNOWN = 0,
    RR_POWER_ON,      // power-on, reset pin, or a firmware upload
    RR_WATCHDOG,
    RR_FAULT,         // CPU fault / panic (rebooted by the core or our watchdog)
    RR_SOFTWARE,      // deliberate reboot (SYSTEM REBOOT, esp_restart)
    RR_LOCKUP,        // SYSTEM REBOOT, or a CPU lockup: SRSR can't tell them apart
    RR_BROWNOUT,
    RR_CLOCK_LOSS,
    RR_OVER_TEMP,
    RR_DEBUGGER,
    RR_WAKEUP,
    RR_COUNT
};

const char RR_S_UNKNOWN[]   PROGMEM = "unknown";
const char RR_S_POWER_ON[]  PROGMEM = "power-on";
const char RR_S_WATCHDOG[]  PROGMEM = "WATCHDOG";
const char RR_S_FAULT[]     PROGMEM = "CPU FAULT / panic";
const char RR_S_SOFTWARE[]  PROGMEM = "software reboot";
const char RR_S_LOCKUP[]    PROGMEM = "reboot/lockup";
const char RR_S_BROWNOUT[]  PROGMEM = "BROWN-OUT (low voltage)";
const char RR_S_CLOCK[]     PROGMEM = "CLOCK LOSS";
const char RR_S_OVER_TEMP[] PROGMEM = "OVER-TEMPERATURE";
const char RR_S_DEBUGGER[]  PROGMEM = "debugger";
const char RR_S_WAKEUP[]    PROGMEM = "wake from sleep";

const char* const RR_LABELS[RR_COUNT] PROGMEM = {
    RR_S_UNKNOWN, RR_S_POWER_ON, RR_S_WATCHDOG, RR_S_FAULT, RR_S_SOFTWARE,
    RR_S_LOCKUP, RR_S_BROWNOUT, RR_S_CLOCK, RR_S_OVER_TEMP, RR_S_DEBUGGER,
    RR_S_WAKEUP,
};

uint8_t s_reason = RR_UNKNOWN;

#if defined(__IMXRT1062__)
// The Teensy core's fault handler leaves an arm_fault_info_struct (imxrt.h)
// here and reboots. We read and clear it ourselves rather than through the
// CrashReport object: referencing that object links its virtual printTo(),
// whose string literals cost ~2 KB of RAM1 on Teensy 4.x. The validity check
// mirrors isvalid() in the core's CrashReport.cpp.
arm_fault_info_struct* const FAULT_INFO = (arm_fault_info_struct*)0x2027FF80;

FLASHMEM bool faultInfoValid() {
    const arm_fault_info_struct* info = FAULT_INFO;
    if (info->len != sizeof(*info) / 4) return false;
    const uint32_t* data = (const uint32_t*)info;
    const uint32_t* end = data + (sizeof(*info) / 4 - 1);
    uint32_t crc = 0xFFFFFFFF;
    while (data < end) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 32; i++) crc = (crc >> 1) ^ (crc & 1) * 0xEDB88320;
    }
    return crc == info->crc;
}

// Raw register kept for the report: SRSR bits accumulate until cleared, so a
// value with more than one bit set is worth seeing verbatim.
uint32_t s_srsr = 0;
// WDOG1's own reset-status register: tells a real watchdog timeout (TOUT)
// apart from a WDOG bit that SRSR sets alongside a software reboot.
uint16_t s_wrsr = 0;
// Fault detail from the Teensy core's CrashReport block, when it is valid.
bool     s_hasFault = false;
uint32_t s_faultAddr = 0;
uint32_t s_faultCfsr = 0;
#endif

} // namespace

FLASHMEM void resetReasonCapture() {
#if defined(__IMXRT1062__)
    const uint32_t srsr = SRC_SRSR;
    s_srsr = srsr;
    s_wrsr = WDOG1_WRSR;

    // The core's fault handler fills the fault block and, if it gets to
    // reboot on its own, does so via SCB_AIRCR with SRC_GPR5 = 0x0BAD00F1 —
    // which is how that is told apart from our own SYSTEM REBOOT.
    if (faultInfoValid()) {
        s_hasFault = true;
        s_faultAddr = FAULT_INFO->ret;
        s_faultCfsr = FAULT_INFO->cfsr;
    }

    // SRSR is cleared every boot, so every set bit belongs to this reset, but
    // one reset can set several — hence the order, which is bench-measured on
    // a teensy40 (#454):
    //  - a fault parks the CPU in the core's handler, and our 2 s watchdog
    //    fires before the handler's own 8 s auto-reboot, so SRSR reads
    //    WDOG_RST_B alone; only the valid fault block says what happened.
    //  - a software reboot (SCB_AIRCR) with the watchdog armed reads
    //    LOCKUP_SYSRESETREQ | WDOG_RST_B, while a genuine watchdog timeout
    //    reads WDOG_RST_B alone. It is one reset, not a reboot followed by a
    //    timeout: WDOG1_WRSR reads SFTW (0x1) after SYSTEM REBOOT and TOUT
    //    (0x2) after a hang.
    // The fault block survives warm resets, so it names the cause only when
    // this reset is one a fault leads to (watchdog or the handler's own
    // SCB_AIRCR reboot). A reset pin or upload during the handler's 8 s wait
    // stays a power-on, and the fault is still printed as detail.
    const uint32_t faultResets = SRC_SRSR_WDOG_RST_B | SRC_SRSR_WDOG3_RST_B |
                                 SRC_SRSR_LOCKUP_SYSRESETREQ;
    const bool gpr5Fault = (SRC_GPR5 == 0x0BAD00F1);
    if (srsr & SRC_SRSR_TEMPSENSE_RST_B)                 s_reason = RR_OVER_TEMP;
    else if (gpr5Fault || (s_hasFault && (srsr & faultResets))) s_reason = RR_FAULT;
    else if (srsr & SRC_SRSR_LOCKUP_SYSRESETREQ)         s_reason = RR_LOCKUP;
    else if (srsr & (SRC_SRSR_WDOG_RST_B | SRC_SRSR_WDOG3_RST_B)) s_reason = RR_WATCHDOG;
    else if (srsr & (SRC_SRSR_JTAG_RST_B | SRC_SRSR_JTAG_SW_RST)) s_reason = RR_DEBUGGER;
    else if (srsr & (SRC_SRSR_IPP_RESET_B | SRC_SRSR_IPP_USER_RESET_B)) s_reason = RR_POWER_ON;

    // Invalidate the fault block and clear SRC_GPR5 and every write-1-to-clear
    // SRSR bit, so the next boot reports only its own reset.
    FAULT_INFO->len = 0;
    FAULT_INFO->crc = 0;
    arm_dcache_flush_delete(FAULT_INFO, sizeof(*FAULT_INFO));
    // Two writes: the W1C bits clear on the first, while TEMPSENSE is cleared
    // by writing 0 (as the core's CrashReport does) and ignores the 1. Each
    // kind ignores the other write.
    SRC_SRSR = srsr;
    SRC_SRSR = srsr & ~SRC_SRSR_TEMPSENSE_RST_B;
    SRC_GPR5 = 0;

#elif defined(__MK20DX256__) || defined(__MK20DX128__) || \
      defined(__MK64FX512__) || defined(__MK66FX1M0__)
    // RCM_SRS0/1 describe only the most recent reset; nothing to clear.
    // POR also sets LVD (reference manual), so POR must be tested first or
    // every cold boot would read as a brown-out.
    const uint8_t srs0 = RCM_SRS0;
    const uint8_t srs1 = RCM_SRS1;
    if (srs0 & RCM_SRS0_WDOG)                        s_reason = RR_WATCHDOG;
    else if (srs1 & RCM_SRS1_LOCKUP)                 s_reason = RR_LOCKUP;
    else if (srs1 & RCM_SRS1_SW)                     s_reason = RR_SOFTWARE;
    else if (srs0 & RCM_SRS0_POR)                    s_reason = RR_POWER_ON;
    else if (srs0 & RCM_SRS0_LVD)                    s_reason = RR_BROWNOUT;
    else if (srs0 & (RCM_SRS0_LOL | RCM_SRS0_LOC))   s_reason = RR_CLOCK_LOSS;
    else if (srs1 & (RCM_SRS1_JTAG | RCM_SRS1_MDM_AP)) s_reason = RR_DEBUGGER;
    else if (srs0 & RCM_SRS0_WAKEUP)                 s_reason = RR_WAKEUP;
    else if (srs0 & RCM_SRS0_PIN)                    s_reason = RR_POWER_ON;

#elif defined(ESP32)
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
        case ESP_RST_EXT:       s_reason = RR_POWER_ON; break;
        case ESP_RST_SW:        s_reason = RR_SOFTWARE; break;
        case ESP_RST_PANIC:     s_reason = RR_FAULT;    break;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       s_reason = RR_WATCHDOG; break;
        case ESP_RST_BROWNOUT:  s_reason = RR_BROWNOUT; break;
        case ESP_RST_DEEPSLEEP: s_reason = RR_WAKEUP;   break;
        default:                s_reason = RR_UNKNOWN;  break;
    }
#endif
}

FLASHMEM bool resetReasonAbnormal() {
    switch (s_reason) {
        case RR_WATCHDOG:
        case RR_FAULT:
        case RR_BROWNOUT:
        case RR_CLOCK_LOSS:
        case RR_OVER_TEMP:
            return true;
        default:
            // A lockup shares RR_LOCKUP with SYSTEM REBOOT, so it stays quiet
            // at boot rather than warning after every deliberate reboot;
            // VERSION still shows it.
            return false;
    }
}

FLASHMEM void printResetReason(Print& out, bool detail) {
    out.print(F("Last reset: "));
    out.print((const __FlashStringHelper*)pgm_read_ptr(&RR_LABELS[s_reason]));
#if defined(__IMXRT1062__)
    if (detail) {
        out.print(F(" (SRSR 0x"));
        out.print(s_srsr, HEX);
        out.print(F(" WRSR 0x"));
        out.print(s_wrsr, HEX);
        out.print(')');
        if (s_hasFault) {
            // `ret` is the stacked PC (the core's "Code was executing from"),
            // not the faulting data address.
            out.print(F(", fault PC 0x"));
            out.print(s_faultAddr, HEX);
            out.print(F(" CFSR 0x"));
            out.print(s_faultCfsr, HEX);
        }
    }
#endif
    out.println();
}
