/*
 * button_handler.h - Multi-function button handler
 * Handles MODE_BUTTON with debouncing and long-press detection
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>

// Button press types
enum ButtonPress {
    BUTTON_NONE = 0,      // No press detected
    BUTTON_SHORT_PRESS,   // Short press (< 2 seconds)
    BUTTON_LONG_PRESS     // Long press (>= 2 seconds)
};

// Initialize button handler
void initButtonHandler();

// Update button state (call frequently from main loop)
// Returns button press type if a press was completed, otherwise BUTTON_NONE
ButtonPress updateButtonHandler();

// Get current button state (for debugging)
bool isButtonPressed();

#endif // BUTTON_HANDLER_H
