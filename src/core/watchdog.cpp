/*
 * watchdog.cpp - Platform-abstracted watchdog timer
 *
 * Holds the policy state (watchdog_policy.h) and applies its timeout through
 * the HAL. The HAL is only told when the effective timeout changes, and every
 * change feeds, so a new timeout counts from the moment it applies.
 */

#include "watchdog.h"
#include "watchdog_policy.h"
#include "../hal/hal_watchdog.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif

// Zero-initialized storage is a valid BOOT policy with no scopes, so scopes
// opened before watchdogStart() are tracked and simply apply once it runs.
static WatchdogPolicy s_policy;
static bool     s_started = false;
static uint16_t s_appliedMs = 0;

// Touches the hardware only when the effective timeout changes, and then
// feeds so the new timeout counts from now. A scope that changes nothing (one
// nested in a wider scope, or a CONFIG<->CONFIG setMode) doesn't feed: a hung
// loop that keeps opening such scopes must still trip the watchdog. A loop
// whose scopes do change the timeout (widen on open, narrow on close) does
// restart it each time, so retry loops around scoped work must be bounded.
static void apply() {
    if (!s_started) return;
    const uint16_t ms = watchdogPolicyTimeoutMs(s_policy);
    if (ms == s_appliedMs) return;
    hal::watchdogEnable(ms);
    hal::watchdogReset();
    s_appliedMs = ms;
}

FLASHMEM void watchdogStart() {
    if (s_started) return;
    s_started = true;
    apply();
}

FLASHMEM void watchdogSetConfigMode(bool config) {
    watchdogPolicySetPhase(s_policy, config ? WD_PHASE_CONFIG : WD_PHASE_RUN);
    apply();
}

void watchdogReset() {
    hal::watchdogReset();
}

FLASHMEM uint16_t watchdogTimeoutMs() {
#if defined(__AVR__)
    return s_appliedMs > 8000 ? 8000 : s_appliedMs;   // WDTO_8S is the AVR ceiling
#else
    return s_appliedMs;
#endif
}

FLASHMEM const char* watchdogActiveTag() {
    return watchdogPolicyTag(s_policy);
}

// Loop-context only, like every caller today: no ISR opens a scope, so the
// policy state needs no locking. An ISR-side scope would.
WatchdogScope::WatchdogScope(uint16_t timeout_ms, const char* tag) {
    watchdogPolicyPush(s_policy, timeout_ms, tag);
    apply();
}

WatchdogScope::~WatchdogScope() {
    watchdogPolicyPop(s_policy);
    apply();
}
