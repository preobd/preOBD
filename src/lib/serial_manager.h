/*
 * serial_manager.h - Serial Port Manager
 *
 * Manages initialization and configuration of hardware serial ports (Serial1-Serial8).
 * Works with the TRANSPORT command for routing messages to different planes.
 *
 * Key Features:
 * - Platform-specific serial port support (Teensy 4.x supports up to 8 ports)
 * - Baud rate configuration via lookup table
 * - Pin conflict validation before port initialization
 * - Integration with TransportInterface for message routing
 *
 * Usage:
 *   1. Call initConfiguredSerialPorts() during setup()
 *   2. Use getSerialPort(port_id) to get the Stream* for a port
 *   3. TRANSPORT command assigns ports to message planes
 */

#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <Arduino.h>
#include "bus_config.h"
#include "bus_defaults.h"

// ============================================================================
// BAUD RATE LOOKUP
// ============================================================================

/**
 * Convert baud rate index to actual baud rate
 * @param index SerialBaudIndex value (0-7)
 * @return Actual baud rate in bps
 */
uint32_t getBaudRateFromIndex(uint8_t index);

/**
 * Convert actual baud rate to index
 * @param baudrate Baud rate in bps
 * @return SerialBaudIndex value, or BAUD_115200 if not found
 */
uint8_t getBaudRateIndex(uint32_t baudrate);

/**
 * Get human-readable baud rate string
 * @param index SerialBaudIndex value (0-7)
 * @return String like "115200"
 */
const char* getBaudRateString(uint8_t index);

// ============================================================================
// SERIAL PORT INITIALIZATION
// ============================================================================

/**
 * Initialize all enabled serial ports
 *
 * Called once during setup(). Reads SystemConfig.serial and initializes
 * all enabled serial ports with their configured baud rates.
 */
void initConfiguredSerialPorts();

/**
 * Initialize a specific serial port
 *
 * @param port_id Port number (1-8 for Serial1-Serial8)
 * @param baudrate Baud rate in bps
 * @return true if successfully initialized, false on error
 */
bool initSerialPort(uint8_t port_id, uint32_t baudrate);

/**
 * Enable a serial port in config and initialize it
 *
 * @param port_id Port number (1-8)
 * @param baud_index SerialBaudIndex value (0-7)
 * @return true if successfully enabled
 */
bool enableSerialPort(uint8_t port_id, uint8_t baud_index);

/**
 * Disable a serial port and release its pins
 *
 * @param port_id Port number (1-8)
 * @return true if successfully disabled
 */
bool disableSerialPort(uint8_t port_id);

// ============================================================================
// SERIAL PORT ACCESS
// ============================================================================

/**
 * Get Stream pointer for a serial port
 *
 * @param port_id Port number (1-8)
 * @return Pointer to Stream (Serial1, Serial2, etc.), or nullptr if not available
 */
Stream* getSerialPort(uint8_t port_id);

/**
 * Check if a serial port is currently enabled
 *
 * @param port_id Port number (1-8)
 * @return true if enabled
 */
bool isSerialPortActive(uint8_t port_id);

// ============================================================================
// SERIAL PORT STATUS
// ============================================================================

/**
 * Display serial port configuration status
 *
 * Shows all available serial ports, which are enabled,
 * their baud rates, and pin assignments.
 */
void displaySerialStatus();

/**
 * Display status for a specific serial port
 *
 * @param port_id Port number (1-8)
 */
void displaySerialPortStatus(uint8_t port_id);

#endif // SERIAL_MANAGER_H
