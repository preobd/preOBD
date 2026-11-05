/*
 * can_flexcan.h - FlexCAN driver for Teensy 3.x/4.x
 * Part of the preOBD Hardware Abstraction Layer
 *
 * HAL bus -> FlexCAN controller (map in can_flexcan.cpp):
 * - Teensy 4.0/4.1: bus 0 CAN1, bus 1 CAN2, bus 2 CAN3
 * - Teensy 3.6:     bus 0 CAN0, bus 1 CAN1
 * - Teensy 3.2/3.5: bus 0 CAN0
 */

#ifndef HAL_CAN_FLEXCAN_H
#define HAL_CAN_FLEXCAN_H

#include <stdint.h>

// Declarations only: the bodies, the driver objects and the one include of
// FlexCAN_T4 are in can_flexcan.cpp (see there for why it must be one file).

namespace hal { namespace can {

#if INCLUDE_CAN_HYBRID
// In hybrid mode, wrap in flexcan namespace for dispatcher
namespace flexcan {
#endif

bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false);
bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0);
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0);
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0);

/**
 * If the controller is error-passive or bus-off (nothing has ACKed its
 * frames: the ECU is unpowered), stop retransmitting what is still
 * waiting to go out (#506). A no-op on a healthy bus.
 */
bool abortUnackedTx(uint8_t bus = 0);

#if INCLUDE_CAN_HYBRID
} // namespace flexcan
#endif

}} // namespace hal::can

#endif // HAL_CAN_FLEXCAN_H
