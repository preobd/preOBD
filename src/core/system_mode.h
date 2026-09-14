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

// Mode-transition observer (same shape as PinOwnerInputProbe in
// pinctrl/pin_owner.h): core/ announces the transition, the composition root
// (main.cpp) wires up who reacts (LCD, LED). Keeps core/ free of outputs/ and
// drivers/ dependencies. Unregistered, setMode() simply notifies no one.
typedef void (*SystemModeObserver)(SystemMode oldMode, SystemMode newMode);

/**
 * Register the mode-transition observer. MUST be called before the first
 * setMode() (main.cpp calls setMode(bootMode) at boot) -- otherwise the boot
 * transition finds no observer and the LCD/LED never reflect it.
 */
void setSystemModeObserver(SystemModeObserver observer);

// Boot-time mode detection (call once in setup)
SystemMode detectBootMode(bool eepromValid);

#endif
