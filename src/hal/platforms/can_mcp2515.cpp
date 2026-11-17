/*
 * can_mcp2515.cpp - the MCP2515 driver objects, one per controller
 *
 * can_mcp2515.h declares them; this is the one definition. As `static`s in
 * the header, every file including hal_can.h had its own MCP2515 object and
 * its own "started" flags for the same chip. It worked only because the
 * inline HAL functions are merged across files by the linker and so all
 * used one file's copies (an ODR violation that happened to hold; bench,
 * Mega, 2026-10-12).
 */

#include "../hal_can.h"
#include <new>   // placement new

#if INCLUDE_CAN

namespace hal { namespace can {

#ifdef HAL_CAN_MCP2515_H
namespace detail {
    // Built in place by begin(), not at static init. The library's
    // constructor calls SPI.begin() and drives CS HIGH (autowp mcp2515.cpp),
    // which tools/check_pins.py can't see; as a static it did so before
    // setup(), on every SPI-CAN build, whether or not the CS claim held.
    // begin() is reached only through can_bus.cpp startBus() -> initCANBus(),
    // which refuses a bus whose CS claim didn't hold (§12.5, #646).
    // Placement new, not a function-local static: no guard variable on AVR.
    alignas(MCP2515) static uint8_t bus0Storage[sizeof(MCP2515)];
    MCP2515* canBus0 = nullptr;
    MCP2515& makeBus0() {
        if (!canBus0) canBus0 = new (bus0Storage) MCP2515(CAN_CS_0);
        return *canBus0;
    }
    #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
    alignas(MCP2515) static uint8_t bus1Storage[sizeof(MCP2515)];
    MCP2515* canBus1 = nullptr;
    MCP2515& makeBus1() {
        if (!canBus1) canBus1 = new (bus1Storage) MCP2515(CAN_CS_1);
        return *canBus1;
    }
    #endif
    bool bus0Initialized = false;
    bool bus1Initialized = false;
}
#endif // HAL_CAN_MCP2515_H

}} // namespace hal::can

#endif // INCLUDE_CAN
