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

// HAL bus number -> FlexCAN controller. FlexCAN_T4 names the controllers
// with CAN_DEV_TABLE enum values, not macros, so they can't be tested with
// #if defined(CAN2): that hid buses 1 and 2 on every Teensy 4 (#554). The
// map follows bus_defaults.h's pins: Teensy 3.x bus 0 is CAN0 (pins 3/4),
// not CAN1 (the 3.6's second controller, pins 33/34).
#if defined(__IMXRT1062__)
    #define FLEXCAN_BUS0_DEV CAN1   // CTX1 22 / CRX1 23
    #define FLEXCAN_BUS1_DEV CAN2   // CTX2 1  / CRX2 0
    #define FLEXCAN_BUS2_DEV CAN3   // CTX3 31 / CRX3 30
#elif defined(__MK66FX1M0__)
    #define FLEXCAN_BUS0_DEV CAN0   // pins 3/4
    #define FLEXCAN_BUS1_DEV CAN1   // pins 33/34
#elif defined(__MK64FX512__) || defined(__MK20DX256__)
    #define FLEXCAN_BUS0_DEV CAN0   // pins 3/4
#else
    #error "can_flexcan.cpp: no FlexCAN controller map for this chip"
#endif

namespace hal { namespace can {

namespace detail {
    // RX is polled straight from the mailboxes (see setFilters), so the
    // library's RX ring is never filled: keep it at its smallest.
    static FlexCAN_T4<FLEXCAN_BUS0_DEV, RX_SIZE_2, TX_SIZE_16> canBus0;
    #ifdef FLEXCAN_BUS1_DEV
        static FlexCAN_T4<FLEXCAN_BUS1_DEV, RX_SIZE_2, TX_SIZE_16> canBus1;
    #endif
    #ifdef FLEXCAN_BUS2_DEV
        static FlexCAN_T4<FLEXCAN_BUS2_DEV, RX_SIZE_2, TX_SIZE_16> canBus2;
    #endif

    // Controllers begin() has started. A FlexCAN controller that was never
    // begun has its clock gated on Teensy 4, and touching its registers is a
    // hard fault, so read/write/setFilters/abort refuse any other bus (#560).
    static bool started[3] = {false, false, false};
    inline bool isStarted(uint8_t bus) { return bus < 3 && started[bus]; }

    // Helper to initialize a specific bus instance
    template<typename T>
    inline void initBus(T& bus, uint32_t baudrate, bool listenOnly = false) {
    #if defined(__IMXRT1062__)
        // With the CAN clock root still off, FlexCAN_T4's begin() calls
        // setClock(), which runs setBaudRate() on EVERY constructed
        // controller: CAN2/CAN3 too, whose clock gates are still off, and
        // touching a gated FlexCAN is a hard fault (boot hang, #560 review).
        // Select the root ourselves, as setClock(CLK_24MHz) would, so begin()
        // never calls it. The root survives a warm reset, which is why this
        // only struck some boots.
        if (((CCM_CSCMR2 >> 8) & 0x3) == 3) {
            CCM_CSCMR2 = (CCM_CSCMR2 & 0xFFFFFC03) | CCM_CSCMR2_CAN_CLK_SEL(1) |
                         CCM_CSCMR2_CAN_CLK_PODF(0);
        }
    #endif
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
            detail::started[0] = true;
            return true;
        #ifdef FLEXCAN_BUS1_DEV
        case 1:
            detail::initBus(detail::canBus1, baudrate, listenOnly);
            detail::started[1] = true;
            return true;
        #endif
        #ifdef FLEXCAN_BUS2_DEV
        case 2:
            detail::initBus(detail::canBus2, baudrate, listenOnly);
            detail::started[2] = true;
            return true;
        #endif
        default:
            return false;
    }
}

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    if (!detail::isStarted(bus)) return false;
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
        #ifdef FLEXCAN_BUS1_DEV
        case 1:
            return detail::canBus1.write(msg) > 0;
        #endif
        #ifdef FLEXCAN_BUS2_DEV
        case 2:
            return detail::canBus2.write(msg) > 0;
        #endif
        default:
            return false;
    }
}

bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus) {
    if (!detail::isStarted(bus)) return false;
    CAN_message_t msg;
    bool result = false;

    switch (bus) {
        case 0:
            result = detail::canBus0.read(msg);
            break;
        #ifdef FLEXCAN_BUS1_DEV
        case 1:
            result = detail::canBus1.read(msg);
            break;
        #endif
        #ifdef FLEXCAN_BUS2_DEV
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
    if (!detail::isStarted(bus)) return;
    switch (bus) {
        case 0:
            detail::canBus0.setMBFilter(MB0, filter1);
            detail::canBus0.setMBFilter(MB1, filter2);
            break;
        #ifdef FLEXCAN_BUS1_DEV
        case 1:
            detail::canBus1.setMBFilter(MB0, filter1);
            detail::canBus1.setMBFilter(MB1, filter2);
            break;
        #endif
        #ifdef FLEXCAN_BUS2_DEV
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
    if (!detail::isStarted(bus)) return false;
    switch (bus) {
        case 0: return detail::abortUnacked(detail::canBus0, (uint32_t)FLEXCAN_BUS0_DEV);
        #ifdef FLEXCAN_BUS1_DEV
        case 1: return detail::abortUnacked(detail::canBus1, (uint32_t)FLEXCAN_BUS1_DEV);
        #endif
        #ifdef FLEXCAN_BUS2_DEV
        case 2: return detail::abortUnacked(detail::canBus2, (uint32_t)FLEXCAN_BUS2_DEV);
        #endif
        default: return false;
    }
}

}} // namespace hal::can

#endif // HAL_CAN_FLEXCAN_H
