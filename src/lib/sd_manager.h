/*
 * sd_manager.h - SD Card Management
 *
 * Centralized SD card initialization for use by multiple modules
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include "../config.h"

#if SUPPORTS_SD

// Initialize SD card (called once during setup)
void initSD();

// Check if SD card is initialized
bool isSDInitialized();

#else

// Stub functions when SD is not available on this board
inline void initSD() {}
inline bool isSDInitialized() { return false; }

#endif

#endif // SD_MANAGER_H
