/*
 * display_manager.h - Display state management
 * Manages runtime display state separate from persistent config
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

// Initialize display manager (call after systemConfig is loaded)
void initDisplayManager();

// Get current runtime display state
bool isDisplayActive();

// Toggle display runtime state (for button press)
void toggleDisplayRuntime();

// Set display runtime state (for serial commands)
void setDisplayRuntime(bool enabled);

#endif // DISPLAY_MANAGER_H
