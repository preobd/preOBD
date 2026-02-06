/*
 * watchdog_stub.h - Stub watchdog implementation for unknown platforms
 * Part of the preOBD Hardware Abstraction Layer
 * Provides no-op implementations when platform watchdog is unavailable
 */

#ifndef HAL_WATCHDOG_STUB_H
#define HAL_WATCHDOG_STUB_H

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
    // No-op for unknown platforms
    (void)timeout_ms;
}

inline void watchdogReset() {
    // No-op
}

inline void watchdogDisable() {
    // No-op
}

} // namespace hal

#endif // HAL_WATCHDOG_STUB_H
