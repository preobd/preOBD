/*
 * can_flexcan.h - FlexCAN driver for Teensy 3.x/4.x
 * Part of the preOBD Hardware Abstraction Layer
 *
 * Supports multiple CAN buses:
 * - Bus 0: CAN1 (available on all Teensy 3.x/4.x)
 * - Bus 1: CAN2 (Teensy 3.6, 4.0, 4.1)
 * - Bus 2: CAN3 (Teensy 4.1 only)
 */

#ifndef HAL_CAN_FLEXCAN_H
#define HAL_CAN_FLEXCAN_H

#include <FlexCAN_T4.h>

namespace hal { namespace can {

#ifdef ENABLE_CAN_HYBRID
// In hybrid mode, wrap in flexcan namespace for dispatcher
namespace flexcan {
#endif

namespace detail {
    // Static instances in detail namespace to avoid ODR issues
    static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> canBus0;
    #if defined(CAN2)
        static FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> canBus1;
    #endif
    #if defined(CAN3)
        static FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> canBus2;
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

inline bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false) {
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

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0) {
    CAN_message_t msg;
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

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0) {
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

inline void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0) {
    switch (bus) {
        case 0:
            detail::canBus0.setMBFilter(MB0, filter1);
            detail::canBus0.setMBFilter(MB1, filter2);
            detail::canBus0.enableMBInterrupt(MB0);
            detail::canBus0.enableMBInterrupt(MB1);
            break;
        #if defined(CAN2)
        case 1:
            detail::canBus1.setMBFilter(MB0, filter1);
            detail::canBus1.setMBFilter(MB1, filter2);
            detail::canBus1.enableMBInterrupt(MB0);
            detail::canBus1.enableMBInterrupt(MB1);
            break;
        #endif
        #if defined(CAN3)
        case 2:
            detail::canBus2.setMBFilter(MB0, filter1);
            detail::canBus2.setMBFilter(MB1, filter2);
            detail::canBus2.enableMBInterrupt(MB0);
            detail::canBus2.enableMBInterrupt(MB1);
            break;
        #endif
    }
}

#ifdef ENABLE_CAN_HYBRID
} // namespace flexcan
#endif

}} // namespace hal::can

#endif // HAL_CAN_FLEXCAN_H
