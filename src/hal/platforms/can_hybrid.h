/*
 * can_hybrid.h - Hybrid CAN Controller Dispatcher
 * Part of the openEMS Hardware Abstraction Layer
 *
 * Enables mixing different CAN controller types on different buses.
 * Example: ESP32 TWAI on bus 0 + MCP2515 on bus 1 for dual-bus operation.
 *
 * This file is only included when ENABLE_CAN_HYBRID is defined.
 * Routes CAN operations to the appropriate driver based on bus controller type.
 *
 * Configuration via platformio.ini:
 *   -D ENABLE_CAN_HYBRID
 *   -D CAN_BUS_0_TYPE=CanControllerType::TWAI
 *   -D CAN_BUS_1_TYPE=CanControllerType::MCP2515
 */

#ifndef HAL_CAN_HYBRID_H
#define HAL_CAN_HYBRID_H

#include <stdint.h>
#include "../platform_caps.h"
#include "../../lib/can_controller_types.h"

// ============================================================================
// Compile-Time Validation
// ============================================================================

// Hybrid mode requires at least CAN_BUS_0_TYPE to be defined
#if !defined(CAN_BUS_0_TYPE)
    #error "Hybrid mode (ENABLE_CAN_HYBRID) requires CAN_BUS_0_TYPE to be defined in platformio.ini"
#endif

// Warn if hybrid mode is enabled but only one bus is configured
#if !defined(CAN_BUS_1_TYPE) && !defined(CAN_BUS_2_TYPE) && !defined(CAN_BUS_3_TYPE)
    #warning "Hybrid mode enabled but only one bus configured - consider using single-controller mode instead"
#endif

// ============================================================================
// Driver Includes
// ============================================================================

// Include all potentially used drivers
// Each driver wraps its functions in a namespace (flexcan, twai, mcp2515)
#if defined(USE_FLEXCAN_NATIVE) || (CAN_CONTROLLER_BUS_0 == CanControllerType::FLEXCAN) || \
    (CAN_CONTROLLER_BUS_1 == CanControllerType::FLEXCAN) || \
    (CAN_CONTROLLER_BUS_2 == CanControllerType::FLEXCAN) || \
    (CAN_CONTROLLER_BUS_3 == CanControllerType::FLEXCAN)
    #include "can_flexcan.h"
#endif

#if defined(ESP32) || (CAN_CONTROLLER_BUS_0 == CanControllerType::TWAI) || \
    (CAN_CONTROLLER_BUS_1 == CanControllerType::TWAI) || \
    (CAN_CONTROLLER_BUS_2 == CanControllerType::TWAI) || \
    (CAN_CONTROLLER_BUS_3 == CanControllerType::TWAI)
    #include "can_twai.h"
#endif

#if (CAN_CONTROLLER_BUS_0 == CanControllerType::MCP2515) || \
    (CAN_CONTROLLER_BUS_1 == CanControllerType::MCP2515) || \
    (CAN_CONTROLLER_BUS_2 == CanControllerType::MCP2515) || \
    (CAN_CONTROLLER_BUS_3 == CanControllerType::MCP2515)
    #include "can_mcp2515.h"
#endif

namespace hal { namespace can {

// ============================================================================
// Hybrid Dispatcher Functions
// ============================================================================

inline bool begin(uint32_t baudrate, uint8_t bus = 0) {
    // Validate bus number is within platform limits
    if (bus >= PLATFORM_EFFECTIVE_CAN_BUSES) return false;

    CanControllerType ctrl = getBusControllerType(bus);

    switch (ctrl) {
        #if defined(USE_FLEXCAN_NATIVE) || (CAN_CONTROLLER_BUS_0 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::FLEXCAN)
        case CanControllerType::FLEXCAN:
            return flexcan::begin(baudrate, bus);
        #endif

        #if defined(ESP32) || (CAN_CONTROLLER_BUS_0 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::TWAI)
        case CanControllerType::TWAI:
            // TWAI only supports bus 0
            return (bus == 0) ? twai::begin(baudrate, 0) : false;
        #endif

        #if (CAN_CONTROLLER_BUS_0 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::MCP2515)
        case CanControllerType::MCP2515:
            return mcp2515::begin(baudrate, bus);
        #endif

        case CanControllerType::NONE:
        default:
            return false;
    }
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0) {
    // Validate bus number is within platform limits
    if (bus >= PLATFORM_EFFECTIVE_CAN_BUSES) return false;

    CanControllerType ctrl = getBusControllerType(bus);

    switch (ctrl) {
        #if defined(USE_FLEXCAN_NATIVE) || (CAN_CONTROLLER_BUS_0 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::FLEXCAN)
        case CanControllerType::FLEXCAN:
            return flexcan::write(id, data, len, extended, bus);
        #endif

        #if defined(ESP32) || (CAN_CONTROLLER_BUS_0 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::TWAI)
        case CanControllerType::TWAI:
            return (bus == 0) ? twai::write(id, data, len, extended, 0) : false;
        #endif

        #if (CAN_CONTROLLER_BUS_0 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::MCP2515)
        case CanControllerType::MCP2515:
            return mcp2515::write(id, data, len, extended, bus);
        #endif

        case CanControllerType::NONE:
        default:
            return false;
    }
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0) {
    // Validate bus number is within platform limits
    if (bus >= PLATFORM_EFFECTIVE_CAN_BUSES) return false;

    CanControllerType ctrl = getBusControllerType(bus);

    switch (ctrl) {
        #if defined(USE_FLEXCAN_NATIVE) || (CAN_CONTROLLER_BUS_0 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::FLEXCAN)
        case CanControllerType::FLEXCAN:
            return flexcan::read(id, data, len, extended, bus);
        #endif

        #if defined(ESP32) || (CAN_CONTROLLER_BUS_0 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::TWAI)
        case CanControllerType::TWAI:
            return (bus == 0) ? twai::read(id, data, len, extended, 0) : false;
        #endif

        #if (CAN_CONTROLLER_BUS_0 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::MCP2515)
        case CanControllerType::MCP2515:
            return mcp2515::read(id, data, len, extended, bus);
        #endif

        case CanControllerType::NONE:
        default:
            return false;
    }
}

inline void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0) {
    CanControllerType ctrl = getBusControllerType(bus);

    switch (ctrl) {
        #if defined(USE_FLEXCAN_NATIVE) || (CAN_CONTROLLER_BUS_0 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::FLEXCAN) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::FLEXCAN)
        case CanControllerType::FLEXCAN:
            flexcan::setFilters(filter1, filter2, bus);
            break;
        #endif

        #if defined(ESP32) || (CAN_CONTROLLER_BUS_0 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::TWAI) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::TWAI)
        case CanControllerType::TWAI:
            if (bus == 0) twai::setFilters(filter1, filter2, 0);
            break;
        #endif

        #if (CAN_CONTROLLER_BUS_0 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_1 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_2 == CanControllerType::MCP2515) || \
            (CAN_CONTROLLER_BUS_3 == CanControllerType::MCP2515)
        case CanControllerType::MCP2515:
            mcp2515::setFilters(filter1, filter2, bus);
            break;
        #endif

        case CanControllerType::NONE:
        default:
            break;
    }
}

}} // namespace hal::can

#endif // HAL_CAN_HYBRID_H
