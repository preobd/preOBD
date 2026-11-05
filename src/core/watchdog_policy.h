/*
 * watchdog_policy.h - What the watchdog timeout should be right now (#541)
 *
 * The one place that decides the timeout. Before this, four call sites each
 * encoded part of it (boot, setMode, and the SD window's enter/exit), with
 * "CONFIG disables it, except on Teensy 4, which can't" in the middle (#243).
 *
 * The policy (#540, owner decision 2026-10-07: every board behaves the same):
 *   - The watchdog always runs once started, in every mode, on every board.
 *     It only fires if the firmware stops looping; the loop feeds it on every
 *     pass in CONFIG too (#268), so an idle prompt is never at risk.
 *   - Phase timeouts: BOOT 8 s (setup() until the first setMode), CONFIG 8 s,
 *     RUN 2 s.
 *   - A WatchdogScope (watchdog.h) widens it for known slow work, e.g. 20 s
 *     around SD card I/O. Scopes nest, and a scope only ever widens: the
 *     effective timeout is the maximum of the phase and every open scope, so
 *     an inner short scope can't narrow an outer long one.
 *   - The innermost scope's tag says what slow work is running, for reporting
 *     where a watchdog reset happened (#542). No scope: "BOOT" in setup(),
 *     "LOOP" after.
 *
 * Pure: no hardware, no globals. core/watchdog.cpp holds the state and
 * applies the result; the native test includes the .cpp directly.
 */

#ifndef WATCHDOG_POLICY_H
#define WATCHDOG_POLICY_H

#include <stdint.h>

enum WatchdogPhase : uint8_t {
    WD_PHASE_BOOT,
    WD_PHASE_CONFIG,
    WD_PHASE_RUN,
};

#define WD_BOOT_TIMEOUT_MS   8000
#define WD_CONFIG_TIMEOUT_MS 8000
#define WD_RUN_TIMEOUT_MS    2000

// Scopes held with their own timeout and tag. Real nesting today is ~4
// (an SD row: updateSDLog -> openLogFile -> closeLogFile / claimNextFileName).
// Deeper scopes still count toward the timeout (see `extra*`); only their tags
// collapse into one.
#define WD_MAX_SCOPES 6

struct WatchdogPolicy {
    WatchdogPhase phase;
    uint8_t       depth;                    // scopes held in ms[]/tag[]
    uint16_t      ms[WD_MAX_SCOPES];
    const char*   tag[WD_MAX_SCOPES];
    uint8_t       extra;                    // open scopes past WD_MAX_SCOPES
    uint16_t      extraMs;                  // widest of those
    const char*   extraTag;                 // innermost of those
};

void        watchdogPolicyInit(WatchdogPolicy& p);
void        watchdogPolicySetPhase(WatchdogPolicy& p, WatchdogPhase phase);
void        watchdogPolicyPush(WatchdogPolicy& p, uint16_t ms, const char* tag);
void        watchdogPolicyPop(WatchdogPolicy& p);   // an extra pop is ignored
uint16_t    watchdogPolicyTimeoutMs(const WatchdogPolicy& p);
const char* watchdogPolicyTag(const WatchdogPolicy& p);   // innermost scope, else "BOOT"/"LOOP"
uint16_t    watchdogPhaseTimeoutMs(WatchdogPhase phase);

#endif // WATCHDOG_POLICY_H
