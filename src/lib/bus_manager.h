/*
 * bus_manager.h - Bus Initialization Manager
 *
 * Manages initialization of the active I2C, SPI, and CAN buses.
 * Uses the simplified "pick one" model - each bus type has one active instance.
 *
 * Key Features:
 * - Platform-specific initialization (Teensy 4.x, 3.6, ESP32, Mega)
 * - Pin conflict validation before bus initialization
 * - Simple accessor functions for the active bus instance
 *
 * Usage:
 *   1. Call initConfiguredBuses() during setup()
 *   2. Sensors use getActiveI2C() to get the active Wire object
 *   3. SPI devices use getActiveSPI() to get the active SPI object
 */

#ifndef BUS_MANAGER_H
#define BUS_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "bus_config.h"
#include "bus_defaults.h"

// Forward declarations
class TwoWire;
class SPIClass;

// ============================================================================
// BUS INITIALIZATION
// ============================================================================

/**
 * Initialize all configured buses
 *
 * Called once during setup(). Reads SystemConfig.buses and initializes
 * the active I2C, SPI, and CAN bus with configured speeds.
 */
void initConfiguredBuses();

/**
 * Initialize a specific I2C bus
 *
 * @param bus_id Bus number (0-2 for Wire/Wire1/Wire2)
 * @param clock_khz I2C clock speed in kHz (100, 400, 1000)
 * @return true if successfully initialized, false on error
 */
bool initI2CBus(uint8_t bus_id, uint16_t clock_khz);

/**
 * Initialize a specific SPI bus
 *
 * @param bus_id Bus number (0-2 for SPI/SPI1/SPI2)
 * @param clock_hz SPI clock speed in Hz
 * @return true if successfully initialized, false on error
 */
bool initSPIBus(uint8_t bus_id, uint32_t clock_hz);

/**
 * Initialize a specific CAN bus
 *
 * @param bus_id Bus number (0-2 for CAN1/CAN2/CAN3)
 * @param baudrate CAN baudrate in bps
 * @return true if successfully initialized, false on error
 */
bool initCANBus(uint8_t bus_id, uint32_t baudrate);

// ============================================================================
// ACTIVE BUS ACCESS
// ============================================================================

/**
 * Get the currently active I2C bus
 * @return Pointer to the active TwoWire object, or nullptr if no bus is up
 *
 * Pure accessor — never brings a bus up. Use acquireI2C() if you need one.
 */
TwoWire* getActiveI2C();

/**
 * Acquire the I2C bus on behalf of a client that needs it (LCD, I2C sensors).
 *
 * Brings the bus up on first use and registers SDA/SCL as PIN_RESERVED. Pin
 * reservation is a property of the bus being *up*, so bring-up must happen here
 * and nowhere else: a client that calls Wire.begin() itself leaves the peripheral
 * live while the registry still shows SDA/SCL free, which is how an input could be
 * assigned to SCL and hang the bus (#351).
 *
 * Honors an explicit `BUS I2C <n>` selection; falls back to DEFAULT_I2C_BUS when
 * none has been made, so a client still gets a working bus on a stock config.
 *
 * @return the active TwoWire, or nullptr if no bus could be brought up
 */
TwoWire* acquireI2C();

/**
 * Get the currently active SPI bus
 * @return Pointer to the active SPIClass object, or nullptr if no bus is up
 *
 * Pure accessor — never brings a bus up. Use acquireSPI() if you need one.
 */
SPIClass* getActiveSPI();

/**
 * Acquire the SPI bus on behalf of a client that needs it (thermocouples, ...).
 *
 * The I2C contract, for SPI: brings the bus up on first use, reserving
 * MOSI/MISO/SCK, and honors an explicit `BUS SPI <n>` selection, falling back to
 * DEFAULT_SPI_BUS when none has been made.
 *
 * @return the active SPIClass, or nullptr if no bus could be brought up
 */
SPIClass* acquireSPI();

/**
 * Bring up every bus the stored configuration needs, in one pass.
 *
 * Called ONCE at boot with the OR of every voter's needs (stored input records
 * and the display — see bus_needs.h), BEFORE any client initializes: before
 * initLCD() and before loadInputConfig() validates a single stored pin. Bus
 * ownership therefore exists before anything can be assigned over it, and no
 * client's init order can change the outcome (#358).
 *
 * A bus that cannot be acquired is reported and skipped; boot always continues.
 */
void acquireRequiredBuses(uint8_t needs);

/**
 * Get the currently active I2C bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveI2CId();

/**
 * Get the currently active SPI bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveSPIId();

/**
 * Get the currently active CAN bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveCANId();

// ============================================================================
// BUS INFORMATION
// ============================================================================

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

// ============================================================================
// BUS STATUS DISPLAY
// ============================================================================

/**
 * Display I2C bus configuration status
 */
void displayI2CStatus();

/**
 * Display SPI bus configuration status
 */
void displaySPIStatus();

/**
 * Display CAN bus configuration status
 */
void displayCANStatus();

#endif // BUS_MANAGER_H
