/*
 * hal_can.h - Hardware Abstraction Layer for CAN Bus
 * Part of the openEMS Hardware Abstraction Layer
 *
 * Provides a unified CAN interface across all supported platforms:
 * - Teensy 3.x/4.x: Native FlexCAN
 * - ESP32: Native TWAI (CAN)
 * - AVR (Uno, Mega): MCP2515 via SPI
 *
 * Usage:
 *   #include "hal/hal_can.h"
 *   hal::can::begin(500000);  // 500 kbps
 *   hal::can::write(0x7E8, data, 8, false);  // Standard frame
 *
 * Note: CAN is only available when ENABLE_CAN is defined
 */

#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <stdint.h>
#include "../config.h"

#ifdef ENABLE_CAN

// Platform detection and include appropriate implementation
#if defined(USE_FLEXCAN_NATIVE) && (defined(__MK20DX256__) || defined(__MK64FX512__) || \
    defined(__MK66FX1M0__) || defined(__IMXRT1062__))
    // Teensy 3.x/4.x with native FlexCAN
    #include "platforms/can_flexcan.h"

#elif defined(ESP32)
    // ESP32 with native TWAI
    #include "platforms/can_twai.h"

#else
    // All other platforms: MCP2515 via SPI
    #include "platforms/can_mcp2515.h"

#endif

#else // !ENABLE_CAN

// Stub implementation when CAN is disabled
namespace hal { namespace can {

inline bool begin(uint32_t baudrate) {
    (void)baudrate;
    return false;
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended) {
    (void)id; (void)data; (void)len; (void)extended;
    return false;
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended) {
    (void)id; (void)data; (void)len; (void)extended;
    return false;
}

inline bool available() {
    return false;
}

inline void setFilters(uint32_t filter1, uint32_t filter2) {
    (void)filter1; (void)filter2;
}

}} // namespace hal::can

#endif // ENABLE_CAN

#endif // HAL_CAN_H
