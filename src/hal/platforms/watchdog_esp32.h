/*
 * watchdog_esp32.h - ESP32 watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 * Uses ESP-IDF task watchdog API
 */

#ifndef HAL_WATCHDOG_ESP32_H
#define HAL_WATCHDOG_ESP32_H

#include <esp_task_wdt.h>

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
    uint32_t timeout_s = (timeout_ms + 999) / 1000;  // Round up to seconds
    esp_task_wdt_init(timeout_s, true);  // Panic on timeout
    esp_task_wdt_add(NULL);  // Add current task
}

inline void watchdogReset() {
    esp_task_wdt_reset();
}

inline void watchdogDisable() {
    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();
}

} // namespace hal

#endif // HAL_WATCHDOG_ESP32_H
