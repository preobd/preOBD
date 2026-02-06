/*
 * can_mcp2515.h - MCP2515 SPI CAN controller driver
 * Part of the preOBD Hardware Abstraction Layer
 * Uses autowp-mcp2515 library (supports multiple instances)
 *
 * Supports up to 2 MCP2515 controllers for dual CAN bus operation:
 * - Bus 0: CAN_CS_0, CAN_INT_0 (primary)
 * - Bus 1: CAN_CS_1, CAN_INT_1 (secondary)
 */

#ifndef HAL_CAN_MCP2515_H
#define HAL_CAN_MCP2515_H

#include <SPI.h>
// MCP2515 library defines DEFAULT_SPI_CLOCK which conflicts with bus_defaults.h
// Temporarily undef it before including the library
#ifdef DEFAULT_SPI_CLOCK
    #undef DEFAULT_SPI_CLOCK
#endif
#include <mcp2515.h>
#include "../../config.h"  // For CAN_CS_x, CAN_INT_x pin definitions

namespace hal { namespace can {

#ifdef ENABLE_CAN_HYBRID
// In hybrid mode, wrap in mcp2515 namespace for dispatcher
namespace mcp2515 {
#endif

namespace detail {
    // Static MCP2515 instances
    static MCP2515 canBus0(CAN_CS_0);

    #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
        static MCP2515 canBus1(CAN_CS_1);
        static constexpr bool hasBus1 = true;
    #else
        static constexpr bool hasBus1 = false;
    #endif

    // Track initialization state
    static bool bus0Initialized = false;
    static bool bus1Initialized = false;

    // Convert baudrate to MCP2515 speed enum
    inline CAN_SPEED baudrateToSpeed(uint32_t baudrate) {
        switch (baudrate) {
            case 1000000: return CAN_1000KBPS;
            case 500000:  return CAN_500KBPS;
            case 250000:  return CAN_250KBPS;
            case 200000:  return CAN_200KBPS;
            case 125000:  return CAN_125KBPS;
            case 100000:  return CAN_100KBPS;
            case 80000:   return CAN_80KBPS;
            case 50000:   return CAN_50KBPS;
            case 40000:   return CAN_40KBPS;
            case 33333:   return CAN_33KBPS;
            case 31250:   return CAN_31K25BPS;
            case 20000:   return CAN_20KBPS;
            case 10000:   return CAN_10KBPS;
            case 5000:    return CAN_5KBPS;
            default:      return CAN_500KBPS;  // Default to 500kbps (OBD-II)
        }
    }

    // Get clock speed - most MCP2515 modules use 8MHz or 16MHz crystal
    // Default to 8MHz (common on cheap modules)
    inline CAN_CLOCK getClockSpeed() {
        #if defined(MCP2515_CLOCK_16MHZ)
            return MCP_16MHZ;
        #else
            return MCP_8MHZ;
        #endif
    }
}

inline bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false) {
    CAN_SPEED speed = detail::baudrateToSpeed(baudrate);
    CAN_CLOCK clock = detail::getClockSpeed();

    switch (bus) {
        case 0:
            detail::canBus0.reset();
            if (detail::canBus0.setBitrate(speed, clock) != MCP2515::ERROR_OK) {
                return false;
            }
            if (listenOnly) {
                if (detail::canBus0.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
            } else {
                if (detail::canBus0.setNormalMode() != MCP2515::ERROR_OK) return false;
            }
            detail::bus0Initialized = true;
            return true;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
                detail::canBus1.reset();
                if (detail::canBus1.setBitrate(speed, clock) != MCP2515::ERROR_OK) {
                    return false;
                }
                if (listenOnly) {
                    if (detail::canBus1.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
                } else {
                    if (detail::canBus1.setNormalMode() != MCP2515::ERROR_OK) return false;
                }
                detail::bus1Initialized = true;
                return true;
            #else
                return false;  // Bus 1 not configured
            #endif

        default:
            return false;  // Invalid bus
    }
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0) {
    struct can_frame frame;
    frame.can_id = id;
    if (extended) {
        frame.can_id |= CAN_EFF_FLAG;
    }
    frame.can_dlc = len;
    memcpy(frame.data, data, len);

    switch (bus) {
        case 0:
            if (!detail::bus0Initialized) return false;
            return detail::canBus0.sendMessage(&frame) == MCP2515::ERROR_OK;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
                if (!detail::bus1Initialized) return false;
                return detail::canBus1.sendMessage(&frame) == MCP2515::ERROR_OK;
            #else
                return false;
            #endif

        default:
            return false;
    }
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0) {
    struct can_frame frame;
    MCP2515::ERROR result;

    switch (bus) {
        case 0:
            if (!detail::bus0Initialized) return false;
            result = detail::canBus0.readMessage(&frame);
            break;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
                if (!detail::bus1Initialized) return false;
                result = detail::canBus1.readMessage(&frame);
                break;
            #else
                return false;
            #endif

        default:
            return false;
    }

    if (result == MCP2515::ERROR_OK) {
        id = frame.can_id & CAN_EFF_MASK;  // Strip flags to get raw ID
        extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        len = frame.can_dlc;
        memcpy(data, frame.data, frame.can_dlc);
        return true;
    }
    return false;
}

inline void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0) {
    // MCP2515 has complex filter configuration
    // For simplicity, we'll accept all frames and do software filtering
    // Full hardware filter support could be added later if needed
    (void)filter1;
    (void)filter2;
    (void)bus;
}

#ifdef ENABLE_CAN_HYBRID
} // namespace mcp2515
#endif

}} // namespace hal::can

#endif // HAL_CAN_MCP2515_H
