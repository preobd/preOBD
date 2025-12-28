/*
 * message_api.h - High-Level Messaging API
 *
 * Provides convenient logging interface that routes to appropriate transports:
 * - msg.control - Interactive commands, configuration responses
 * - msg.data - Sensor data output (CSV, RealDash binary)
 * - msg.debug - Debug/diagnostic messages
 *
 * Usage:
 *   msg.control.println(F("Configuration saved"));
 *   msg.data.print(ptr->abbrName);
 *   msg.debug.println(F("✓ ADC configured"));
 *
 * Build Flags:
 *   -D DISABLE_DEBUG_MESSAGES - Compile out all debug messages (saves flash/RAM)
 */

#ifndef MESSAGE_API_H
#define MESSAGE_API_H

#include "message_router.h"
#include <Arduino.h>

// Print stream wrapper that routes to a specific message plane
class MessageStream {
private:
    MessagePlane plane;

public:
    MessageStream(MessagePlane p) : plane(p) {}

    // ========== Text Output ==========

    size_t print(const char* str) {
        if (!str) return 0;
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t n = t->print(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
        if (t2 && t2->isConnected()) {
            t2->print(str);
        }

        return n;
    }

    size_t println(const char* str) {
        if (!str) return 0;
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t n = t->println(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
        if (t2 && t2->isConnected()) {
            t2->println(str);
        }

        return n;
    }

    size_t println() {
        return println("");
    }

    // ========== Numeric Output ==========

    size_t print(int n) {
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(n);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
        if (t2 && t2->isConnected()) {
            t2->print(n);
        }

        return written;
    }

    size_t println(int n) {
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t written = t->println(n);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
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

    // ========== Float Output ==========

    size_t print(float f, int digits = 2) {
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(f, digits);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
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
        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t written = t->print(str);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
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

        TransportInterface* t = router.getTransport(plane, true);
        if (!t || !t->isConnected()) return 0;
        size_t written = t->write(data, len);

        // Multi-cast to secondary if configured
        TransportInterface* t2 = router.getTransport(plane, false);
        if (t2 && t2->isConnected()) {
            t2->write(data, len);
        }

        return written;
    }

    size_t write(uint8_t c) {
        return write(&c, 1);
    }
};

// Stub class for disabled debug messages (all methods compile to no-ops)
#ifdef DISABLE_DEBUG_MESSAGES
class MessageStreamStub {
public:
    MessageStreamStub(MessagePlane p) { (void)p; }  // Suppress unused parameter warning

    // All methods are inline no-ops that the optimizer will eliminate
    inline size_t print(const char* str) { (void)str; return 0; }
    inline size_t println(const char* str) { (void)str; return 0; }
    inline size_t println() { return 0; }
    inline size_t print(int n) { (void)n; return 0; }
    inline size_t println(int n) { (void)n; return 0; }
    inline size_t print(unsigned int n) { (void)n; return 0; }
    inline size_t println(unsigned int n) { (void)n; return 0; }
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

    MessageAPI() : control(PLANE_CONTROL), data(PLANE_DATA), debug(PLANE_DEBUG) {}
};

extern MessageAPI msg;

#endif // MESSAGE_API_H
