/*
 * display_manager.h - Display state management
 * Manages runtime display state separate from persistent config
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "../config.h"

void initDisplayManager();
bool isDisplayActive();
void toggleDisplayRuntime();
void setDisplayRuntime(bool enabled);

#endif // DISPLAY_MANAGER_H
