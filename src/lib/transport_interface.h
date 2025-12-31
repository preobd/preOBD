/*
 * transport_interface.h - Hardware-Agnostic Transport Interface
 *
 * Provides a unified Print-compatible interface for Serial, Bluetooth, and other
 * communication backends. Enables runtime switching between transports and
 * supports multiple transport types (USB Serial, hardware UARTs, BT/BLE).
 *
 * Part of the Transport Abstraction Layer (TAL) for openEMS.
 */

#ifndef TRANSPORT_INTERFACE_H
#define TRANSPORT_INTERFACE_H

#include <Arduino.h>

// Transport capabilities bitfield
enum TransportCapabilities {
    CAP_NONE            = 0x00,
    CAP_READ            = 0x01,  // Can receive data
    CAP_WRITE           = 0x02,  // Can send data
    CAP_BINARY          = 0x04,  // Supports binary protocols
    CAP_AUTHENTICATED   = 0x08,  // Requires/supports authentication
    CAP_HARDWARE_SERIAL = 0x10,  // Hardware UART (reliable, fast)
    CAP_VIRTUAL         = 0x20,  // Virtual/software serial
};

// Transport connection state
enum TransportState {
    TRANSPORT_DISCONNECTED = 0,
    TRANSPORT_CONNECTING   = 1,
    TRANSPORT_CONNECTED    = 2,
    TRANSPORT_ERROR        = 3,
};

// Abstract transport interface
// All concrete transports (Serial, Bluetooth, etc.) implement this interface
class TransportInterface {
public:
    virtual ~TransportInterface() {}

    // ========== Core I/O (Print-compatible) ==========

    // Write single byte
    virtual size_t write(uint8_t c) = 0;

    // Write buffer (for binary data like RealDash frames)
    virtual size_t write(const uint8_t* buffer, size_t size) = 0;

    // Check bytes available for reading
    virtual int available() = 0;

    // Read single byte (-1 if none available)
    virtual int read() = 0;

    // Peek at next byte without consuming it
    virtual int peek() = 0;

    // Flush output buffer
    virtual void flush() = 0;

    // ========== Transport Metadata ==========

    // Get transport name (e.g., "USB", "SERIAL1", "ESP32_BT")
    virtual const char* getName() const = 0;

    // Get capability flags (CAP_READ | CAP_WRITE | ...)
    virtual uint8_t getCapabilities() const = 0;

    // Get current connection state
    virtual TransportState getState() const = 0;

    // ========== Lifecycle Management ==========

    // Initialize transport (called once at startup)
    virtual bool begin() = 0;

    // Shutdown transport
    virtual void end() = 0;

    // Update/housekeeping (called each loop)
    virtual void update() = 0;

    // ========== Capability Queries ==========

    bool canRead() const {
        return (getCapabilities() & CAP_READ) != 0;
    }

    bool canWrite() const {
        return (getCapabilities() & CAP_WRITE) != 0;
    }

    bool supportsBinary() const {
        return (getCapabilities() & CAP_BINARY) != 0;
    }

    bool requiresAuth() const {
        return (getCapabilities() & CAP_AUTHENTICATED) != 0;
    }

    bool isConnected() const {
        return getState() == TRANSPORT_CONNECTED;
    }

    // ========== Print Convenience Methods ==========
    // These provide compatibility with Serial.print() API

    size_t print(const char* str) {
        if (!str) return 0;
        return write((const uint8_t*)str, strlen(str));
    }

    size_t println(const char* str) {
        size_t n = print(str);
        n += write('\r');
        n += write('\n');
        return n;
    }

    size_t println() {
        size_t n = write('\r');
        n += write('\n');
        return n;
    }

    size_t print(int n) {
        char buf[12];
        itoa(n, buf, 10);
        return print(buf);
    }

    size_t println(int n) {
        size_t written = print(n);
        written += write('\r');
        written += write('\n');
        return written;
    }

    size_t print(float f, int digits = 2) {
        char buf[16];
        dtostrf(f, 0, digits, buf);
        return print(buf);
    }

    size_t println(float f, int digits = 2) {
        size_t n = print(f, digits);
        n += write('\r');
        n += write('\n');
        return n;
    }

    // Flash string support (F() macro)
    size_t print(const __FlashStringHelper* str) {
        if (!str) return 0;
        const char* p = (const char*)str;
        size_t n = 0;
        char c;
        while ((c = pgm_read_byte(p++)) != 0) {
            n += write(c);
        }
        return n;
    }

    size_t println(const __FlashStringHelper* str) {
        size_t n = print(str);
        n += write('\r');
        n += write('\n');
        return n;
    }
};

#endif // TRANSPORT_INTERFACE_H
