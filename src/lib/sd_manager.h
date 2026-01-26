/*
 * sd_manager.h - SD Card Management
 *
 * Centralized SD card initialization for use by multiple modules
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>

// Initialize SD card (called once during setup)
void initSD();

// Check if SD card is initialized
bool isSDInitialized();

#endif // SD_MANAGER_H
