/*
 * bus_manager.h - Multi-Bus Initialization Manager
 *
 * Manages initialization of I2C, SPI, and CAN buses based on SystemConfig.
 * Provides platform-abstracted access to multiple bus instances (Wire/Wire1/Wire2, etc.).
 *
 * Key Features:
 * - Platform-specific initialization (Teensy 4.x, 3.6, ESP32, Mega)
 * - Pin conflict validation before bus initialization
 * - Multiple bus instance management (up to 3 of each type)
 * - Accessor functions for sensors to get correct bus instance
 *
 * Usage:
 *   1. Call initConfiguredBuses() during setup()
 *   2. Sensors use getI2CInstance(bus_id) to get correct Wire object
 *   3. SPI devices use getSPIInstance(bus_id) to get correct SPI object
 */

#ifndef BUS_MANAGER_H
#define BUS_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "bus_config.h"

// Forward declarations for platform-specific bus classes
class TwoWire;
class SPIClass;

// ============================================================================
// BUS INITIALIZATION
// ============================================================================

/**
 * Initialize all configured buses
 *
 * Called once during setup(). Reads SystemConfig.buses and initializes
 * enabled I2C, SPI, and CAN buses with configured or default pins.
 *
 * Validation:
 * - Checks pin conflicts using pin registry
 * - Validates platform supports requested bus count
 * - Falls back to defaults on invalid configuration
 */
void initConfiguredBuses();

/**
 * Initialize a specific I2C bus
 *
 * @param bus_id Bus number (0-2 for Wire/Wire1/Wire2)
 * @param config Pointer to I2CBusConfig structure
 * @return true if successfully initialized, false on error
 *
 * Platform notes:
 * - Teensy 4.x: Fixed hardware pins, config pins ignored
 * - Teensy 3.x: Wire.setSDA/setSCL supported
 * - ESP32: Wire.begin(sda, scl) supported
 */
bool initI2CBus(uint8_t bus_id, I2CBusConfig* config);

/**
 * Initialize a specific SPI bus
 *
 * @param bus_id Bus number (0-2 for SPI/SPI1/SPI2)
 * @param config Pointer to SPIBusConfig structure
 * @return true if successfully initialized, false on error
 *
 * Platform notes:
 * - Teensy: Multiple SPI instances available (SPI, SPI1, SPI2)
 * - ESP32: Custom pins supported via SPI.begin(sck, miso, mosi)
 */
bool initSPIBus(uint8_t bus_id, SPIBusConfig* config);

/**
 * Initialize a specific CAN bus
 *
 * @param bus_id Bus number (0-2 for CAN1/CAN2/CAN3)
 * @param config Pointer to CANBusConfig structure
 * @return true if successfully initialized, false on error
 *
 * Platform notes:
 * - Teensy 4.x: FlexCAN native support (CAN1, CAN2, CAN3)
 * - Teensy 3.6: FlexCAN native support (CAN1, CAN2)
 * - Other: MCP2515 external CAN via SPI
 */
bool initCANBus(uint8_t bus_id, CANBusConfig* config);

// ============================================================================
// BUS INSTANCE ACCESS
// ============================================================================

/**
 * Get I2C bus instance for sensor communication
 *
 * @param bus_id Bus number (0-2)
 * @return Pointer to TwoWire object, or nullptr if bus not initialized
 *
 * Example:
 *   TwoWire* wire = getI2CInstance(1);  // Get Wire1
 *   if (wire) {
 *       bme.begin(0x76, wire);
 *   }
 */
TwoWire* getI2CInstance(uint8_t bus_id);

/**
 * Get SPI bus instance for sensor communication
 *
 * @param bus_id Bus number (0-2)
 * @return Pointer to SPIClass object, or nullptr if bus not initialized
 *
 * Example:
 *   SPIClass* spi = getSPIInstance(1);  // Get SPI1
 *   if (spi) {
 *       spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
 *   }
 */
SPIClass* getSPIInstance(uint8_t bus_id);

// ============================================================================
// BUS INFORMATION
// ============================================================================

/**
 * Check if I2C bus is initialized and ready
 * @param bus_id Bus number (0-2)
 * @return true if bus is initialized
 */
bool isI2CBusReady(uint8_t bus_id);

/**
 * Check if SPI bus is initialized and ready
 * @param bus_id Bus number (0-2)
 * @return true if bus is initialized
 */
bool isSPIBusReady(uint8_t bus_id);

/**
 * Check if CAN bus is initialized and ready
 * @param bus_id Bus number (0-2)
 * @return true if bus is initialized
 */
bool isCANBusReady(uint8_t bus_id);

/**
 * Get human-readable name for I2C bus
 * @param bus_id Bus number (0-2)
 * @return String like "Wire", "Wire1", "Wire2"
 */
const char* getI2CBusName(uint8_t bus_id);

/**
 * Get human-readable name for SPI bus
 * @param bus_id Bus number (0-2)
 * @return String like "SPI", "SPI1", "SPI2"
 */
const char* getSPIBusName(uint8_t bus_id);

/**
 * Get human-readable name for CAN bus
 * @param bus_id Bus number (0-2)
 * @return String like "CAN1", "CAN2", "CAN3"
 */
const char* getCANBusName(uint8_t bus_id);

#endif // BUS_MANAGER_H
