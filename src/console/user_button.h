/*
 * user_button.h - The user button: held at boot it selects CONFIG mode
 * (core/system_mode.cpp); a short press acknowledges alarms and a long press
 * toggles the display (main.cpp).
 * Only compiled when INCLUDE_USER_BUTTON is nonzero (boards/resolve.h).
 */

#ifndef USER_BUTTON_H
#define USER_BUTTON_H

#include <Arduino.h>

// Button press types
enum ButtonPress {
    BUTTON_NONE = 0,      // No press detected
    BUTTON_SHORT_PRESS,   // Short press (< 2 seconds)
    BUTTON_LONG_PRESS     // Long press (>= 2 seconds)
};

// Initialize the user button
void initUserButton();

// Update button state (call frequently from main loop)
// Returns button press type if a press was completed, otherwise BUTTON_NONE
ButtonPress updateUserButton();

// Get current button state (for debugging)
bool isUserButtonPressed();

#endif // USER_BUTTON_H
