/*
 * bus_config.h - Bus Configuration Structure
 *
 * Simple "pick one" model for I2C, SPI, and CAN buses.
 * Select which bus to use for each type and set its speed.
 * All sensors/outputs of a given type use the selected bus.
 *
 * Also configures hardware serial ports (Serial1-Serial8) for TRANSPORT use.
 *
 * Total size: 16 bytes for BusConfig + 16 bytes for SerialPortConfig
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

/**
 * Serial Port Baud Rate Index
 *
 * Maps index values to standard baud rates.
 * Used by SerialPortConfig.baudrate_index[].
 */
enum SerialBaudIndex {
    BAUD_9600   = 0,   //   9600 bps
    BAUD_19200  = 1,   //  19200 bps
    BAUD_38400  = 2,   //  38400 bps
    BAUD_57600  = 3,   //  57600 bps
    BAUD_115200 = 4,   // 115200 bps (default)
    BAUD_230400 = 5,   // 230400 bps
    BAUD_460800 = 6,   // 460800 bps
    BAUD_921600 = 7,   // 921600 bps
    NUM_BAUD_RATES = 8
};

/**
 * Serial Port Configuration Structure
 *
 * Configures which hardware serial ports (Serial1-Serial8) are enabled
 * and their baud rates. Multiple ports can be enabled simultaneously
 * since TRANSPORT can assign different ports to different planes.
 *
 * Total size: 16 bytes
 */
struct SerialPortConfig {
    // Enabled port bitmask (1 byte)
    // Bit 0 = Serial1, Bit 1 = Serial2, ... Bit 7 = Serial8
    uint8_t enabled_mask;

    // Baud rate index for each port (8 bytes)
    // Index 0 = Serial1, Index 1 = Serial2, etc.
    // Values are SerialBaudIndex enum (0-7)
    uint8_t baudrate_index[8];

    // Reserved for future expansion (7 bytes)
    uint8_t reserved[7];
};  // 16 bytes total

#endif // BUS_CONFIG_H
