/*
 * serial_config.h - Serial Command Interface
 *
 * Provides serial command interface for runtime configuration of inputs.
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include "../config.h"

#ifndef USE_STATIC_CONFIG

#include <Arduino.h>

// Initialize serial command handler
void initSerialConfig();

// Process serial commands (call from loop)
void processSerialCommands();

// Handle a single command (char* to avoid String class RAM overhead)
void handleSerialCommand(char* cmd);

// Handle incoming character input (called by MessageRouter)
void handleCommandInput(char c);

#else // USE_STATIC_CONFIG defined

// Stub functions for static builds (no runtime configuration)
inline void processSerialCommands() {}
inline void handleCommandInput(char) {}

#endif // USE_STATIC_CONFIG

#endif // SERIAL_CONFIG_H
