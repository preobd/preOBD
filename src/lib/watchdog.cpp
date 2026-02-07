/*
 * watchdog.cpp - Platform-abstracted watchdog timer
 * Delegates to HAL for platform-specific implementations
 */

#include "watchdog.h"
#include "../hal/hal_watchdog.h"

void watchdogEnable(uint16_t timeout_ms) {
    hal::watchdogEnable(timeout_ms);
}

void watchdogReset() {
    hal::watchdogReset();
}

void watchdogDisable() {
    hal::watchdogDisable();
}
