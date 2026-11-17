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
#include "../../config.h"  // CAN_CS_x, CAN_INT_x come from wiring.ini (#658)

namespace hal { namespace can {

namespace detail {
    // One driver object and one "started" flag per controller for the whole
    // image, defined in can_mcp2515.cpp. As `static`s here every including
    // file had its own copy (see there).
    // nullptr until begin() builds it (makeBusN(), can_mcp2515.cpp); every
    // other use sits behind busNInitialized, which only begin() sets.
    extern MCP2515* canBus0;
    MCP2515& makeBus0();

    #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
        // Nothing claims CAN_CS_1 (registerSystemPins() claims CAN_CS_0
        // only), so a second MCP2515 would drive an unheld pin (§12.5).
        #error "CAN_CS_1: add its claim in registerSystemPins() and the check in initCANBus() first (#646)"
        extern MCP2515* canBus1;
        MCP2515& makeBus1();
        static constexpr bool hasBus1 = true;
    #else
        static constexpr bool hasBus1 = false;
    #endif

    // Track initialization state
    extern bool bus0Initialized;
    extern bool bus1Initialized;

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

    // Down until this call succeeds: a failed restart must not leave read()
    // and write() talking to a chip reset() left in config mode
    if (bus == 0) detail::bus0Initialized = false;
    #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
    if (bus == 1) detail::bus1Initialized = false;
    #endif

    switch (bus) {
        case 0:
        {
            MCP2515& m = detail::makeBus0();
            m.reset();
            if (m.setBitrate(speed, clock) != MCP2515::ERROR_OK) {
                return false;
            }
            if (listenOnly) {
                if (m.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
            } else {
                if (m.setNormalMode() != MCP2515::ERROR_OK) return false;
            }
        }
            detail::bus0Initialized = true;
            return true;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
            {
                MCP2515& m = detail::makeBus1();
                m.reset();
                if (m.setBitrate(speed, clock) != MCP2515::ERROR_OK) {
                    return false;
                }
                if (listenOnly) {
                    if (m.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
                } else {
                    if (m.setNormalMode() != MCP2515::ERROR_OK) return false;
                }
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

// A new rate or mode on a running bus: reset() plus the setup, as begin()
inline bool restart(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false) {
    return begin(baudrate, bus, listenOnly);
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0) {
    struct can_frame frame;
    frame.can_id = id;
    if (extended) {
        frame.can_id |= CAN_EFF_FLAG;
    }
    if (len > 8) len = 8;
    frame.can_dlc = len;
    memcpy(frame.data, data, len);

    switch (bus) {
        case 0:
            if (!detail::bus0Initialized) return false;
            return detail::canBus0->sendMessage(&frame) == MCP2515::ERROR_OK;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
                if (!detail::bus1Initialized) return false;
                return detail::canBus1->sendMessage(&frame) == MCP2515::ERROR_OK;
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
            result = detail::canBus0->readMessage(&frame);
            break;

        case 1:
            #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
                if (!detail::bus1Initialized) return false;
                result = detail::canBus1->readMessage(&frame);
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

namespace detail {
    // The library keeps register access private: clear TXREQ in TXBnCTRL
    // with a BIT MODIFY of our own, at the library's SPI settings
    inline void clearTxRequests(uint8_t cs) {
        for (uint8_t ctrl = 0x30; ctrl <= 0x50; ctrl += 0x10) {   // TXB0-2CTRL
            SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
            digitalWrite(cs, LOW);
            SPI.transfer(0x05);          // BIT MODIFY
            SPI.transfer(ctrl);
            SPI.transfer(0x08);          // mask: TXREQ
            SPI.transfer(0x00);
            digitalWrite(cs, HIGH);
            SPI.endTransaction();
        }
    }

    inline bool abortUnacked(MCP2515& m, uint8_t cs) {
        if (!(m.getErrorFlags() & (MCP2515::EFLG_TXEP | MCP2515::EFLG_TXBO))) return false;
        // A frame mid-transmission finishes or fails, then isn't retried
        clearTxRequests(cs);
        return true;
    }
}

/**
 * If the controller is error-passive or bus-off (nothing has ACKed its
 * frames: the ECU is unpowered), stop retransmitting what is still
 * waiting to go out (#506). A no-op on a healthy bus.
 */
inline bool abortUnackedTx(uint8_t bus = 0) {
    switch (bus) {
        case 0:
            return detail::bus0Initialized && detail::abortUnacked(*detail::canBus0, CAN_CS_0);
        #if defined(CAN_CS_1) && (CAN_CS_1 != 0xFF)
        case 1:
            return detail::bus1Initialized && detail::abortUnacked(*detail::canBus1, CAN_CS_1);
        #endif
        default:
            return false;
    }
}

}} // namespace hal::can

#endif // HAL_CAN_MCP2515_H
