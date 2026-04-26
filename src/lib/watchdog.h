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

// Print wrapper that feeds the watchdog every N bytes.
// Use when serializing large payloads over slow transports (e.g. a BLE UART
// bridge at 9600 baud) where write() blocks long enough to trip the watchdog.
// Default interval of 256 bytes: ~270ms at 9600 baud, well within a 2s window.
class WatchdogKickingPrint : public Print {
public:
    WatchdogKickingPrint(Print& out, uint16_t interval = 256)
        : _out(out), _interval(interval), _count(0) {}
    size_t write(uint8_t c) override {
        if (++_count >= _interval) { watchdogReset(); _count = 0; }
        return _out.write(c);
    }
    size_t write(const uint8_t* buf, size_t size) override {
        size_t written = 0;
        while (written < size) {
            uint16_t remaining = _interval - _count;
            uint16_t chunk = (size - written < remaining) ? (size - written) : remaining;
            size_t n = _out.write(buf + written, chunk);
            written += n;
            _count += n;
            if (_count >= _interval) { watchdogReset(); _count = 0; }
            if (n < chunk) break;  // downstream write failure
        }
        return written;
    }
private:
    Print& _out;
    uint16_t _interval;
    uint16_t _count;
};

#endif
