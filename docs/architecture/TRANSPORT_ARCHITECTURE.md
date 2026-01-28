# Transport Architecture

This document describes openEMS's multi-transport communication architecture, which enables serial commands and data output across multiple communication channels simultaneously.

## Table of Contents

1. [Overview](#overview)
2. [Architecture Design](#architecture-design)
3. [Transport Interface](#transport-interface)
4. [Message Router](#message-router)
5. [Supported Transports](#supported-transports)
6. [Command Routing](#command-routing)
7. [Output Broadcasting](#output-broadcasting)
8. [Implementation Details](#implementation-details)
9. [Adding New Transports](#adding-new-transports)

## Overview

### What is the Transport System?

The transport system is an abstraction layer that allows openEMS to communicate over multiple serial channels simultaneously. Instead of being tied to USB Serial only, openEMS can now:

- Accept commands from **any** connected transport (USB, Bluetooth, UART)
- Send output to **all** connected transports simultaneously
- Route messages intelligently based on transport priority and state
- Support platform-specific transports (ESP32 Bluetooth, Teensy FlexCAN)

### Why Was This Needed?

**Before (Pre-Transport System):**
- Commands only accepted via USB Serial
- RealDash output only to USB Serial
- Bluetooth required custom workarounds
- No way to use multiple serial ports for commands

**After (With Transport System):**
- Commands accepted from USB, Bluetooth, Serial1, Serial2
- RealDash broadcasts to all connected devices
- Clean abstraction for platform-specific features
- Easy to add new transports (WiFi, BLE, etc.)

### Key Benefits

1. **Wireless configuration** - Configure openEMS via Bluetooth without USB cable
2. **Multi-device output** - View RealDash on phone via Bluetooth while logging to PC via USB
3. **Platform flexibility** - ESP32 gets native Bluetooth, Teensy uses UART modules
4. **Future-proof** - Easy to add WiFi, Ethernet, BLE, or other transports

## Architecture Design

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│                       Application Layer                      │
│  (main.cpp, serial_config.cpp, output_realdash.cpp, etc.)  │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────────┐
│                      Message Router                          │
│  - Manages all registered transports                        │
│  - Polls for incoming commands                              │
│  - Routes output to all connected transports                │
│  - Provides msg.control, msg.debug, msg.data APIs           │
└───────────────────────┬─────────────────────────────────────┘
                        │
         ┌──────────────┼──────────────┬──────────────┐
         ▼              ▼              ▼              ▼
┌────────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
│ USB Serial     │ │ Serial1    │ │ Serial2    │ │ ESP32 BT    │
│ Transport      │ │ Transport  │ │ Transport  │ │ Transport   │
└────────────────┘ └────────────┘ └────────────┘ └─────────────┘
         │              │              │              │
         ▼              ▼              ▼              ▼
┌────────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
│ USB Serial     │ │ Hardware   │ │ Hardware   │ │ Bluetooth   │
│ (CDC)          │ │ UART       │ │ UART       │ │ Serial      │
└────────────────┘ └────────────┘ └────────────┘ └─────────────┘
```

### Design Principles

1. **Abstract Interface** - All transports implement `TransportInterface`
2. **Zero External Dependencies** - Transports don't know about each other
3. **Centralized Routing** - MessageRouter handles all message distribution
4. **Priority-Based Selection** - USB Serial takes priority for control responses
5. **Platform Agnostic** - Application code doesn't know which transport is used

## Transport Interface

All transports implement the `TransportInterface` abstract base class:

```cpp
class TransportInterface {
public:
    // Lifecycle
    virtual bool begin() = 0;        // Initialize the transport
    virtual void end() = 0;          // Shutdown the transport

    // State
    virtual TransportState getState() const = 0;  // Connection state

    // I/O
    virtual int available() = 0;     // Bytes available to read
    virtual int read() = 0;          // Read one byte
    virtual size_t write(uint8_t) = 0;  // Write one byte
    virtual size_t write(const uint8_t* buffer, size_t size) = 0;  // Write buffer
    virtual int peek() = 0;          // Peek at next byte without consuming
    virtual void flush() = 0;        // Flush output buffer
};
```

### Transport States

```cpp
enum TransportState {
    TRANSPORT_DISCONNECTED,  // Not connected (no client)
    TRANSPORT_CONNECTED,     // Connected and ready
    TRANSPORT_ERROR          // Error state
};
```

## Message Router

The `MessageRouter` class is the central hub for all communications.

### Registration

Transports are registered at startup:

```cpp
MessageRouter router;

// Register transports
router.registerTransport(TRANSPORT_USB_SERIAL, &usbSerial);

// Register all available hardware serial transports (Serial1-Serial8)
// Actual initialization is controlled by BUS SERIAL command configuration
#if NUM_SERIAL_PORTS >= 1
router.registerTransport(TRANSPORT_SERIAL1, &hwSerial1);
#endif
#if NUM_SERIAL_PORTS >= 2
router.registerTransport(TRANSPORT_SERIAL2, &hwSerial2);
#endif
// ... up to Serial8 on Teensy 4.1

#ifdef ESP32
router.registerTransport(TRANSPORT_ESP32_BT, &btESP32);
#endif

router.begin();  // Initialize all transports
```

### Transport IDs

```cpp
enum TransportID {
    TRANSPORT_NONE = 0,
    TRANSPORT_USB_SERIAL = 1,   // Serial (USB)
    TRANSPORT_SERIAL1 = 2,      // Hardware Serial1
    TRANSPORT_SERIAL2 = 3,      // Hardware Serial2
    TRANSPORT_SERIAL3 = 4,      // Hardware Serial3
    TRANSPORT_SERIAL4 = 5,      // Hardware Serial4
    TRANSPORT_SERIAL5 = 6,      // Hardware Serial5
    TRANSPORT_SERIAL6 = 7,      // Hardware Serial6
    TRANSPORT_SERIAL7 = 8,      // Hardware Serial7
    TRANSPORT_SERIAL8 = 9,      // Hardware Serial8 (Teensy 4.1 only)
    TRANSPORT_ESP32_BT = 10,    // ESP32 built-in Bluetooth Classic
    NUM_TRANSPORTS = 11
};
```

Serial ports 3-8 availability depends on platform. Use `BUS SERIAL` command to enable/disable ports and configure baud rates.

### Message APIs

The router provides three message channels:

```cpp
// Control messages (commands, responses, configuration)
msg.control.println("OK");  // Goes to active command transport

// Debug messages (diagnostic info, warnings, errors) - use tagged logging
msg.debug.info(TAG_SENSOR, "Sensor initialized");  // Goes to debug transport (usually USB)

// Data messages (sensor values, RealDash frames, CSV output)
msg.data.write(buffer, size);  // Broadcasts to all connected transports
```

### Priority System

The router maintains a priority order for transport selection:

1. **USB Serial** - Highest priority (always preferred for control)
2. **Bluetooth** - Second priority
3. **Serial1** - Third priority
4. **Serial2** - Lowest priority

When multiple transports have pending commands, USB Serial is processed first.

## Supported Transports

### USB Serial (All Platforms)

- **Implementation**: `SerialTransportWrapper` around `Serial`
- **Platforms**: All (Teensy, ESP32, Mega, Uno, Due)
- **Baud Rate**: 115200
- **Use Case**: Primary interface for configuration and debugging

**Initialization:**
```cpp
SerialTransportWrapper usbSerial(Serial);
router.registerTransport(TRANSPORT_USB_SERIAL, &usbSerial);
```

### Hardware Serial 1 (Teensy, AVR, Mega)

- **Implementation**: `SerialTransportWrapper` around `Serial1`
- **Platforms**: Teensy, Mega, Due, AVR
- **Baud Rate**: 115200
- **Use Case**: UART Bluetooth modules (HC-05, HM-10), external devices

**Pins:**
- Teensy 4.1: TX1 (pin 1), RX1 (pin 0)
- Arduino Mega: TX1 (pin 18), RX1 (pin 19)

**Initialization:**
```cpp
#if defined(__MK66FX1M0__) || defined(__IMXRT1062__) || defined(__AVR_ATmega2560__)
Serial1.begin(115200);
SerialTransportWrapper hwSerial1(Serial1);
router.registerTransport(TRANSPORT_SERIAL1, &hwSerial1);
#endif
```

### Hardware Serial 2 (Teensy, AVR, Mega)

- **Implementation**: `SerialTransportWrapper` around `Serial2`
- **Platforms**: Teensy, Mega, Due, AVR
- **Baud Rate**: 9600 (default for HC-05/HM-10 modules)
- **Use Case**: Bluetooth modules, secondary UART devices

**Pins:**
- Teensy 4.1: TX2 (pin 8), RX2 (pin 7)
- Arduino Mega: TX2 (pin 16), RX2 (pin 17)

**Initialization:**
```cpp
#if defined(__MK66FX1M0__) || defined(__IMXRT1062__) || defined(__AVR_ATmega2560__)
Serial2.begin(9600);  // HC-05/HM-10 default baud
SerialTransportWrapper hwSerial2(Serial2);
router.registerTransport(TRANSPORT_SERIAL2, &hwSerial2);
#endif
```

### ESP32 Bluetooth Classic

- **Implementation**: `BluetoothTransportESP32` wrapping `BluetoothSerial`
- **Platform**: ESP32 only
- **Device Name**: "openEMS"
- **Use Case**: Wireless RealDash, wireless configuration, wireless logging

**Features:**
- Native Bluetooth Classic (SPP profile)
- No external hardware required
- Automatic pairing
- Transparent serial bridge

**Initialization:**
```cpp
#ifdef ESP32
BluetoothTransportESP32 btESP32("openEMS");
if (btESP32.begin()) {
    router.registerTransport(TRANSPORT_ESP32_BT, &btESP32);
    msg.debug.info(TAG_BT, "ESP32 Bluetooth initialized");
}
#endif
```

**Connection:**
1. Power on ESP32
2. Pair with "openEMS" from phone/tablet Bluetooth settings
3. Connect from RealDash or serial terminal app

## Command Routing

### How Commands Are Processed

1. **Polling** - `router.pollForCommands()` checks all transports
2. **Detection** - First transport with `available() > 0` is selected
3. **Activation** - Selected transport becomes "active control transport"
4. **Processing** - Commands are read and processed from active transport
5. **Response** - Responses go back to the transport that sent the command

**Example:**
```cpp
void loop() {
    // Poll all transports for commands
    if (router.pollForCommands()) {
        // A command is ready from some transport
        String cmd = router.readCommand();

        // Process command
        processCommand(cmd);

        // Response automatically goes to the transport that sent the command
        msg.control.println("OK");
    }
}
```

### Active Control Transport

Only one transport can be the "active control transport" at a time. This ensures:
- Commands don't get mixed between transports
- Responses go to the correct requester
- Priority is respected (USB beats Bluetooth)

**Switching Control:**
```cpp
// User sends command via Bluetooth
router.pollForCommands();  // Detects Bluetooth has data
// Bluetooth becomes active control transport

msg.control.println("OK");  // Goes to Bluetooth

// User sends command via USB
router.pollForCommands();  // Detects USB has data (higher priority)
// USB becomes active control transport

msg.control.println("OK");  // Goes to USB
```

## Output Broadcasting

### Broadcast vs. Unicast

- **Unicast (Control)** - Goes to active control transport only
- **Broadcast (Data)** - Goes to all connected transports

**Unicast Example:**
```cpp
msg.control.println("SET RPM completed");
// Only goes to the transport that sent the SET RPM command
```

**Broadcast Example:**
```cpp
msg.data.println("CHT,EGT,OilP,OilT");  // CSV header
msg.data.print("150,");  // CHT
msg.data.print("450,");  // EGT
msg.data.println("4.2,110");  // Oil pressure and temp
// Goes to USB Serial, Bluetooth, Serial1, Serial2 (all connected transports)
```

### RealDash Broadcasting

RealDash frames are broadcast to all transports:

```cpp
void sendRealDashFrame(uint32_t pid, const uint8_t* data, size_t len) {
    uint8_t frame[16];
    // ... build frame ...

    // Broadcast to all connected transports
    msg.data.write(frame, 16);
    // Now visible on USB, Bluetooth, Serial1, Serial2 simultaneously
}
```

This allows:
- RealDash on phone via Bluetooth
- Simultaneous data logging on PC via USB
- External device monitoring via Serial1

## Implementation Details

### File Structure

```
src/lib/transports/
├── transport_interface.h          # Abstract TransportInterface
├── message_router.h/.cpp          # MessageRouter implementation
├── serial_transport_wrapper.h     # Wrapper for Serial/Serial1/Serial2
└── transport_bluetooth_esp32.h    # ESP32 Bluetooth transport
```

### Memory Footprint

| Component | Flash (bytes) | RAM (bytes) |
|-----------|---------------|-------------|
| TransportInterface (vtable) | ~100 | 4 per instance |
| MessageRouter | ~2KB | ~256 |
| SerialTransportWrapper | ~500 | 8 per instance |
| BluetoothTransportESP32 | ~1KB | ~32 |
| **Total Overhead** | **~4KB** | **~300** |

### Performance

- **Command latency**: <1ms (polling every loop)
- **Broadcast overhead**: ~10μs per transport
- **Buffer size**: Uses underlying transport buffers (typically 64-256 bytes)

### Thread Safety

The transport system is **not thread-safe**. All operations must occur on the main loop thread. This is acceptable because:
- Arduino framework is single-threaded
- All I/O happens in `loop()`
- No concurrent access to transports

## Adding New Transports

### Step 1: Implement TransportInterface

Create a new class that implements `TransportInterface`:

```cpp
// transport_wifi_esp32.h
#ifdef ESP32
#include <WiFi.h>
#include "transport_interface.h"

class WiFiTransportESP32 : public TransportInterface {
private:
    WiFiClient client;
    WiFiServer server;

public:
    WiFiTransportESP32(uint16_t port) : server(port) {}

    bool begin() override {
        WiFi.begin("SSID", "password");
        server.begin();
        return true;
    }

    void end() override {
        server.end();
    }

    TransportState getState() const override {
        if (!WiFi.isConnected()) return TRANSPORT_DISCONNECTED;
        return client.connected() ? TRANSPORT_CONNECTED : TRANSPORT_DISCONNECTED;
    }

    int available() override {
        if (!client || !client.connected()) {
            client = server.available();
        }
        return client ? client.available() : 0;
    }

    int read() override { return client.read(); }
    int peek() override { return client.peek(); }
    size_t write(uint8_t b) override { return client.write(b); }
    size_t write(const uint8_t* buf, size_t size) override {
        return client.write(buf, size);
    }
    void flush() override { client.flush(); }
};
#endif
```

### Step 2: Add Transport ID

Add to `TransportID` enum in [transport_interface.h](../../src/lib/transports/transport_interface.h):

```cpp
enum TransportID {
    // ... existing IDs ...
    TRANSPORT_WIFI,
    TRANSPORT_MAX
};
```

### Step 3: Register in main.cpp

Register the transport at startup:

```cpp
#ifdef ESP32
WiFiTransportESP32 wifiTransport(23);  // Port 23 (telnet)
#endif

void setup() {
    // ... existing registrations ...

    #ifdef ESP32
    if (wifiTransport.begin()) {
        router.registerTransport(TRANSPORT_WIFI, &wifiTransport);
        msg.debug.info(TAG_WIFI, "ESP32 WiFi initialized");
    }
    #endif
}
```

### Step 4: Test

1. Build and flash
2. Connect via new transport (e.g., telnet to ESP32 IP)
3. Send commands - should work like USB Serial
4. Verify RealDash frames broadcast to new transport

### Example: BLE Transport

```cpp
// transport_ble_esp32.h
#ifdef ESP32
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "transport_interface.h"

class BLETransportESP32 : public TransportInterface {
private:
    BLECharacteristic* pCharacteristic;
    bool deviceConnected = false;

public:
    BLETransportESP32(const char* deviceName) {
        BLEDevice::init(deviceName);
        BLEServer* pServer = BLEDevice::createServer();
        // ... BLE setup ...
    }

    TransportState getState() const override {
        return deviceConnected ? TRANSPORT_CONNECTED : TRANSPORT_DISCONNECTED;
    }

    // ... implement other methods ...
};
#endif
```

## Best Practices

### For Application Code

1. **Use msg.control for commands/config** - Goes to active control transport
2. **Use msg.debug with tags for diagnostics** - Use `msg.debug.error()`, `msg.debug.warn()`, `msg.debug.info()`, `msg.debug.debug()` with appropriate tags
3. **Use msg.data for sensor output** - Broadcasts to all transports
4. **Never call Serial directly** - Always use msg APIs

### For Transport Implementations

1. **Handle disconnections gracefully** - Return 0 from `available()` when disconnected
2. **Non-blocking I/O only** - Never block in `read()` or `write()`
3. **Buffer appropriately** - Don't rely on small buffers for large transfers
4. **Report accurate state** - `getState()` must reflect actual connection state

### For Debugging

1. **Check transport state** - Use `LIST TRANSPORTS` command (if implemented)
2. **Monitor tagged debug logs** - Connect USB Serial and use appropriate log tags
3. **Test one transport at a time** - Verify each works independently
4. **Test simultaneous connections** - Verify broadcasting works

## Future Enhancements

### Planned Features

- **Transport statistics** - Bytes sent/received per transport
- **Transport filtering** - Control which outputs go to which transports
- **Dynamic transport discovery** - Auto-detect and register transports
- **Transport prioritization** - User-configurable priority order
- **Command queuing** - Queue commands from multiple transports
- **Flow control** - Backpressure handling for slow transports

### Potential Transports

- **WiFi TCP/IP** - Telnet server on ESP32
- **WiFi UDP** - Fast broadcast protocol
- **Ethernet** - Teensy 4.1 with Ethernet library
- **BLE (Bluetooth Low Energy)** - Lower power alternative to Bluetooth Classic
- **LoRa** - Long-range wireless for remote monitoring
- **WebSocket** - Web-based dashboard

## References

- [TransportInterface API](../../src/lib/transports/transport_interface.h)
- [MessageRouter Implementation](../../src/lib/transports/message_router.h)
- [Serial Transport Wrapper](../../src/lib/transports/serial_transport_wrapper.h)
- [ESP32 Bluetooth Transport](../../src/lib/transports/transport_bluetooth_esp32.h)
- [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md)
- [RealDash Setup Guide](../guides/outputs/REALDASH_SETUP_GUIDE.md)
