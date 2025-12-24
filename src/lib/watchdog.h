/*
 * watchdog.h - Platform-abstracted watchdog timer
 * Provides unified interface across all supported platforms
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

// Platform-agnostic watchdog interface
// Enable watchdog with specified timeout in milliseconds
void watchdogEnable(uint16_t timeout_ms);

// Reset watchdog timer (call regularly in main loop)
void watchdogReset();

// Disable watchdog (use sparingly, mainly for debugging)
void watchdogDisable();

#endif
