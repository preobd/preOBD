/*
 * bus_config.h - I2C, SPI, and CAN Bus Configuration
 *
 * Manages runtime configuration for multiple I2C, SPI, and CAN buses.
 * Stored in EEPROM as part of SystemConfig for persistence across reboots.
 *
 * Key Features:
 * - Platform-aware bus availability (Teensy 4.x has 3x I2C/SPI/CAN)
 * - Pin conflict detection prevents invalid configurations
 * - Defaults to platform-specific hardware pins (0xFF = use default)
 * - Runtime enable/disable of individual buses
 */

#ifndef BUS_CONFIG_H
#define BUS_CONFIG_H

#include <Arduino.h>

// ============================================================================
// I2C BUS CONFIGURATION
// ============================================================================

/**
 * I2C Bus Configuration Structure
 *
 * Stores configuration for a single I2C bus (Wire, Wire1, or Wire2).
 * Total size: 8 bytes per bus
 */
struct I2CBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint8_t sda_pin;        // SDA pin number (0xFF = use platform default)
    uint8_t scl_pin;        // SCL pin number (0xFF = use platform default)
    uint16_t clock_speed;   // I2C clock speed in kHz (100, 400, 1000)
    uint8_t reserved[4];    // Future expansion (padding to 8 bytes)
};  // 8 bytes

// ============================================================================
// SPI BUS CONFIGURATION
// ============================================================================

/**
 * SPI Bus Configuration Structure
 *
 * Stores configuration for a single SPI bus (SPI, SPI1, or SPI2).
 * Total size: 12 bytes per bus
 */
struct SPIBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint8_t mosi_pin;       // MOSI pin number (0xFF = use platform default)
    uint8_t miso_pin;       // MISO pin number (0xFF = use platform default)
    uint8_t sck_pin;        // SCK pin number (0xFF = use platform default)
    uint32_t clock_speed;   // SPI clock speed in Hz (e.g., 4000000 = 4MHz)
    uint8_t reserved[4];    // Future expansion (padding to 12 bytes)
};  // 12 bytes

// ============================================================================
// CAN BUS CONFIGURATION
// ============================================================================

/**
 * CAN Bus Configuration Structure
 *
 * Stores configuration for a single CAN bus (CAN1, CAN2, or CAN3).
 * Supports both native FlexCAN (Teensy) and external MCP2515 (SPI-based).
 * Total size: 12 bytes per bus
 */
struct CANBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint8_t tx_pin;         // TX pin number (0xFF = use platform default)
    uint8_t rx_pin;         // RX pin number (0xFF = use platform default)
    uint8_t use_external;   // 0=FlexCAN (Teensy native), 1=MCP2515 (external)
    uint32_t baudrate;      // CAN baudrate (125000, 250000, 500000, 1000000)
    uint8_t cs_pin;         // MCP2515 chip select pin (only if use_external=1)
    uint8_t int_pin;        // MCP2515 interrupt pin (only if use_external=1)
    uint8_t reserved[2];    // Future expansion (padding to 12 bytes)
};  // 12 bytes

// ============================================================================
// GLOBAL BUS CONFIGURATION
// ============================================================================

/**
 * Global Bus Configuration Structure
 *
 * Contains configuration for all I2C, SPI, and CAN buses.
 * This structure is embedded in SystemConfig and persisted to EEPROM.
 *
 * Total size: 104 bytes
 * - I2C: 3 buses × 8 bytes = 24 bytes
 * - SPI: 3 buses × 12 bytes = 36 bytes
 * - CAN: 3 buses × 12 bytes = 36 bytes
 * - Masks + reserved: 8 bytes
 */
struct BusConfig {
    // I2C Bus Configuration (24 bytes)
    I2CBusConfig i2c[3];    // Wire (bus 0), Wire1 (bus 1), Wire2 (bus 2)

    // SPI Bus Configuration (36 bytes)
    SPIBusConfig spi[3];    // SPI (bus 0), SPI1 (bus 1), SPI2 (bus 2)

    // CAN Bus Configuration (36 bytes)
    CANBusConfig can[3];    // CAN1 (bus 0), CAN2 (bus 1), CAN3 (bus 2)

    // Active Bus Bitmasks (3 bytes)
    // These provide quick lookup for which buses are enabled
    // Bit 0 = bus 0, Bit 1 = bus 1, Bit 2 = bus 2
    uint8_t active_i2c_mask;  // Bitmask for active I2C buses (e.g., 0b00000011 = Wire and Wire1)
    uint8_t active_spi_mask;  // Bitmask for active SPI buses
    uint8_t active_can_mask;  // Bitmask for active CAN buses

    // Reserved for future expansion (5 bytes)
    uint8_t reserved[5];      // Padding to maintain 8-byte alignment
};  // Total: 24 + 36 + 36 + 3 + 5 = 104 bytes

// ============================================================================
// HELPER MACROS
// ============================================================================

// Check if a specific bus is enabled using bitmask
#define IS_I2C_BUS_ACTIVE(config, bus_id) ((config)->active_i2c_mask & (1 << (bus_id)))
#define IS_SPI_BUS_ACTIVE(config, bus_id) ((config)->active_spi_mask & (1 << (bus_id)))
#define IS_CAN_BUS_ACTIVE(config, bus_id) ((config)->active_can_mask & (1 << (bus_id)))

// Set/clear bus active bit
#define SET_I2C_BUS_ACTIVE(config, bus_id) ((config)->active_i2c_mask |= (1 << (bus_id)))
#define SET_SPI_BUS_ACTIVE(config, bus_id) ((config)->active_spi_mask |= (1 << (bus_id)))
#define SET_CAN_BUS_ACTIVE(config, bus_id) ((config)->active_can_mask |= (1 << (bus_id)))

#define CLEAR_I2C_BUS_ACTIVE(config, bus_id) ((config)->active_i2c_mask &= ~(1 << (bus_id)))
#define CLEAR_SPI_BUS_ACTIVE(config, bus_id) ((config)->active_spi_mask &= ~(1 << (bus_id)))
#define CLEAR_CAN_BUS_ACTIVE(config, bus_id) ((config)->active_can_mask &= ~(1 << (bus_id)))

// Default pin marker (indicates "use platform default")
#define BUS_PIN_DEFAULT 0xFF

#endif // BUS_CONFIG_H
