/*
 * bus_config.h - Bus Configuration Structure
 *
 * Simple "pick one" model for I2C, SPI, and CAN buses.
 * Select which bus to use for each type and set its speed.
 * All sensors/outputs of a given type use the selected bus.
 *
 * Total size: 16 bytes (stored in EEPROM as part of SystemConfig)
 */

#ifndef BUS_CONFIG_H
#define BUS_CONFIG_H

#include <stdint.h>

/**
 * Bus Configuration Structure
 *
 * For each bus type, stores:
 * - Which bus instance to use (0, 1, or 2)
 * - The speed/baudrate setting
 *
 * Example: active_i2c=1 means all I2C sensors use Wire1
 */
struct BusConfig {
    uint8_t active_i2c;      // 0=Wire, 1=Wire1, 2=Wire2
    uint16_t i2c_clock;      // kHz (100, 400, 1000)

    uint8_t active_spi;      // 0=SPI, 1=SPI1, 2=SPI2
    uint32_t spi_clock;      // Hz (e.g., 4000000 = 4MHz)

    uint8_t active_can;      // 0=CAN1, 1=CAN2, 2=CAN3
    uint32_t can_baudrate;   // bps (125000, 250000, 500000, 1000000)

    uint8_t reserved[2];     // Padding for alignment
};  // 16 bytes

#endif // BUS_CONFIG_H
