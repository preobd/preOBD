/*
 * serial_config.h - Serial Command Interface
 *
 * Provides serial command interface for runtime configuration of inputs.
 */

#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include "../config.h"
#include "../lib/platform.h"

#include <Arduino.h>

// Initialize serial command handler
void initSerialConfig();

// Process serial commands (call from loop)
void processSerialCommands();

// Handle a single command (char* to avoid String class RAM overhead)
void handleSerialCommand(char* cmd);

// Handle incoming character input (called by MessageRouter)
void handleCommandInput(char c);

#if SUPPORTS_JSON_IMPORT_STREAM
// Begin JSON import streaming mode (returns false if already active)
bool serialConfig_beginJsonImport();
bool serialConfig_isJsonImportActive();
#endif

#endif // SERIAL_CONFIG_H
