/*
 * can_bxcan.cpp - STM32 bxCAN driver (see can_bxcan.h)
 *
 * Register-level, against CMSIS names, so no CAN library is needed. The
 * controller has 3 RX FIFO slots and 3 TX mailboxes, too few for a polled
 * loop: an ISO-TP reply or a burst of broadcast frames would overflow them.
 * So both directions go through interrupt-driven rings here: RX frames are
 * copied out of FIFO0 by CAN1_RX0_IRQHandler, and frames that find no free
 * mailbox wait in a TX ring that CAN1_TX_IRQHandler refills from.
 */

#include "../hal_can.h"

#ifdef HAL_CAN_BXCAN_H

#include <Arduino.h>
#include "../bus_defaults.h"

namespace hal { namespace can {

namespace detail {
    struct Frame {
        uint32_t id;
        uint8_t  len;
        uint8_t  extended;
        uint8_t  data[8];
    };

    // Power-of-two sizes: the indices wrap with a mask. One producer and
    // one consumer per ring (ISR and loop), so volatile indices suffice.
    constexpr uint8_t RX_RING = 32;
    constexpr uint8_t TX_RING = 16;
    static Frame rxRing[RX_RING];
    static Frame txRing[TX_RING];
    static volatile uint8_t rxHead = 0, rxTail = 0;
    static volatile uint8_t txHead = 0, txTail = 0;

    // CAN1's registers are reachable once its clock is on, but a controller
    // begin() never configured would sit in sleep mode with frames queued
    // forever. Refuse it, as can_flexcan.cpp does (#560).
    static bool started = false;

    // Bits of CAN_TIxR / CAN_RIxR
    constexpr uint32_t TI_TXRQ = 1UL << 0;
    constexpr uint32_t xI_RTR  = 1UL << 1;
    constexpr uint32_t xI_IDE  = 1UL << 2;

    // Writes one frame into an empty mailbox. Caller has checked TMEx.
    inline void loadMailbox(uint8_t mb, const Frame& f) {
        CAN_TxMailBox_TypeDef& m = CAN1->sTxMailBox[mb];
        m.TIR  = f.extended ? ((f.id << 3) | xI_IDE) : (f.id << 21);
        m.TDTR = f.len;
        m.TDLR = (uint32_t)f.data[0] | ((uint32_t)f.data[1] << 8) |
                 ((uint32_t)f.data[2] << 16) | ((uint32_t)f.data[3] << 24);
        m.TDHR = (uint32_t)f.data[4] | ((uint32_t)f.data[5] << 8) |
                 ((uint32_t)f.data[6] << 16) | ((uint32_t)f.data[7] << 24);
        m.TIR |= TI_TXRQ;
    }

    // Index of an empty mailbox, or -1. TSR.CODE names the next empty one
    // whenever any TMEx is set.
    inline int8_t freeMailbox() {
        const uint32_t tsr = CAN1->TSR;
        if (!(tsr & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2))) return -1;
        return (int8_t)((tsr & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos);
    }

    // Moves queued frames into free mailboxes. Runs in the TX ISR, and from
    // write() with the TX interrupt masked.
    inline void drainTxRing() {
        while (txTail != txHead) {
            const int8_t mb = freeMailbox();
            if (mb < 0) return;
            loadMailbox((uint8_t)mb, txRing[txTail]);
            txTail = (txTail + 1) & (TX_RING - 1);
        }
    }

    // Puts bus_defaults.h's CAN1 pins (the ones the pin registry claims) on
    // alternate function 9. RX gets a pull-up so an unwired RX reads
    // recessive instead of floating.
    inline bool configurePins() {
        const PinName rd = digitalPinToPinName(DEFAULT_CAN1_RX);
        const PinName td = digitalPinToPinName(DEFAULT_CAN1_TX);
        if (rd == NC || td == NC) return false;
        pin_function(rd, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF9_CAN1));
        pin_function(td, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_NOPULL, GPIO_AF9_CAN1));
        return true;
    }

    // Waits for MSR.INAK to reach `want`. Leaving init mode needs 11
    // recessive bits on RX, so with no transceiver (or a dominant-stuck bus)
    // it never happens.
    inline bool waitInak(bool want) {
        const uint32_t t0 = millis();
        while (((CAN1->MSR & CAN_MSR_INAK) != 0) != want) {
            if (millis() - t0 > 10) return false;
        }
        return true;
    }

    // BTR timing for `baud` from the APB1 clock: 1 sync + TS1 + TS2 time
    // quanta per bit, sample point near 87.5 %, SJW 1. 42 MHz gives exact
    // settings at 14 tq for every BUS CAN BAUDRATE (125k..1M).
    inline bool bitTiming(uint32_t baud, uint32_t& btr) {
        const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
        for (uint32_t ntq = 16; ntq >= 8; ntq--) {
            if (pclk % (baud * ntq) != 0) continue;
            const uint32_t brp = pclk / (baud * ntq);
            if (brp < 1 || brp > 1024) continue;
            uint32_t ts2 = (ntq + 4) / 8;         // ~12.5 % of the bit, rounded
            if (ts2 < 2) ts2 = 2;
            const uint32_t ts1 = ntq - 1 - ts2;
            if (ts1 < 1 || ts1 > 16 || ts2 > 8) continue;
            btr = ((ts2 - 1) << CAN_BTR_TS2_Pos) | ((ts1 - 1) << CAN_BTR_TS1_Pos) |
                  (brp - 1);   // SJW = 1 (field 0)
            return true;
        }
        return false;
    }
}

bool begin(uint32_t baudrate, uint8_t bus, bool listenOnly) {
    if (bus != 0) return false;

    uint32_t btr;
    if (!detail::bitTiming(baudrate, btr)) return false;
    if (listenOnly) btr |= CAN_BTR_SILM;

    // A second begin() (BUS CAN BAUDRATE) starts from a reset controller.
    NVIC_DisableIRQ(CAN1_RX0_IRQn);
    NVIC_DisableIRQ(CAN1_TX_IRQn);
    detail::started = false;
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_CAN1_FORCE_RESET();
    __HAL_RCC_CAN1_RELEASE_RESET();
    detail::rxHead = detail::rxTail = 0;
    detail::txHead = detail::txTail = 0;

    if (!detail::configurePins()) return false;

    // Leave sleep, enter init.
    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    if (!detail::waitInak(true)) return false;

    // ABOM: recover from bus-off by itself. TXFP: send the mailboxes in the
    // order they were requested, not by ID, so a multi-frame reply keeps its
    // order. Automatic retransmission stays on (NART clear).
    CAN1->MCR = (CAN1->MCR & ~(CAN_MCR_NART | CAN_MCR_TTCM | CAN_MCR_AWUM | CAN_MCR_RFLM)) |
                CAN_MCR_ABOM | CAN_MCR_TXFP;
    CAN1->BTR = btr;

    // Filter bank 0: 32-bit mask 0 = accept every frame into FIFO0. Banks
    // 0..13 belong to CAN1 (CAN2SB reset value 14).
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R  &= ~1UL;
    CAN1->FM1R  &= ~1UL;   // mask mode
    CAN1->FS1R  |=  1UL;   // single 32-bit
    CAN1->FFA1R &= ~1UL;   // FIFO0
    CAN1->sFilterRegister[0].FR1 = 0;
    CAN1->sFilterRegister[0].FR2 = 0;
    CAN1->FA1R  |=  1UL;
    CAN1->FMR &= ~CAN_FMR_FINIT;

    CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_TMEIE;
    NVIC_SetPriority(CAN1_RX0_IRQn, 5);
    NVIC_SetPriority(CAN1_TX_IRQn, 5);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);
    NVIC_EnableIRQ(CAN1_TX_IRQn);

    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (!detail::waitInak(false)) return false;

    detail::started = true;
    return true;
}

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    if (bus != 0 || !detail::started) return false;
    if (len > 8) len = 8;

    detail::Frame f;
    f.id = extended ? (id & 0x1FFFFFFFUL) : (id & 0x7FFUL);
    f.len = len;
    f.extended = extended ? 1 : 0;
    memset(f.data, 0, sizeof(f.data));
    memcpy(f.data, data, len);

    // Mask the TX interrupt so the ISR's drain can't interleave with ours:
    // queued frames go out before this one, keeping the order of a reply.
    NVIC_DisableIRQ(CAN1_TX_IRQn);
    bool ok = true;
    detail::drainTxRing();
    const int8_t mb = (detail::txHead == detail::txTail) ? detail::freeMailbox() : -1;
    if (mb >= 0) {
        detail::loadMailbox((uint8_t)mb, f);
    } else {
        const uint8_t next = (detail::txHead + 1) & (detail::TX_RING - 1);
        if (next == detail::txTail) {
            ok = false;   // ring full: the bus isn't taking frames
        } else {
            detail::txRing[detail::txHead] = f;
            detail::txHead = next;
        }
    }
    NVIC_EnableIRQ(CAN1_TX_IRQn);
    return ok;
}

bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus) {
    if (bus != 0 || !detail::started) return false;
    if (detail::rxTail == detail::rxHead) return false;
    const detail::Frame& f = detail::rxRing[detail::rxTail];
    id = f.id;
    len = f.len;
    extended = f.extended != 0;
    memcpy(data, f.data, f.len);
    detail::rxTail = (detail::rxTail + 1) & (detail::RX_RING - 1);
    return true;
}

// Every frame is accepted (filter bank 0, mask 0): CAN input imports need
// all of them, and the OBD-II responder picks out 0x7DF/0x7E0 in software.
// That matches FlexCAN, where MB2-7 stay accept-all beside these filters.
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus) {
    (void)filter1; (void)filter2; (void)bus;
}

bool abortUnackedTx(uint8_t bus) {
    if (bus != 0 || !detail::started) return false;
    // ESR: EPVF error-passive, BOFF bus-off. Error-active means frames are
    // being ACKed, so let them go.
    if (!(CAN1->ESR & (CAN_ESR_EPVF | CAN_ESR_BOFF))) return false;

    NVIC_DisableIRQ(CAN1_TX_IRQn);
    bool aborted = detail::txHead != detail::txTail;
    detail::txTail = detail::txHead;
    const uint32_t tsr = CAN1->TSR;
    if (!(tsr & CAN_TSR_TME0)) { CAN1->TSR = CAN_TSR_ABRQ0; aborted = true; }
    if (!(tsr & CAN_TSR_TME1)) { CAN1->TSR = CAN_TSR_ABRQ1; aborted = true; }
    if (!(tsr & CAN_TSR_TME2)) { CAN1->TSR = CAN_TSR_ABRQ2; aborted = true; }
    NVIC_EnableIRQ(CAN1_TX_IRQn);
    return aborted;
}

}} // namespace hal::can

extern "C" void CAN1_RX0_IRQHandler(void) {
    using namespace hal::can::detail;
    while (CAN1->RF0R & CAN_RF0R_FMP0) {
        const CAN_FIFOMailBox_TypeDef& m = CAN1->sFIFOMailBox[0];
        const uint8_t next = (rxHead + 1) & (RX_RING - 1);
        // RTR frames carry no data and nothing here asks for them: skip.
        if (next != rxTail && !(m.RIR & xI_RTR)) {
            Frame& f = rxRing[rxHead];
            const uint32_t rir = m.RIR;
            f.extended = (rir & xI_IDE) ? 1 : 0;
            f.id = f.extended ? (rir >> 3) : (rir >> 21);
            f.len = m.RDTR & 0x0F;
            if (f.len > 8) f.len = 8;
            const uint32_t lo = m.RDLR, hi = m.RDHR;
            for (uint8_t i = 0; i < 4; i++) {
                f.data[i] = (uint8_t)(lo >> (8 * i));
                f.data[i + 4] = (uint8_t)(hi >> (8 * i));
            }
            rxHead = next;
        }
        CAN1->RF0R = CAN_RF0R_RFOM0;   // release; a full ring drops the frame
    }
    CAN1->RF0R = CAN_RF0R_FOVR0 | CAN_RF0R_FULL0;   // write-1-to-clear
}

extern "C" void CAN1_TX_IRQHandler(void) {
    // A mailbox finished (sent or aborted): clear its RQCP, which is what
    // raised TMEIE, then refill from the ring.
    CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2;
    hal::can::detail::drainTxRing();
}

#endif // HAL_CAN_BXCAN_H
