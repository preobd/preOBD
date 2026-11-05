/*
 * watchdog_policy.cpp - see watchdog_policy.h
 */

#include "watchdog_policy.h"

#if defined(ARDUINO)
#include <Arduino.h>   // FLASHMEM on Teensy
#endif
#ifndef FLASHMEM
#define FLASHMEM
#endif

uint16_t watchdogPhaseTimeoutMs(WatchdogPhase phase) {
    switch (phase) {
        case WD_PHASE_RUN:    return WD_RUN_TIMEOUT_MS;
        case WD_PHASE_CONFIG: return WD_CONFIG_TIMEOUT_MS;
        case WD_PHASE_BOOT:   break;
    }
    return WD_BOOT_TIMEOUT_MS;
}

FLASHMEM void watchdogPolicyInit(WatchdogPolicy& p) {
    p.phase = WD_PHASE_BOOT;
    p.depth = 0;
    p.extra = 0;
    p.extraMs = 0;
    p.extraTag = nullptr;
    for (uint8_t i = 0; i < WD_MAX_SCOPES; i++) {
        p.ms[i] = 0;
        p.tag[i] = nullptr;
    }
}

FLASHMEM void watchdogPolicySetPhase(WatchdogPolicy& p, WatchdogPhase phase) {
    p.phase = phase;
}

void watchdogPolicyPush(WatchdogPolicy& p, uint16_t ms, const char* tag) {
    if (p.depth < WD_MAX_SCOPES) {
        p.ms[p.depth] = ms;
        p.tag[p.depth] = tag;
        p.depth++;
        return;
    }
    p.extra++;
    if (ms > p.extraMs) p.extraMs = ms;
    p.extraTag = tag;
}

void watchdogPolicyPop(WatchdogPolicy& p) {
    if (p.extra > 0) {
        // The overflow scopes' widths are only known as a maximum, so it holds
        // until the last of them closes. Errs wide, never narrow.
        if (--p.extra == 0) {
            p.extraMs = 0;
            p.extraTag = nullptr;
        }
        return;
    }
    if (p.depth > 0) p.depth--;
}

uint16_t watchdogPolicyTimeoutMs(const WatchdogPolicy& p) {
    uint16_t t = watchdogPhaseTimeoutMs(p.phase);
    for (uint8_t i = 0; i < p.depth; i++) {
        if (p.ms[i] > t) t = p.ms[i];
    }
    if (p.extra > 0 && p.extraMs > t) t = p.extraMs;
    return t;
}

FLASHMEM const char* watchdogPolicyTag(const WatchdogPolicy& p) {
    if (p.extra > 0 && p.extraTag) return p.extraTag;
    if (p.depth > 0 && p.tag[p.depth - 1]) return p.tag[p.depth - 1];
    return "LOOP";
}
