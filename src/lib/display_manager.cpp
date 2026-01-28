/*
 * display_manager.cpp - Display state management implementation
 */

#include "display_manager.h"

#ifndef USE_STATIC_CONFIG

#include "system_config.h"
#include "message_api.h"
#include "log_tags.h"

// Display control functions from display modules
extern void enableLCD();
extern void disableLCD();

// Runtime display state (separate from systemConfig.displayEnabled)
// This is NEVER saved to EEPROM - resets to systemConfig default at boot
static bool displayRuntimeState = true;

void initDisplayManager() {
    // Initialize runtime state from persistent config
    displayRuntimeState = systemConfig.displayEnabled;

    // Apply initial state
    if (displayRuntimeState) {
        enableLCD();
    } else {
        disableLCD();
    }
}

bool isDisplayActive() {
    return displayRuntimeState;
}

void toggleDisplayRuntime() {
    // Toggle runtime state (NEVER modifies systemConfig.displayEnabled)
    displayRuntimeState = !displayRuntimeState;

    if (displayRuntimeState) {
        enableLCD();
        msg.debug.info(TAG_DISPLAY, "Display toggled ON");
    } else {
        disableLCD();
        msg.debug.info(TAG_DISPLAY, "Display toggled OFF");
    }
}

void setDisplayRuntime(bool enabled) {
    // Set runtime state directly (for serial commands)
    // Also NEVER modifies systemConfig.displayEnabled
    displayRuntimeState = enabled;

    if (displayRuntimeState) {
        enableLCD();
    } else {
        disableLCD();
    }
}

#endif // USE_STATIC_CONFIG
