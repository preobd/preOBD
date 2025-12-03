/*
 * version.h - Firmware and EEPROM version constants
 * Single source of truth for all version-related constants
 */

#ifndef VERSION_H
#define VERSION_H

// Firmware version string (semantic versioning)
#define FIRMWARE_VERSION "v0.4.0-alpha"

// EEPROM configuration version
// Increment when Input struct layout changes (forces reconfiguration)
#define EEPROM_VERSION 1

#endif
