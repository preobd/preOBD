/*
 * can_mcp2515.h - MCP2515 SPI CAN controller driver
 * Part of the openEMS Hardware Abstraction Layer
 * Uses arduino-CAN library (Sandeep Mistry)
 */

#ifndef HAL_CAN_MCP2515_H
#define HAL_CAN_MCP2515_H

#include <CAN.h>
#include "../../config.h"  // For CAN_CS, CAN_INT pin definitions

namespace hal { namespace can {

inline bool begin(uint32_t baudrate) {
    CAN.setPins(CAN_CS, CAN_INT);
    return CAN.begin(baudrate);
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended) {
    if (extended) {
        CAN.beginExtendedPacket(id, len);
    } else {
        CAN.beginPacket(id, len);
    }
    CAN.write(data, len);
    return CAN.endPacket();
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended) {
    int packetSize = CAN.parsePacket();
    if (packetSize > 0) {
        id = CAN.packetId();
        extended = CAN.packetExtended();
        len = 0;
        while (CAN.available() && len < 8) {
            data[len++] = CAN.read();
        }
        return true;
    }
    return false;
}

inline void setFilters(uint32_t filter1, uint32_t filter2) {
    // MCP2515 filtering via arduino-CAN library
    // Note: Library doesn't expose filter configuration directly
    // Software filtering recommended for now
    (void)filter1;
    (void)filter2;
}

}} // namespace hal::can

#endif // HAL_CAN_MCP2515_H
