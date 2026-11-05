/*
 * can_flexcan.cpp - FlexCAN driver for Teensy 3.x/4.x (see can_flexcan.h)
 *
 * The ONLY file that includes FlexCAN_T4. The library keeps its ISR target
 * (_CAN1) and ISR functions as `static`s in its header, and its template
 * code is merged across files by the linker, each merged copy using the
 * statics of whichever file it came from. With FlexCAN_T4 in more than one
 * file, begin() can install an ISR that reads another file's _CAN1, still
 * null, so the interrupt is never serviced and re-fires until the watchdog
 * resets the board (bench, 2026-10-12). And each file that defined the
 * driver object had its own, 10.5 KB of RAM1 apiece on Teensy 4.
 * tools/check_layering.py fails a commit that includes FlexCAN_T4 anywhere
 * else.
 */

#include "../hal_can.h"

#ifdef HAL_CAN_FLEXCAN_H

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

#include <FlexCAN_T4.h>

namespace hal { namespace can {

#if INCLUDE_CAN_HYBRID
// In hybrid mode, wrap in flexcan namespace for dispatcher
namespace flexcan {
#endif

namespace detail {
    // RX is polled straight from the mailboxes (see setFilters), so the
    // library's RX ring is never filled: keep it at its smallest.
    // CAN2/CAN3 below are FlexCAN_T4 enum values, not macros, so those
    // branches never compile (only CAN1 works on Teensy, #554).
    static FlexCAN_T4<CAN1, RX_SIZE_2, TX_SIZE_16> canBus0;
    #if defined(CAN2)
        static FlexCAN_T4<CAN2, RX_SIZE_2, TX_SIZE_16> canBus1;
    #endif
    #if defined(CAN3)
        static FlexCAN_T4<CAN3, RX_SIZE_2, TX_SIZE_16> canBus2;
    #endif

    // Helper to initialize a specific bus instance
    template<typename T>
    inline void initBus(T& bus, uint32_t baudrate, bool listenOnly = false) {
        bus.begin();
        bus.setBaudRate(baudrate, listenOnly ? LISTEN_ONLY : TX);
        bus.setMaxMB(16);
        // Configure first 8 mailboxes for RX
        for (int i = 0; i < 8; i++) {
            bus.setMB((FLEXCAN_MAILBOX)i, RX, STD);
        }
    }
}

FLASHMEM bool begin(uint32_t baudrate, uint8_t bus, bool listenOnly) {
    switch (bus) {
        case 0:
            detail::initBus(detail::canBus0, baudrate, listenOnly);
            return true;
        #if defined(CAN2)
        case 1:
            detail::initBus(detail::canBus1, baudrate, listenOnly);
            return true;
        #endif
        #if defined(CAN3)
        case 2:
            detail::initBus(detail::canBus2, baudrate, listenOnly);
            return true;
        #endif
        default:
            return false;
    }
}

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    CAN_message_t msg;
    if (len > 8) len = 8;
    msg.id = id;
    msg.len = len;
    msg.flags.extended = extended ? 1 : 0;
    msg.flags.remote = 0;
    memcpy(msg.buf, data, len);

    switch (bus) {
        case 0:
            return detail::canBus0.write(msg) > 0;
        #if defined(CAN2)
        case 1:
            return detail::canBus1.write(msg) > 0;
        #endif
        #if defined(CAN3)
        case 2:
            return detail::canBus2.write(msg) > 0;
        #endif
        default:
            return false;
    }
}

bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus) {
    CAN_message_t msg;
    bool result = false;

    switch (bus) {
        case 0:
            result = detail::canBus0.read(msg);
            break;
        #if defined(CAN2)
        case 1:
            result = detail::canBus1.read(msg);
            break;
        #endif
        #if defined(CAN3)
        case 2:
            result = detail::canBus2.read(msg);
            break;
        #endif
        default:
            return false;
    }

    if (result) {
        id = msg.id;
        len = msg.len;
        extended = msg.flags.extended;
        memcpy(data, msg.buf, msg.len);
        return true;
    }
    return false;
}

// Mailboxes stay POLLED: read() skips interrupt-enabled mailboxes, and with
// no FlexCAN_T4 callbacks registered the ISR drops their frames. Enabling
// the MB0/MB1 interrupts here meant every 0x7DF/0x7E0 request vanished and
// the OBD-II responder never answered over CAN (#495).
FLASHMEM void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus) {
    switch (bus) {
        case 0:
            detail::canBus0.setMBFilter(MB0, filter1);
            detail::canBus0.setMBFilter(MB1, filter2);
            break;
        #if defined(CAN2)
        case 1:
            detail::canBus1.setMBFilter(MB0, filter1);
            detail::canBus1.setMBFilter(MB1, filter2);
            break;
        #endif
        #if defined(CAN3)
        case 2:
            detail::canBus2.setMBFilter(MB0, filter1);
            detail::canBus2.setMBFilter(MB1, filter2);
            break;
        #endif
    }
}

namespace detail {
    // Register offsets are the same on Kinetis (3.x) and i.MX RT (4.x);
    // FlexCAN_T4 names the macros differently on each, so spell them out
    inline volatile uint32_t& esr1(uint32_t base) { return *(volatile uint32_t*)(base + 0x20); }
    inline volatile uint32_t& imask1(uint32_t base) { return *(volatile uint32_t*)(base + 0x28); }
    inline volatile uint32_t& iflag1(uint32_t base) { return *(volatile uint32_t*)(base + 0x30); }
    inline volatile uint32_t& mbCS(uint32_t base, uint8_t n) {
        return *(volatile uint32_t*)(base + 0x80 + n * 0x10);
    }

    // Mailbox i's abort has finished: clear its flag and hand it back as
    // TX_INACTIVE with its interrupt on, as setMaxMB left it, so write() can
    // use it again
    template<typename T>
    inline void finishAbort(T& bus, uint32_t base, uint8_t i) {
        iflag1(base) = 1UL << i;   // write-1-to-clear
        bus.setMB((FLEXCAN_MAILBOX)i, TX);
    }

    template<typename T>
    inline bool abortUnacked(T& bus, uint32_t base) {
        // initBus: setMaxMB(16) leaves MB0-7 RX and MB8-15 TX.
        // First finish any abort a previous call left pending (masked, no
        // longer TX_ONCE), whatever the bus state is now.
        for (uint8_t i = 8; i < 16; i++) {
            if (!(imask1(base) & (1UL << i)) &&
                ((mbCS(base, i) >> 24) & 0x0F) != FLEXCAN_MB_CODE_TX_ONCE) {
                finishAbort(bus, base, i);
            }
        }

        // FLTCONF (ESR1[5:4]): 00 error-active, 01 passive, 1x bus-off
        if ((esr1(base) & 0x30) == 0) return false;
        bool aborted = false;
        for (uint8_t i = 8; i < 16; i++) {
            if (((mbCS(base, i) >> 24) & 0x0F) != FLEXCAN_MB_CODE_TX_ONCE) continue;
            // FlexCAN_T4 sets MCR[AEN], so only the ABORT code stops a
            // pending frame; writing TX_INACTIVE is ignored (bench, #506).
            // The abort completes when the attempt on the wire ends: one
            // unACKed attempt plus error frame, ~140 bit times (~280 us at
            // 500 kbps, ~1.1 ms at 125 kbps, the slowest BUS CAN BAUDRATE).
            //
            // Mask the mailbox's interrupt first. A finished abort sets its
            // IFLAG, and FlexCAN_T4's ISR has no case for the ABORT code, so
            // it never clears the flag and re-enters until the watchdog
            // resets the board (bench, #506). The library can't mask it
            // (disableMBInterrupt keeps TX interrupts on), hence registers.
            imask1(base) &= ~(1UL << i);
            mbCS(base, i) = (mbCS(base, i) & ~(0x0FUL << 24)) |
                            ((uint32_t)FLEXCAN_MB_CODE_TX_ABORT << 24);
            uint32_t t0 = micros();
            while (((mbCS(base, i) >> 24) & 0x0F) == FLEXCAN_MB_CODE_TX_ONCE &&
                   micros() - t0 < 3000) {}
            aborted = true;
            // Still on the wire: leave it masked. Unmasking now would let the
            // abort land under the ISR (#522 review); the next call finishes it.
            if (((mbCS(base, i) >> 24) & 0x0F) == FLEXCAN_MB_CODE_TX_ONCE) continue;
            finishAbort(bus, base, i);
        }
        return aborted;
    }
}

/**
 * If the controller is error-passive or bus-off (nothing has ACKed its
 * frames: the ECU is unpowered), stop retransmitting what is still
 * waiting to go out (#506). A no-op on a healthy bus.
 */
bool abortUnackedTx(uint8_t bus) {
    switch (bus) {
        case 0: return detail::abortUnacked(detail::canBus0, (uint32_t)CAN1);
        #if defined(CAN2)
        case 1: return detail::abortUnacked(detail::canBus1, (uint32_t)CAN2);
        #endif
        #if defined(CAN3)
        case 2: return detail::abortUnacked(detail::canBus2, (uint32_t)CAN3);
        #endif
        default: return false;
    }
}

#if INCLUDE_CAN_HYBRID
} // namespace flexcan
#endif

}} // namespace hal::can

#endif // HAL_CAN_FLEXCAN_H
