/*
 * message_api.h - High-Level Messaging API
 *
 * Provides convenient logging interface that routes to appropriate transports:
 * - msg.control - Interactive commands, configuration responses (user-facing)
 * - msg.data - Sensor data output (CSV, RealDash binary)
 * - msg.debug - Debug/diagnostic messages with log levels and tags
 *
 * Usage:
 *   // Control plane - user feedback with F() macro
 *   msg.control.println(F("✓ Configuration saved"));
 *
 *   // Data plane - sensor output
 *   msg.data.print(ptr->abbrName);
 *
 *   // Debug plane - structured logging (NO F() macro - printf uses RAM strings)
 *   msg.debug.error(TAG_SD, "Mount failed");
 *   msg.debug.warn(TAG_SENSOR, "BME280 not found at 0x%02X", addr);
 *   msg.debug.info(TAG_ADC, "ADC configured: %d-bit resolution", bits);
 *   msg.debug.debug(TAG_I2C, "Read %d bytes from device 0x%02X", count, addr);
 *
 *   // Or use macro shortcuts
 *   LOG_ERROR(TAG_SD, "Mount failed");
 *   LOG_INFO(TAG_ADC, "ADC configured: %d-bit resolution", bits);
 *
 * Build Flags:
 *   -D DISABLE_DEBUG_MESSAGES - Compile out all debug messages (saves flash/RAM)
 */

#ifndef MESSAGE_API_H
#define MESSAGE_API_H

#include "message_router.h"
#include "log_filter.h"
#include "log_tags.h"
#include <Arduino.h>
#include <stdarg.h>  // For variadic functions

// Print stream wrapper that routes to a specific message plane
class MessageStream {
private:
    MessagePlane plane;
    // When true (used by msg.control): during command dispatch, route to the
    // transport that sent the command (activeControlTransport) so responses
    // return to the sender.  When activeControlTransport is nullptr (between
    // dispatches), falls back to the configured primary transport so unsolicited
    // output (alarms, boot banners, etc.) still reaches all configured ports.
    bool routeToActive;

    // Single source of truth for primary write target.
    TransportInterface* primaryTransport() {
        if (routeToActive) {
            TransportInterface* a = router.getActiveControlTransport();
            if (a) return a;
        }
        return router.getTransport(plane, true);
    }

    // Secondary multi-cast target.  Suppressed during active dispatch so the
    // response goes only to the sender (not also to the other control port).
    TransportInterface* secondaryTransport() {
        if (routeToActive && router.getActiveControlTransport()) return nullptr;
        return router.getTransport(plane, false);
    }

public:
    MessageStream(MessagePlane p, bool useActive = false)
        : plane(p), routeToActive(useActive) {}

    // ========== Text Output ==========

    size_t print(const char* str) {
        if (!str) return 0;
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t n = t->print(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->print(str);
        }

        return n;
    }

    size_t println(const char* str) {
        if (!str) return 0;
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t n = t->println(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->println(str);
        }

        return n;
    }

    size_t println() {
        return println("");
    }

    // ========== Character Output ==========

    size_t print(char c) {
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(c);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->print(c);
        }

        return written;
    }

    size_t println(char c) {
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->println(c);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->println(c);
        }

        return written;
    }

    // ========== Numeric Output ==========

    // Note: uint8_t is typedef'd to unsigned char, so we need explicit overloads
    // to prevent uint8_t values from matching print(char) instead of print(int)
    size_t print(unsigned char n) {
        return print((int)n);
    }

    size_t println(unsigned char n) {
        return println((int)n);
    }

    size_t print(int n) {
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(n);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->print(n);
        }

        return written;
    }

    size_t println(int n) {
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->println(n);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->println(n);
        }

        return written;
    }

    size_t print(unsigned int n) {
        return print((int)n);
    }

    size_t println(unsigned int n) {
        return println((int)n);
    }

    // Print with base (HEX, DEC, BIN, OCT)
    size_t print(int n, int base) {
        char buf[20];
        if (base == HEX) {
            utoa(n, buf, 16);
        } else if (base == BIN) {
            utoa(n, buf, 2);
        } else if (base == OCT) {
            utoa(n, buf, 8);
        } else {
            itoa(n, buf, 10);
        }
        return print(buf);
    }

    size_t println(int n, int base) {
        return print(n, base) + println();
    }

    size_t print(unsigned int n, int base) {
        return print((int)n, base);
    }

    size_t println(unsigned int n, int base) {
        return print((unsigned int)n, base) + println();
    }

    size_t print(long n) {
        char buf[16];
        ltoa(n, buf, 10);
        return print(buf);
    }

    size_t println(long n) {
        return print(n) + println();
    }

    size_t print(unsigned long n) {
        char buf[16];
        ultoa(n, buf, 10);
        return print(buf);
    }

    size_t println(unsigned long n) {
        return print(n) + println();
    }

    size_t print(unsigned long n, int base) {
        char buf[20];
        if (base == HEX) {
            ultoa(n, buf, 16);
        } else if (base == BIN) {
            ultoa(n, buf, 2);
        } else if (base == OCT) {
            ultoa(n, buf, 8);
        } else {
            ultoa(n, buf, 10);
        }
        return print(buf);
    }

    size_t println(unsigned long n, int base) {
        return print(n, base) + println();
    }

    // ========== Float Output ==========

    size_t print(float f, int digits = 2) {
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(f, digits);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->print(f, digits);
        }

        return written;
    }

    size_t println(float f, int digits = 2) {
        return print(f, digits) + println();
    }

    size_t print(double d, int digits = 2) {
        return print((float)d, digits);
    }

    size_t println(double d, int digits = 2) {
        return print((float)d, digits) + println();
    }

    // ========== Flash String Support ==========

    size_t print(const __FlashStringHelper* str) {
        if (!str) return 0;
        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->print(str);
        }

        return written;
    }

    size_t println(const __FlashStringHelper* str) {
        size_t n = print(str);
        n += println();
        return n;
    }

    // ========== Binary Output (for RealDash, etc.) ==========

    size_t write(const uint8_t* data, size_t len) {
        if (!data || len == 0) return 0;

        TransportInterface* t = primaryTransport();
        if (!t || !t->isConnected()) return 0;
        size_t written = t->write(data, len);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = secondaryTransport();
        if (t2 && t2->isConnected()) {
            t2->write(data, len);
        }

        return written;
    }

    size_t write(uint8_t c) {
        return write(&c, 1);
    }

    // ========== Level-Based Logging (Printf-style) ==========

    // Printf-style logging methods with log levels and tags
    // Note: Do NOT use F() macro - format strings must be in RAM for vsnprintf
    // These methods check the log filter before formatting/outputting
    size_t error(const char* tag, const char* fmt, ...);
    size_t warn(const char* tag, const char* fmt, ...);
    size_t info(const char* tag, const char* fmt, ...);
    size_t debug(const char* tag, const char* fmt, ...);

private:
    // Helper method to format and output with level/tag prefix
    size_t logWithLevel(LogLevel level, const char* tag, const char* msg);
};

// Stub class for disabled debug messages (all methods compile to no-ops)
#ifdef DISABLE_DEBUG_MESSAGES
class MessageStreamStub {
public:
    MessageStreamStub(MessagePlane p, bool useActive = false) { (void)p; (void)useActive; }

    // All methods are inline no-ops that the optimizer will eliminate
    inline size_t print(const char* str) { (void)str; return 0; }
    inline size_t println(const char* str) { (void)str; return 0; }
    inline size_t println() { return 0; }
    inline size_t print(char c) { (void)c; return 0; }
    inline size_t println(char c) { (void)c; return 0; }
    inline size_t print(unsigned char n) { (void)n; return 0; }
    inline size_t println(unsigned char n) { (void)n; return 0; }
    inline size_t print(int n) { (void)n; return 0; }
    inline size_t println(int n) { (void)n; return 0; }
    inline size_t print(unsigned int n) { (void)n; return 0; }
    inline size_t println(unsigned int n) { (void)n; return 0; }
    inline size_t print(int n, int base) { (void)n; (void)base; return 0; }
    inline size_t println(int n, int base) { (void)n; (void)base; return 0; }
    inline size_t print(unsigned int n, int base) { (void)n; (void)base; return 0; }
    inline size_t println(unsigned int n, int base) { (void)n; (void)base; return 0; }
    inline size_t print(long n) { (void)n; return 0; }
    inline size_t println(long n) { (void)n; return 0; }
    inline size_t print(unsigned long n) { (void)n; return 0; }
    inline size_t println(unsigned long n) { (void)n; return 0; }
    inline size_t print(float f, int digits = 2) { (void)f; (void)digits; return 0; }
    inline size_t println(float f, int digits = 2) { (void)f; (void)digits; return 0; }
    inline size_t print(double d, int digits = 2) { (void)d; (void)digits; return 0; }
    inline size_t println(double d, int digits = 2) { (void)d; (void)digits; return 0; }
    inline size_t print(const __FlashStringHelper* str) { (void)str; return 0; }
    inline size_t println(const __FlashStringHelper* str) { (void)str; return 0; }
    inline size_t write(const uint8_t* data, size_t len) { (void)data; (void)len; return 0; }
    inline size_t write(uint8_t c) { (void)c; return 0; }

    // Level-based logging stub methods (compile to no-ops)
    inline size_t error(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; return 0; }
    inline size_t warn(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; return 0; }
    inline size_t info(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; return 0; }
    inline size_t debug(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; return 0; }
};
#endif

// Global message API instance
struct MessageAPI {
    MessageStream control;
    MessageStream data;

#ifdef DISABLE_DEBUG_MESSAGES
    MessageStreamStub debug;  // Stub version - compiles to no-ops
#else
    MessageStream debug;      // Full version
#endif

    MessageAPI() : control(PLANE_CONTROL, /*routeToActive=*/true), data(PLANE_DATA), debug(PLANE_DEBUG) {}
};

extern MessageAPI msg;

#endif // MESSAGE_API_H
