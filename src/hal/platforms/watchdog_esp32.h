/*
 * watchdog_esp32.h - ESP32 watchdog implementation
 * Part of the preOBD Hardware Abstraction Layer
 * Uses ESP-IDF task watchdog API
 */

#ifndef HAL_WATCHDOG_ESP32_H
#define HAL_WATCHDOG_ESP32_H

#include <esp_task_wdt.h>
#include <esp_idf_version.h>

namespace hal {

inline void watchdogEnable(uint16_t timeout_ms) {
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    // IDF 5 (Arduino-ESP32 3.x) takes a config struct and the core has usually
    // initialised the task watchdog already, so reconfigure rather than init.
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = timeout_ms;
    cfg.idle_core_mask = 0;
    cfg.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
    uint32_t timeout_s = (timeout_ms + 999) / 1000;  // Round up to seconds
    esp_task_wdt_init(timeout_s, true);  // Panic on timeout
#endif
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
