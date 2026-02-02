/*
 * can_flexcan.h - FlexCAN driver for Teensy 3.x/4.x
 * Part of the openEMS Hardware Abstraction Layer
 */

#ifndef HAL_CAN_FLEXCAN_H
#define HAL_CAN_FLEXCAN_H

#include <FlexCAN_T4.h>

namespace hal { namespace can {

// CAN bus instance (using CAN1)
static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> canBus;

inline bool begin(uint32_t baudrate) {
    canBus.begin();
    canBus.setBaudRate(baudrate);
    canBus.setMaxMB(16);
    return true;
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended) {
    CAN_message_t msg;
    msg.id = id;
    msg.len = len;
    msg.flags.extended = extended ? 1 : 0;
    msg.flags.remote = 0;
    memcpy(msg.buf, data, len);
    return canBus.write(msg) > 0;
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended) {
    CAN_message_t msg;
    if (canBus.read(msg)) {
        id = msg.id;
        len = msg.len;
        extended = msg.flags.extended;
        memcpy(data, msg.buf, msg.len);
        return true;
    }
    return false;
}

inline bool available() {
    // FlexCAN_T4 doesn't have a simple available() - read() returns false if empty
    return true;  // Always check via read()
}

inline void setFilters(uint32_t filter1, uint32_t filter2) {
    canBus.setMBFilter(MB0, filter1);
    canBus.setMBFilter(MB1, filter2);
    canBus.enableMBInterrupt(MB0);
    canBus.enableMBInterrupt(MB1);
}

}} // namespace hal::can

#endif // HAL_CAN_FLEXCAN_H
