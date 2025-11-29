/*
 * system_mode.h - System mode management (CONFIG vs RUN)
 * Only compiled in EEPROM/runtime configuration mode
 */

#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#include <Arduino.h>

// System operating modes
enum SystemMode {
    MODE_RUN,      // Normal operation (sensors active, config locked)
    MODE_CONFIG    // Configuration mode (sensors paused, config unlocked)
};

// Mode management functions
void initSystemMode();
SystemMode getCurrentMode();
void setMode(SystemMode newMode);
bool isInConfigMode();
bool isInRunMode();

// Boot-time mode detection (call once in setup)
SystemMode detectBootMode(bool eepromValid);

#endif
