/*
 * version.h - Firmware and EEPROM version constants
 * Single source of truth for all version-related constants
 */

#ifndef VERSION_H
#define VERSION_H

// Firmware version string (semantic versioning)
#define FIRMWARE_VERSION "v0.5.0-alpha"

// EEPROM configuration version
// Increment when Input struct layout changes (forces reconfiguration)
// Version 2: Changed from storing enum indices to storing name hashes (Phase 5)
#define EEPROM_VERSION 2

#endif
