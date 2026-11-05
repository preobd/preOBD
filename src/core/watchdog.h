/*
 * watchdog.h - Platform-abstracted watchdog timer
 * Provides unified interface across all supported platforms
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

// The watchdog's timeout is decided in one place, watchdog_policy.h (#541):
// it runs in every mode on every board once started, at 8 s in BOOT and
// CONFIG and 2 s in RUN, and a WatchdogScope widens it around known slow work.
// Nothing outside core/ sets a timeout directly.

// Arm the watchdog at the BOOT timeout. Called once from setup().
void watchdogStart();

// The phase follows the system mode; setMode() calls this.
void watchdogSetConfigMode(bool config);

// Reset watchdog timer (call regularly in main loop)
void watchdogReset();

// Timeout in force now, in ms (0 before watchdogStart()). For status text.
uint16_t watchdogTimeoutMs();

// What slow work is running ("SD", ...), or "LOOP" outside any scope (#542).
const char* watchdogActiveTag();

// Widen the watchdog around work known to block, e.g. SD card I/O:
//     { WatchdogScope wd(20000, "SD"); file.flush(); }
// Nestable; the timeout is the widest of the mode and every open scope, so an
// inner scope never narrows an outer one. Leaving the last scope restores the
// mode's timeout, counted from that moment. The tag must be a string that
// outlives the scope (a literal).
class WatchdogScope {
public:
    WatchdogScope(uint16_t timeout_ms, const char* tag);
    ~WatchdogScope();
private:
    WatchdogScope(const WatchdogScope&);
    WatchdogScope& operator=(const WatchdogScope&);
};

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
