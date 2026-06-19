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

#include <Arduino.h>

#include "transport_interface.h"
#include "watchdog.h"

class SerialTransport : public TransportInterface {
private:
    Stream* serial;          // Pointer to Serial, Serial1, Serial2, etc.
    const char* name;
    uint32_t baudRate;
    int16_t flowInPin;       // GPIO read before write; HIGH = peer wants pause.
                             // -1 disables. Used by the Phase 3 nRF coprocessor
                             // link (Teensy 4.x Serial1 has no HW CTS, so we
                             // software-pace).

    // Chunk size for bulk writes -- we re-check the flow-control pin between
    // chunks so a peer can throttle us partway through a long response. At
    // 115200 baud, 32 bytes = ~2.8ms wire time, comfortably below the nRF's
    // 100ms notify-stall window while keeping overhead low.
    static constexpr size_t kFlowChunkBytes = 32;

    // Wait for the flow-control pin to deassert. Blocks indefinitely while
    // the peer asserts the line -- a timed fail-open would dump bytes into
    // the peer's already-full buffer and cause exactly the bulk-response
    // corruption Phase 3 is meant to prevent. The watchdog is fed every
    // spin so an extended wait can't trip the WDT (the usual feeder,
    // WatchdogKickingPrint, only runs after a successful byte batch and
    // never sees the inside of this wait).
    //
    // Safety: if the line is genuinely stuck (hardware fault) the Teensy
    // halts here -- a strictly better tradeoff than silent data corruption.
    // User power-cycles to recover.
    void awaitFlowClear() {
        if (flowInPin < 0) return;
        while (digitalRead(flowInPin) == HIGH) {
            watchdogReset();
            yield();
        }
    }

public:
    SerialTransport(Stream* serialPort, const char* transportName,
                    uint32_t baud = 115200, int16_t flow_in_pin = -1)
        : serial(serialPort), name(transportName), baudRate(baud),
          flowInPin(flow_in_pin) {}

    // ========== TransportInterface Implementation ==========

    size_t write(uint8_t c) override {
        awaitFlowClear();
        return serial->write(c);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        // When flow control is disabled, fall through to a single write so
        // the underlying driver can use its most efficient path.
        if (flowInPin < 0) {
            return serial->write(buffer, size);
        }
        // With flow control active, break the write into small chunks and
        // re-check the gate between each. A single awaitFlowClear() before
        // a multi-KB buffer isn't enough -- the peer can assert mid-stream
        // and we need to honor it before the rest of the buffer drains.
        size_t written = 0;
        while (written < size) {
            awaitFlowClear();
            const size_t remaining = size - written;
            const size_t chunk = remaining < kFlowChunkBytes
                                     ? remaining : kFlowChunkBytes;
            const size_t n = serial->write(buffer + written, chunk);
            if (n == 0) break;
            written += n;
        }
        return written;
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
