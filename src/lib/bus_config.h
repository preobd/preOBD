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
 *
 * Design Note:
 * Pin fields are only meaningful on platforms with remappable pins (ESP32).
 * On Teensy 4.x, pins are hardware-fixed and these fields are ignored.
 * We keep them for ESP32 compatibility but don't expose pin remapping
 * commands initially.
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
 * Total size: 5 bytes per bus
 */
struct I2CBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint16_t clock_speed;   // I2C clock speed in kHz (100, 400, 1000)
    uint8_t sda_pin;        // SDA pin (0xFF = platform default, ESP32 only)
    uint8_t scl_pin;        // SCL pin (0xFF = platform default, ESP32 only)
};  // 5 bytes

// ============================================================================
// SPI BUS CONFIGURATION
// ============================================================================

/**
 * SPI Bus Configuration Structure
 *
 * Stores configuration for a single SPI bus (SPI, SPI1, or SPI2).
 * Total size: 8 bytes per bus
 */
struct SPIBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint32_t clock_speed;   // SPI clock speed in Hz (e.g., 4000000 = 4MHz)
    uint8_t mosi_pin;       // MOSI pin (0xFF = platform default, ESP32 only)
    uint8_t miso_pin;       // MISO pin (0xFF = platform default, ESP32 only)
    uint8_t sck_pin;        // SCK pin (0xFF = platform default, ESP32 only)
};  // 8 bytes

// ============================================================================
// CAN BUS CONFIGURATION
// ============================================================================

/**
 * CAN Bus Configuration Structure
 *
 * Stores configuration for a single CAN bus (CAN1, CAN2, or CAN3).
 * Total size: 7 bytes per bus
 *
 * Note: MCP2515 external CAN support deferred to future enhancement.
 */
struct CANBusConfig {
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint32_t baudrate;      // CAN baudrate (125000, 250000, 500000, 1000000)
    uint8_t tx_pin;         // TX pin (0xFF = platform default, ESP32 only)
    uint8_t rx_pin;         // RX pin (0xFF = platform default, ESP32 only)
};  // 7 bytes

// ============================================================================
// GLOBAL BUS CONFIGURATION
// ============================================================================

/**
 * Global Bus Configuration Structure
 *
 * Contains configuration for all I2C, SPI, and CAN buses.
 * This structure is embedded in SystemConfig and persisted to EEPROM.
 *
 * Total size: 62 bytes
 * - I2C: 3 buses × 5 bytes = 15 bytes
 * - SPI: 3 buses × 8 bytes = 24 bytes
 * - CAN: 3 buses × 7 bytes = 21 bytes
 * - Reserved: 2 bytes (padding for alignment)
 */
struct BusConfig {
    I2CBusConfig i2c[3];    // Wire (bus 0), Wire1 (bus 1), Wire2 (bus 2)
    SPIBusConfig spi[3];    // SPI (bus 0), SPI1 (bus 1), SPI2 (bus 2)
    CANBusConfig can[3];    // CAN1 (bus 0), CAN2 (bus 1), CAN3 (bus 2)
    uint8_t reserved[2];    // Padding for alignment
};  // Total: 15 + 24 + 21 + 2 = 62 bytes

// ============================================================================
// HELPER MACROS
// ============================================================================

// Default pin marker (indicates "use platform default")
#define BUS_PIN_DEFAULT 0xFF

#endif // BUS_CONFIG_H
