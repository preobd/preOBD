/*
 * can_bxcan.h - STM32 bxCAN driver (STM32F4)
 * Part of the preOBD Hardware Abstraction Layer
 * Note: External CAN transceiver required (SN65HVD230 etc.)
 *
 * HAL bus 0 -> CAN1 on bus_defaults.h's DEFAULT_CAN1_RX/TX, alternate
 * function 9 (Feather F405: RX PB8 = D9, TX PB9 = D10). A second controller (CAN2) exists
 * on the F405, but the Feather breaks out no CAN2 RX pin, so one bus only.
 */

#ifndef HAL_CAN_BXCAN_H
#define HAL_CAN_BXCAN_H

#include <stdint.h>

// Declarations only: the bodies, the ring buffers and the IRQ handlers are
// in can_bxcan.cpp.

namespace hal { namespace can {

bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false);
bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0);
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0);
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0);

/**
 * If the controller is error-passive or bus-off (nothing has ACKed its
 * frames: the ECU is unpowered), drop everything still waiting to go out
 * (#506). A no-op on a healthy bus.
 */
bool abortUnackedTx(uint8_t bus = 0);

}} // namespace hal::can

#endif // HAL_CAN_BXCAN_H
