# Transport Architecture

This document describes preOBD's multi-transport communication architecture, which enables serial commands and data output across multiple communication channels simultaneously.

## Table of Contents

1. [Overview](#overview)
2. [Architecture Design](#architecture-design)
3. [Transport Interface](#transport-interface)
4. [Registry, console and log](#registry-console-and-log)
5. [Supported Transports](#supported-transports)
6. [Command Routing](#command-routing)
7. [Output](#output)
8. [Future Enhancements](#future-enhancements)
9. [CAN Controller Abstraction](#can-controller-abstraction)
10. [References](#references)

## Overview

### What is the Transport System?

The transport system is an abstraction layer that allows preOBD to communicate over multiple serial channels simultaneously. Instead of being tied to USB Serial only, preOBD can now:

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

1. **Wireless configuration** - Configure preOBD via Bluetooth without USB cable
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
│   msg (core/)  ·  console + log (console/)  ·  outputs     │
│  - msg.control / msg.debug write where console/log say      │
│  - the console pump reads commands from its sources         │
│  - each output writes to its own transport (OUTPUT ADD)     │
│   transport registry (transports/): which transports exist  │
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
3. **One owner per job** - the transport registry knows which transports exist; the console and the log each own their transports; `msg` only writes where they say
4. **Replies go to the sender** - a command is answered on the transport it came from
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

## Registry, console and log

Three pieces share the work (#612 phase 2b-2 split them out of the old `MessageRouter`):

| Piece | File | Job |
|---|---|---|
| Transport registry | `src/transports/transport_registry.{h,cpp}` | which transports exist; `transportGet(id)` only hands out a hardware serial transport whose port has started (#558) |
| Console and log | `src/console/console_routing.{h,cpp}` | the console's and the log's transports (`CONSOLE`/`LOG TRANSPORT`), the command pump, where replies go, the USB fallback |
| `msg` | `src/core/message_api.{h,cpp}` | `msg.control` (the console) and `msg.debug` (the log) write to the sinks main.cpp registers; `msg.filter` is the log filter |

### Registration

At startup (main.cpp), in this order:

```cpp
// Early, beside the other boot-order-sensitive probes
setSerialPortActiveProbe(isSerialPortActive);         // the registry's started-port guard
setMessageSinkResolver(PLANE_CONTROL, consoleSink);   // msg.control -> the console
setMessageSinkResolver(PLANE_DEBUG, logSink);         // msg.debug   -> the log
setOutputConsoleFallbackProbe(consoleOnUsbFallback);  // USB stream outputs pause on the fallback

// Then the transports
transportRegister(TRANSPORT_USB_SERIAL, &usbSerial);

// Every hardware serial transport (Serial1-Serial8); whether each runs is
// BUS SERIAL configuration
#if NUM_SERIAL_PORTS >= 1
transportRegister(TRANSPORT_SERIAL1, &hwSerial1);
#endif
// ... up to Serial8 on Teensy 4.1

#ifdef ESP32
transportRegister(TRANSPORT_ESP32_BT, &btESP32);
#endif

routingApplyConfig();   // the stored CONSOLE/LOG transports and log filter
transportsBegin();      // begin() every registered transport
```

Each loop: `transportsUpdate()` (housekeeping; the framed mux pumps its frames) then `consolePump()` (commands).

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

```cpp
// The console: replies to the command being run, else unsolicited output
msg.control.println("OK");   // the sender while a command runs; else the console's transports

// The log: tagged, filtered by LOG LEVEL / LOG TAG, to the LOG TRANSPORT
msg.debug.info(TAG_SENSOR, "Sensor initialized");
```

Sensor data is not a `msg` stream: RealDash and CSV are outputs, each on its own transport (`OUTPUT ADD`, #612).

## Supported Transports

### USB Serial (All Platforms)

- **Implementation**: `SerialTransportWrapper` around `Serial`
- **Platforms**: All (Teensy, ESP32, Mega, Uno, Due)
- **Baud Rate**: 115200
- **Use Case**: Primary interface for configuration and debugging

**Initialization:**
```cpp
SerialTransportWrapper usbSerial(Serial);
transportRegister(TRANSPORT_USB_SERIAL, &usbSerial);
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
transportRegister(TRANSPORT_SERIAL1, &hwSerial1);
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
transportRegister(TRANSPORT_SERIAL2, &hwSerial2);
#endif
```

### ESP32 Bluetooth Classic

- **Implementation**: `BluetoothTransportESP32` wrapping `BluetoothSerial`
- **Platform**: ESP32 only
- **Device Name**: "preOBD"
- **Use Case**: Wireless RealDash, wireless configuration, wireless logging

**Features:**
- Native Bluetooth Classic (SPP profile)
- No external hardware required
- Automatic pairing
- Transparent serial bridge

**Initialization:**
```cpp
#ifdef ESP32
BluetoothTransportESP32 btESP32("preOBD");
if (btESP32.begin()) {
    transportRegister(TRANSPORT_ESP32_BT, &btESP32);
    msg.debug.info(TAG_BT, "ESP32 Bluetooth initialized");
}
#endif
```

**Connection:**
1. Power on ESP32
2. Pair with "preOBD" from phone/tablet Bluetooth settings
3. Connect from RealDash or serial terminal app

## Command Routing

### How Commands Are Processed

`consolePump()` (console/console_routing.cpp) runs each loop:

1. **Sources, in order:** the console's primary transport, its secondary, then every started framed port (the nRF link is always a console source, so the app can't be cut off).
2. **Per source:** if it has bytes, it becomes the active source, its bytes feed the CLI (`handleCommandInput`), and the buffered command is dispatched (`processSerialCommands`) before the next source.
3. **Replies:** while a command runs, `msg.control` writes to the active source only. Between commands, it writes to the console's primary and secondary.
4. **USB fallback:** if neither console transport is usable (a serial port that was turned off), the console answers on USB (#558). The log never falls back.

All sources feed one CLI/JSON buffer, so a JSON IMPORT from one link can interleave with text from another (#624).

### Multi-Port Simultaneous Control

Setting a secondary console transport allows two ports to share control without either losing connectivity. Replies go to the transport a command came from; unsolicited console output goes to both.

**User command (runtime configurable):**
```
CONSOLE TRANSPORT SERIAL7                 # HM-10 Bluetooth as primary
CONSOLE TRANSPORT USB SECONDARY           # Laptop USB stays active too
SAVE
```

**Clear secondary:**
```
CONSOLE TRANSPORT NONE SECONDARY
SAVE
```

Both ports accept commands independently. A command is answered on the port it came from; unsolicited console output (alarms, notes) goes to both.

### Active Control Transport

The active source is the transport whose command is running; `msg.control` answers on it alone, and it is cleared after each dispatch.

## Output

`msg.control` writes to the active source while a command runs, else to the console's primary and secondary transports; `msg.debug` writes to the log's primary and secondary. If the primary isn't connected, the write is skipped, secondary included (kept as it was).

```cpp
msg.control.println("SET RPM completed");   // to the sender of the command
```

Sensor data goes through outputs, not `msg`: `OUTPUT ADD CSV USB`, `OUTPUT ADD REALDASH SERIAL5` and so on, one format per stream (ARCHITECTURE §14.4). A RealDash output needs its stream to itself, so it can't share USB with the console.

### For Application Code

1. **Use msg.control for commands/config** - Goes to active control transport
2. **Use msg.debug with tags for diagnostics** - Use `msg.debug.error()`, `msg.debug.warn()`, `msg.debug.info()`, `msg.debug.debug()` with appropriate tags
3. **Sensor output is an output** - add one with `OUTPUT ADD`; there is no msg stream for data
4. **Never call Serial directly** - Always use msg APIs

### For Transport Implementations

1. **Handle disconnections gracefully** - Return 0 from `available()` when disconnected
2. **Non-blocking I/O only** - Never block in `read()` or `write()`
3. **Buffer appropriately** - Don't rely on small buffers for large transfers
4. **Report accurate state** - `getState()` must reflect actual connection state

### For Debugging

1. **Check transport state** - Use `LIST TRANSPORTS` and `CONSOLE STATUS`
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

## CAN Controller Abstraction

While the transport system handles serial communication channels (USB, Bluetooth, UART), CAN bus communication uses a separate Hardware Abstraction Layer (HAL).

### CAN HAL Architecture

The CAN HAL provides a unified API regardless of the underlying controller:

```cpp
namespace hal { namespace can {
    bool begin(uint32_t baudrate, uint8_t bus = 0);
    bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus);
    bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus);
    void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus);
}}
```

**Supported Controllers:**
- **FlexCAN** (Teensy 3.6/4.0/4.1 native) - 2-3 buses
- **TWAI** (ESP32/ESP32-S3 native) - 1 bus
- **MCP2515** (External SPI) - 1-2 buses
- **bxCAN** (STM32F4 native) - 1 bus

Platform capabilities are detected at compile-time via `src/hal/platform_caps.h`:
- `PLATFORM_CAN_CONTROLLER` - String identifier ("FlexCAN", "TWAI", "SPI", "bxCAN")
- `PLATFORM_HAS_NATIVE_CAN` - Boolean flag (1 = integrated peripheral, 0 = external)
- `PLATFORM_NEEDS_SPI_CAN` - Boolean flag (1 = requires SPI CAN pins in board.h)

### Relationship to Transport System

The transport system and CAN HAL are independent:
- **Transport system** - Serial communication for commands and RealDash frames
- **CAN HAL** - CAN bus communication for vehicle networks and CAN sensor import

CAN output modules use the CAN HAL to send OBD-II frames and broadcast data, while the transport system handles serial-based communication.

## References

- [TransportInterface API](../../../src/core/transport_interface.h)
- [Transport registry](../../../src/transports/transport_registry.h)
- [Console and log](../../../src/console/console_routing.h)
- [Serial Transport Wrapper](../../../src/transports/transport_serial.h)
- [ESP32 Bluetooth Transport](../../../src/transports/transport_bluetooth_esp32.h)
- [CAN HAL Interface](../../../src/hal/hal_can.h)
- [Platform Capabilities](../../../src/hal/platform_caps.h)
- [Build Configuration Guide](../../guides/configuration/BUILD_CONFIGURATION_GUIDE.md)
- [CAN Transceiver Hardware Guide](../../guides/hardware/CAN_TRANSCEIVER_GUIDE.md)
- [RealDash Setup Guide](../../guides/outputs/REALDASH_SETUP_GUIDE.md)
