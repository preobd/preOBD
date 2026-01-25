/*
 * version.h - Firmware version constants
 * Single source of truth for all version-related constants
 *
 * Three-layer versioning model:
 *   1. API/Feature Version (FW_MAJOR.FW_MINOR.FW_PATCH) - user-visible, changes rarely
 *   2. Build Number (FW_BUILD_NUMBER) - monotonic integer for OTA comparison
 *   3. Commit ID (FW_GIT_HASH) - for debugging
 */

#ifndef VERSION_H
#define VERSION_H

#include <stdint.h>

// =============================================================================
// API / Feature Version (user-visible, changes rarely)
// =============================================================================
#define FW_MAJOR  0
#define FW_MINOR  6
#define FW_PATCH  5

// Prerelease tag: "alpha", "beta", "rc1", or "" for release
#define FW_PRERELEASE "beta"

// =============================================================================
// Build Number (monotonic integer for OTA comparison)
// Injected at build time by version_inject.py; defaults to 0 for fallback
// =============================================================================
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

// =============================================================================
// Git Commit Hash (for debugging)
// Injected at build time by version_inject.py; defaults to "unknown"
// =============================================================================
#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

// =============================================================================
// EEPROM configuration version
// Increment when Input struct layout changes (forces reconfiguration)
// Version 2: Changed from storing enum indices to storing name hashes (Phase 5)
// =============================================================================
#define EEPROM_VERSION 2

// =============================================================================
// Helper functions (defined in version.cpp)
// =============================================================================

// Returns the build number for OTA version comparison
uint32_t firmwareVersion();

// Returns human-readable version string, e.g., "0.6.3-beta (b147 @a1b2c3d)"
const char* firmwareVersionString();

#endif
