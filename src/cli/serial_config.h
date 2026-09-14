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
// Begin JSON import streaming mode (returns false if already active).
// target == 0 selects sentinel mode (paste JSON, then 'END JSON' on its own
// line) — the interactive-human path. target > 0 selects byte-counted mode
// (stream exactly `target` raw bytes, no sentinel) — the programmatic-sender
// path (mobile app profiles, scripts), which already knows its payload length.
bool serialConfig_beginJsonImport(size_t target = 0);
bool serialConfig_isJsonImportActive();
// Aborts + frees an import that's received no bytes in ~30-60s (sentinel mode
// only realistically reachable — a byte-counted sender streams its whole
// payload in one continuous write). Call unconditionally every loop tick.
void serialConfig_checkJsonImportIdle();
#endif

#endif // SERIAL_CONFIG_H
