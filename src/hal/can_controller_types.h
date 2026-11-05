/*
 * can_controller_types.h - CAN Controller Type Definitions
 * Part of the preOBD Hardware Abstraction Layer
 *
 * Defines the CAN controller type enum and compile-time bus-to-controller mapping.
 * Used by the hybrid dispatcher to route CAN operations to the correct driver.
 *
 * Usage:
 *   #include "can_controller_types.h"
 *   CanControllerType ctrl = getBusControllerType(bus);
 *   if (ctrl == CanControllerType::TWAI) { ... }
 *
 * Hybrid mode is declared in boards/<board>/board.h (resolve.h derives
 * INCLUDE_CAN_HYBRID), e.g. boards/esp32s3_hybrid/board.h:
 *   #define SUPPORTS_CAN_HYBRID 1
 *   #define CAN_BUS_0_TYPE CanControllerType::TWAI
 *   #define CAN_BUS_1_TYPE CanControllerType::MCP2515
 */

#ifndef CAN_CONTROLLER_TYPES_H
#define CAN_CONTROLLER_TYPES_H

#include <stdint.h>

// ============================================================================
// CAN Controller Type Enum
// ============================================================================

// Supported CAN controller types
enum class CanControllerType : uint8_t {
    NONE = 0,       // No controller / disabled
    FLEXCAN = 1,    // Teensy native FlexCAN
    TWAI = 2,       // ESP32 native TWAI
    MCP2515 = 3,    // External SPI (MCP2515, MCP25625)
    BXCAN = 4,      // STM32 native bxCAN (future)
    SJA1000 = 5     // External SPI SJA1000 (future)
};

// ============================================================================
// Compile-Time Bus-to-Controller Mapping
// ============================================================================

// Bus 0 controller type (always defined)
#if defined(CAN_BUS_0_TYPE)
    // Explicit override via build flag
    #define CAN_CONTROLLER_BUS_0 CAN_BUS_0_TYPE
#elif defined(USE_FLEXCAN_NATIVE) && (defined(__MK20DX256__) || defined(__MK64FX512__) || \
      defined(__MK66FX1M0__) || defined(__IMXRT1062__))
    // Teensy with FlexCAN
    #define CAN_CONTROLLER_BUS_0 CanControllerType::FLEXCAN
#elif defined(ESP32)
    // ESP32 with TWAI
    #define CAN_CONTROLLER_BUS_0 CanControllerType::TWAI
#elif defined(STM32F4xx) || defined(STM32F1xx)
    // STM32 with bxCAN (STM32F4 driver: hal/platforms/can_bxcan.cpp)
    #define CAN_CONTROLLER_BUS_0 CanControllerType::BXCAN
#else
    // Default: External SPI CAN controller
    #define CAN_CONTROLLER_BUS_0 CanControllerType::MCP2515
#endif

// Bus 1 controller type (optional)
#if defined(CAN_BUS_1_TYPE)
    // Explicit override via build flag
    #define CAN_CONTROLLER_BUS_1 CAN_BUS_1_TYPE
#elif defined(USE_FLEXCAN_NATIVE) && defined(NUM_CAN_BUSES) && (NUM_CAN_BUSES >= 2)
    // Teensy with dual+ FlexCAN
    #define CAN_CONTROLLER_BUS_1 CanControllerType::FLEXCAN
#elif defined(STM32F407xx) || defined(STM32F429xx)
    // STM32F4 with dual bxCAN (future)
    #define CAN_CONTROLLER_BUS_1 CanControllerType::BXCAN
#else
    // No second bus by default
    #define CAN_CONTROLLER_BUS_1 CanControllerType::NONE
#endif

// Bus 2 controller type (optional)
#if defined(CAN_BUS_2_TYPE)
    #define CAN_CONTROLLER_BUS_2 CAN_BUS_2_TYPE
#elif defined(USE_FLEXCAN_NATIVE) && defined(NUM_CAN_BUSES) && (NUM_CAN_BUSES >= 3)
    // Teensy 4.1 with 3 FlexCAN buses
    #define CAN_CONTROLLER_BUS_2 CanControllerType::FLEXCAN
#else
    #define CAN_CONTROLLER_BUS_2 CanControllerType::NONE
#endif

// Bus 3 controller type (optional, hybrid mode only)
#if defined(CAN_BUS_3_TYPE)
    #define CAN_CONTROLLER_BUS_3 CAN_BUS_3_TYPE
#else
    #define CAN_CONTROLLER_BUS_3 CanControllerType::NONE
#endif

// ============================================================================
// Helper Functions
// ============================================================================

// Get the controller type for a given bus (compile-time).
// Written as a single return expression for C++11 constexpr compatibility
// (ESP32 Arduino framework defaults to gnu++11).
inline constexpr CanControllerType getBusControllerType(uint8_t bus) {
    return (bus == 0) ? CAN_CONTROLLER_BUS_0 :
           (bus == 1) ? CAN_CONTROLLER_BUS_1 :
           (bus == 2) ? CAN_CONTROLLER_BUS_2 :
           (bus == 3) ? CAN_CONTROLLER_BUS_3 :
                        CanControllerType::NONE;
}

// Check if a bus has a controller assigned
inline constexpr bool hasBusController(uint8_t bus) {
    return getBusControllerType(bus) != CanControllerType::NONE;
}

namespace can_ctrl_detail {
    // Buses below `bus` (from `i` up) whose controller is `type`
    inline constexpr uint8_t sameTypeBefore(uint8_t bus, CanControllerType type, uint8_t i) {
        return (i >= bus) ? 0 :
               (uint8_t)((getBusControllerType(i) == type ? 1 : 0) + sameTypeBefore(bus, type, i + 1));
    }
}

// Which controller of its type a bus is: 0 for the first MCP2515, 1 for the
// second, whatever bus numbers they sit on. A driver that owns several chips
// indexes them by this, not by the bus number (#556: hybrid boards put their
// one MCP2515 on bus 1 or 3, and the driver only knew chips 0 and 1).
inline constexpr uint8_t controllerIndexForBus(uint8_t bus) {
    return can_ctrl_detail::sameTypeBefore(bus, getBusControllerType(bus), 0);
}

#endif // CAN_CONTROLLER_TYPES_H
