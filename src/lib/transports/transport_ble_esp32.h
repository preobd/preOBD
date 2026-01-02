/*
 * transport_ble_esp32.h - ESP32-S3 BLE Transport
 *
 * Wraps ESP32_BleSerial into the TransportInterface abstraction.
 * Provides BLE UART service for ESP32-S3 and other BLE-only variants.
 *
 * Usage:
 *   BLETransportESP32 ble("openEMS");
 *   router.registerTransport(TRANSPORT_ESP32_BT, &ble);
 *
 * Note: Only available on ESP32-S3, ESP32-C3 (BLE-only chips)
 */

#ifndef TRANSPORT_BLE_ESP32_H
#define TRANSPORT_BLE_ESP32_H

#include "../transport_interface.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
#include <BleSerial.h>

class BLETransportESP32 : public TransportInterface {
private:
    BleSerial bleSerial;
    const char* deviceName;
    bool initialized;

public:
    BLETransportESP32(const char* name)
        : deviceName(name), initialized(false) {}

    // ========== TransportInterface Implementation ==========

    size_t write(uint8_t c) override {
        if (!initialized || !bleSerial.connected()) return 0;
        return bleSerial.write(c);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!initialized || !bleSerial.connected()) return 0;
        return bleSerial.write(buffer, size);
    }

    int available() override {
        if (!initialized) return 0;
        return bleSerial.available();
    }

    int read() override {
        if (!initialized) return -1;
        return bleSerial.read();
    }

    int peek() override {
        if (!initialized) return -1;
        return bleSerial.peek();
    }

    void flush() override {
        if (initialized) {
            bleSerial.flush();
        }
    }

    const char* getName() const override {
        return "BLE_ESP32";
    }

    uint8_t getCapabilities() const override {
        return CAP_READ | CAP_WRITE | CAP_BINARY;
    }

    TransportState getState() const override {
        if (!initialized) return TRANSPORT_DISCONNECTED;
        // Cast away const to call non-const connected() method
        return const_cast<BleSerial&>(bleSerial).connected() ? TRANSPORT_CONNECTED : TRANSPORT_DISCONNECTED;
    }

    bool begin() override {
        if (!initialized) {
            bleSerial.begin(deviceName);
            initialized = true;
        }
        return initialized;
    }

    void end() override {
        if (initialized) {
            bleSerial.end();
            initialized = false;
        }
    }

    void update() override {
        // ESP32_BleSerial handles connection management internally
        // No housekeeping needed
    }
};

#endif // ESP32-S3 or ESP32-C3
#endif // TRANSPORT_BLE_ESP32_H
