/*
 * display_manager.h - Display state management
 * Manages runtime display state separate from persistent config
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "../config.h"

#ifndef USE_STATIC_CONFIG
    // Full display manager (EEPROM mode with runtime toggle)
    void initDisplayManager();
    bool isDisplayActive();
    void toggleDisplayRuntime();
    void setDisplayRuntime(bool enabled);
#else
    // Static config mode - display is always on (simplified)
    inline void initDisplayManager() {}
    inline bool isDisplayActive() { return true; }
    inline void toggleDisplayRuntime() {}
    inline void setDisplayRuntime(bool enabled) {}
#endif

#endif // DISPLAY_MANAGER_H
