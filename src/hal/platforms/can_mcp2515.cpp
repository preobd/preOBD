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

#if INCLUDE_CAN

namespace hal { namespace can {

#ifdef HAL_CAN_MCP2515_H
namespace detail {
    MCP2515 canBus0(CAN_CS_0);
    #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
        MCP2515 canBus1(CAN_CS_1);
    #endif
    bool bus0Initialized = false;
    bool bus1Initialized = false;
}
#endif // HAL_CAN_MCP2515_H

}} // namespace hal::can

#endif // INCLUDE_CAN
