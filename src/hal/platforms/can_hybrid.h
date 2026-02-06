/*
 * can_hybrid.h - Hybrid CAN Controller Dispatcher
 * Part of the preOBD Hardware Abstraction Layer
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
// Controller Detection Helper Macros
// ============================================================================
// These macros simplify the conditional compilation logic for including drivers
// Note: We check the actual controller assignments rather than using enum comparisons
// since preprocessor directives can't evaluate C++ enum class values

// Check if any bus uses FlexCAN
#if defined(USE_FLEXCAN_NATIVE)
    #define HYBRID_HAS_FLEXCAN 1
#else
    #define HYBRID_HAS_FLEXCAN 0
#endif

// Check if any bus uses TWAI (ESP32 native CAN)
#if defined(ESP32)
    #define HYBRID_HAS_TWAI 1
#else
    #define HYBRID_HAS_TWAI 0
#endif

// Check if any bus uses MCP2515 (always included for now since we can't check enum at preprocessor time)
// In hybrid mode, at least one bus might be MCP2515 if it's not the native controller
#define HYBRID_HAS_MCP2515 1

// ============================================================================
// Driver Includes
// ============================================================================

// Include all potentially used drivers
// Each driver wraps its functions in a namespace (flexcan, twai, mcp2515)
#if HYBRID_HAS_FLEXCAN
    #include "can_flexcan.h"
#endif

#if HYBRID_HAS_TWAI
    #include "can_twai.h"
#endif

#if HYBRID_HAS_MCP2515
    #include "can_mcp2515.h"
#endif

namespace hal { namespace can {

// ============================================================================
// Hybrid Dispatcher Functions
// ============================================================================

inline bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false) {
    // Validate bus number is within platform limits
    if (bus >= PLATFORM_EFFECTIVE_CAN_BUSES) return false;

    CanControllerType ctrl = getBusControllerType(bus);

    switch (ctrl) {
        #if HYBRID_HAS_FLEXCAN
        case CanControllerType::FLEXCAN:
            return flexcan::begin(baudrate, bus, listenOnly);
        #endif

        #if HYBRID_HAS_TWAI
        case CanControllerType::TWAI:
            // TWAI only supports bus 0
            return (bus == 0) ? twai::begin(baudrate, 0, listenOnly) : false;
        #endif

        #if HYBRID_HAS_MCP2515
        case CanControllerType::MCP2515:
            return mcp2515::begin(baudrate, bus, listenOnly);
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
        #if HYBRID_HAS_FLEXCAN
        case CanControllerType::FLEXCAN:
            return flexcan::write(id, data, len, extended, bus);
        #endif

        #if HYBRID_HAS_TWAI
        case CanControllerType::TWAI:
            return (bus == 0) ? twai::write(id, data, len, extended, 0) : false;
        #endif

        #if HYBRID_HAS_MCP2515
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
        #if HYBRID_HAS_FLEXCAN
        case CanControllerType::FLEXCAN:
            return flexcan::read(id, data, len, extended, bus);
        #endif

        #if HYBRID_HAS_TWAI
        case CanControllerType::TWAI:
            return (bus == 0) ? twai::read(id, data, len, extended, 0) : false;
        #endif

        #if HYBRID_HAS_MCP2515
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
        #if HYBRID_HAS_FLEXCAN
        case CanControllerType::FLEXCAN:
            flexcan::setFilters(filter1, filter2, bus);
            break;
        #endif

        #if HYBRID_HAS_TWAI
        case CanControllerType::TWAI:
            if (bus == 0) twai::setFilters(filter1, filter2, 0);
            break;
        #endif

        #if HYBRID_HAS_MCP2515
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
