/*
 * transport_serial.h - Hardware Serial Transport Wrapper
 *
 * Wraps Arduino Serial, Serial1, Serial2, etc. into the TransportInterface
 * abstraction. Provides unified access to hardware UARTs.
 *
 * Usage:
 *   SerialTransport usb(&Serial, "USB", 115200);
 *   SerialTransport hw1(&Serial1, "SERIAL1", 115200);
 */

#ifndef TRANSPORT_SERIAL_H
#define TRANSPORT_SERIAL_H

#include "transport_interface.h"

class SerialTransport : public TransportInterface {
private:
    Stream* serial;       // Pointer to Serial, Serial1, Serial2, etc.
    const char* name;
    uint32_t baudRate;

public:
    SerialTransport(Stream* serialPort, const char* transportName, uint32_t baud = 115200)
        : serial(serialPort), name(transportName), baudRate(baud) {}

    // ========== TransportInterface Implementation ==========

    size_t write(uint8_t c) override {
        return serial->write(c);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        return serial->write(buffer, size);
    }

    int available() override {
        return serial->available();
    }

    int read() override {
        return serial->read();
    }

    int peek() override {
        return serial->peek();
    }

    void flush() override {
        serial->flush();
    }

    const char* getName() const override {
        return name;
    }

    uint8_t getCapabilities() const override {
        return CAP_READ | CAP_WRITE | CAP_BINARY | CAP_HARDWARE_SERIAL;
    }

    TransportState getState() const override {
        // Hardware serial is always "connected" once initialized
        return TRANSPORT_CONNECTED;
    }

    bool begin() override {
        // Serial.begin() is handled by platform initialization in main.cpp
        // This transport just wraps an already-initialized Serial object
        return true;
    }

    void end() override {
        // Don't actually close Serial - it might be needed by other code
    }

    void update() override {
        // No housekeeping needed for hardware serial
    }
};

#endif // TRANSPORT_SERIAL_H
