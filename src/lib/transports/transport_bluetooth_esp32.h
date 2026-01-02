/*
 * transport_bluetooth_esp32.h - ESP32 Bluetooth Classic Transport
 *
 * Wraps ESP32's built-in BluetoothSerial into the TransportInterface
 * abstraction. Provides Bluetooth Classic SPP (Serial Port Profile) support.
 *
 * Usage:
 *   BluetoothTransportESP32 bt("openEMS");
 *   router.registerTransport(TRANSPORT_ESP32_BT, &bt);
 *
 * Note: Only available on original ESP32 (not S3, C3, or other BLE-only variants)
 */

#ifndef TRANSPORT_BLUETOOTH_ESP32_H
#define TRANSPORT_BLUETOOTH_ESP32_H

#include "../transport_interface.h"

// Only compile for original ESP32 with Bluetooth Classic support
#if defined(CONFIG_IDF_TARGET_ESP32) || (defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3))
#include <BluetoothSerial.h>

class BluetoothTransportESP32 : public TransportInterface {
private:
    BluetoothSerial btSerial;
    const char* deviceName;
    bool initialized;

public:
    BluetoothTransportESP32(const char* name)
        : deviceName(name), initialized(false) {}

    // ========== TransportInterface Implementation ==========

    size_t write(uint8_t c) override {
        if (!initialized || !btSerial.hasClient()) return 0;
        return btSerial.write(c);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!initialized || !btSerial.hasClient()) return 0;
        return btSerial.write(buffer, size);
    }

    int available() override {
        if (!initialized) return 0;
        return btSerial.available();
    }

    int read() override {
        if (!initialized) return -1;
        return btSerial.read();
    }

    int peek() override {
        if (!initialized) return -1;
        return btSerial.peek();
    }

    void flush() override {
        if (initialized) {
            btSerial.flush();
        }
    }

    const char* getName() const override {
        return "BT_ESP32";
    }

    uint8_t getCapabilities() const override {
        return CAP_READ | CAP_WRITE | CAP_BINARY;
    }

    TransportState getState() const override {
        if (!initialized) return TRANSPORT_DISCONNECTED;
        // Cast away const to call non-const hasClient() method
        return const_cast<BluetoothSerial&>(btSerial).hasClient() ? TRANSPORT_CONNECTED : TRANSPORT_DISCONNECTED;
    }

    bool begin() override {
        if (!initialized) {
            initialized = btSerial.begin(deviceName);
        }
        return initialized;
    }

    void end() override {
        if (initialized) {
            btSerial.end();
            initialized = false;
        }
    }

    void update() override {
        // ESP32 BluetoothSerial handles connection management internally
        // No housekeeping needed
    }
};

#endif // ESP32
#endif // TRANSPORT_BLUETOOTH_ESP32_H
