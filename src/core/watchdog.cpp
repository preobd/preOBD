/*
 * watchdog.cpp - Platform-abstracted watchdog timer
 * Delegates to HAL for platform-specific implementations
 */

#include "watchdog.h"
#include "../hal/hal_watchdog.h"

// Teensy 4.x's WDOG1 can't be disabled once enabled (#243): watchdogDisable()
// is a no-op there, so the watchdog stays armed until the next reset.
#if defined(__IMXRT1062__)
static const bool WATCHDOG_CAN_DISABLE = false;
#else
static const bool WATCHDOG_CAN_DISABLE = true;
#endif

static bool s_armed = false;

void watchdogEnable(uint16_t timeout_ms) {
    hal::watchdogEnable(timeout_ms);
    s_armed = true;
}

bool watchdogArmed() {
    return s_armed;
}

void watchdogReset() {
    hal::watchdogReset();
}

void watchdogDisable() {
    hal::watchdogDisable();
    if (WATCHDOG_CAN_DISABLE) s_armed = false;
}
