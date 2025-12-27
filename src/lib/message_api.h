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

// Global message API instance
struct MessageAPI {
    MessageStream control;
    MessageStream data;
    MessageStream debug;

    MessageAPI() : control(PLANE_CONTROL), data(PLANE_DATA), debug(PLANE_DEBUG) {}
};

extern MessageAPI msg;

#endif // MESSAGE_API_H
