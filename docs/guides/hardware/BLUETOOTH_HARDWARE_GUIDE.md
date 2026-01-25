# Bluetooth Hardware Setup Guide

This guide covers hardware setup for Bluetooth modules used with openEMS, including voltage level considerations, wiring diagrams, and configuration.

## Table of Contents

1. [Overview](#overview)
2. [Bluetooth Module Options](#bluetooth-module-options)
3. [Voltage Level Requirements](#voltage-level-requirements)
4. [Wiring for 5V Platforms](#wiring-for-5v-platforms)
5. [Wiring for 3.3V Platforms](#wiring-for-33v-platforms)
6. [Module Configuration](#module-configuration)
7. [Troubleshooting](#troubleshooting)

## Overview

openEMS supports two types of Bluetooth connectivity:

1. **ESP32 Native Bluetooth Classic** - Built-in, no external hardware
2. **UART Bluetooth Modules** - HC-05, HM-10, or similar for Teensy/Mega/AVR

This guide focuses on **UART Bluetooth modules** for platforms that don't have built-in Bluetooth.

## Bluetooth Module Options

### HC-05 (Bluetooth Classic / SPP)

- **Protocol**: Bluetooth 2.0/2.1 Classic (SPP profile)
- **Range**: ~10 meters (33 feet)
- **Voltage**: 3.3V logic (VCC can be 3.6-6V)
- **Default Baud**: 9600 or 38400 (configurable via AT commands)
- **Cost**: ~$5-8
- **Pros**: Widely available, well-documented, easy to configure
- **Cons**: Higher power consumption than BLE

**Recommended for**: RealDash, serial configuration, data logging

### HM-10 (Bluetooth Low Energy / BLE 4.0)

- **Protocol**: Bluetooth 4.0 BLE
- **Range**: ~50-100 meters (165-330 feet) in open air
- **Voltage**: 3.3V logic and VCC (NOT 5V tolerant!)
- **Default Baud**: 9600 (configurable via AT commands)
- **Cost**: ~$3-6
- **Pros**: Long range, low power, good for battery applications
- **Cons**: Requires BLE-compatible phone/tablet

**Recommended for**: Battery-powered installations, long-range monitoring

### HC-06 (Bluetooth Classic / SPP - Slave Only)

- **Protocol**: Bluetooth 2.0 Classic (SPP profile)
- **Range**: ~10 meters (33 feet)
- **Voltage**: 3.3V logic (VCC can be 3.6-6V)
- **Default Baud**: 9600
- **Cost**: ~$4-7
- **Pros**: Simpler than HC-05, cheaper
- **Cons**: Slave-only (can't initiate connections), harder to configure

**Recommended for**: Simple RealDash setups where configuration isn't needed

## Voltage Level Requirements

### Critical Warning: Voltage Compatibility

⚠️ **Most Bluetooth modules use 3.3V logic and are NOT 5V tolerant on RX pin!**

Connecting a 5V TX signal directly to a 3.3V Bluetooth module RX pin **will damage the module**.

| Platform | TX Voltage | Bluetooth RX Tolerance | Level Shifter Needed? |
|----------|------------|------------------------|----------------------|
| **Teensy 4.x** | 3.3V | 3.3V | ✅ No - direct connection safe |
| **Teensy 3.x** | 3.3V | 3.3V | ✅ No - direct connection safe |
| **Arduino Mega** | 5V | 3.3V | ❌ Yes - voltage divider required |
| **Arduino Uno** | 5V | 3.3V | ❌ Yes - voltage divider required |
| **Arduino Due** | 3.3V | 3.3V | ✅ No - direct connection safe |
| **ESP32** | 3.3V | N/A | N/A - use built-in Bluetooth instead |

### Module-Specific Voltage Tolerance

| Module | VCC Supply | Logic Level | RX 5V Tolerant? | TX Output Level |
|--------|------------|-------------|-----------------|-----------------|
| **HC-05** | 3.6-6V | 3.3V | ❌ No | 3.3V |
| **HC-06** | 3.6-6V | 3.3V | ❌ No | 3.3V |
| **HM-10** | 3.3V only | 3.3V | ❌ No | 3.3V |

**Key Points:**
- HC-05/HC-06 can be powered from 5V (VCC pin), but logic is still 3.3V
- HM-10 must be powered from 3.3V on both VCC and logic
- None of these modules have 5V-tolerant RX pins

## Wiring for 5V Platforms

### Arduino Mega / Uno with HC-05/HC-06

**Required Components:**
- HC-05 or HC-06 module
- 10kΩ resistor
- 20kΩ resistor (or two 10kΩ in series)
- Breadboard and jumper wires

**Voltage Divider Calculation:**

The RX pin needs 3.3V max, but Mega/Uno TX is 5V. Use a resistor divider:

```
Arduino TX (5V) ---[10kΩ]---+--- Bluetooth RX (3.3V)
                            |
                          [20kΩ]
                            |
                           GND
```

Output voltage: `5V × (20kΩ / (10kΩ + 20kΩ)) = 3.33V` ✅

**Alternative resistor values:**
- 1kΩ + 2kΩ = 3.33V
- 4.7kΩ + 10kΩ = 3.40V (slightly high but safe)
- 2.2kΩ + 4.7kΩ = 3.41V (slightly high but safe)

**Avoid resistors too high (>100kΩ total)** - signal may become unreliable.

**Connection Diagram - Mega with HC-05 on Serial2:**

```
Arduino Mega 2560                       HC-05 Module
┌─────────────────┐                    ┌──────────────┐
│                 │                    │              │
│   5V        ────┼────────────────────┼─── VCC      │
│   GND       ────┼────────────────────┼─── GND      │
│                 │                    │              │
│   TX2 (16)  ────┼───[10kΩ]───+───────┼─── RX       │
│                 │            │       │              │
│                 │          [20kΩ]    │              │
│                 │            │       │              │
│                 │           GND      │              │
│                 │                    │              │
│   RX2 (17)  ────┼────────────────────┼─── TX       │
│                 │                    │              │
└─────────────────┘                    └──────────────┘
```

**Pin Assignments:**
- Arduino TX2 (pin 16) → 10kΩ → 20kΩ to GND → HC-05 RX
- Arduino RX2 (pin 17) → HC-05 TX (direct connection - 3.3V is safe for 5V input)
- Arduino 5V → HC-05 VCC
- Arduino GND → HC-05 GND

### Arduino Mega / Uno with HM-10

**Important:** HM-10 VCC is 3.3V only! Do NOT connect to 5V or you will destroy the module.

**Connection Diagram - Mega with HM-10 on Serial2:**

```
Arduino Mega 2560                       HM-10 Module
┌─────────────────┐                    ┌──────────────┐
│                 │                    │              │
│   3.3V      ────┼────────────────────┼─── VCC      │  ⚠️ 3.3V only!
│   GND       ────┼────────────────────┼─── GND      │
│                 │                    │              │
│   TX2 (16)  ────┼───[10kΩ]───+───────┼─── RX       │
│                 │            │       │              │
│                 │          [20kΩ]    │              │
│                 │            │       │              │
│                 │           GND      │              │
│                 │                    │              │
│   RX2 (17)  ────┼────────────────────┼─── TX       │
│                 │                    │              │
└─────────────────┘                    └──────────────┘
```

**Critical Difference:** VCC connected to 3.3V, NOT 5V!

## Wiring for 3.3V Platforms

### Teensy 4.1 / 4.0 with HC-05/HC-06

**Good news:** Teensy 4.x uses 3.3V logic, so **no voltage divider needed!** Direct connection is safe.

**Connection Diagram - Teensy 4.1 with HC-05 on Serial2:**

```
Teensy 4.1                             HC-05 Module
┌─────────────────┐                    ┌──────────────┐
│                 │                    │              │
│   VUSB (5V) ────┼────────────────────┼─── VCC      │  (or use 3.3V)
│   GND       ────┼────────────────────┼─── GND      │
│                 │                    │              │
│   TX2 (8)   ────┼────────────────────┼─── RX       │  ✅ Direct - 3.3V safe
│   RX2 (7)   ────┼────────────────────┼─── TX       │  ✅ Direct - 3.3V safe
│                 │                    │              │
└─────────────────┘                    └──────────────┘
```

**Pin Assignments (Teensy 4.1):**
- Serial1: TX1 (pin 1), RX1 (pin 0)
- Serial2: TX2 (pin 8), RX2 (pin 7)

**Power Options:**
- HC-05/HC-06: Can use 5V (VUSB) or 3.3V (3V3 pin)
- HM-10: Must use 3.3V (3V3 pin) only

### Teensy 4.1 / 4.0 with HM-10

**Connection Diagram - Teensy 4.1 with HM-10 on Serial2:**

```
Teensy 4.1                             HM-10 Module
┌─────────────────┐                    ┌──────────────┐
│                 │                    │              │
│   3V3       ────┼────────────────────┼─── VCC      │  ⚠️ 3.3V only!
│   GND       ────┼────────────────────┼─── GND      │
│                 │                    │              │
│   TX2 (8)   ────┼────────────────────┼─── RX       │  ✅ Direct - 3.3V safe
│   RX2 (7)   ────┼────────────────────┼─── TX       │  ✅ Direct - 3.3V safe
│                 │                    │              │
└─────────────────┘                    └──────────────┘
```

## Module Configuration

### Default Settings (Usually Work As-Is)

Most HC-05/HC-06/HM-10 modules come pre-configured with settings that work with openEMS:

- **Baud Rate**: 9600 (matches Serial2 default in openEMS)
- **Device Name**: "HC-05" or "HM-10" or similar
- **PIN/Password**: "1234" or "0000"

**For most users:** Just wire it up and it should work immediately.

### Changing Settings (Optional)

If you need to change baud rate or device name, use AT commands.

#### HC-05 Configuration Mode

1. **Enter AT mode:**
   - Disconnect VCC from HC-05
   - Hold the button on HC-05 (or bridge KEY pin to VCC)
   - Reconnect VCC while holding button
   - LED should blink slowly (~2 second intervals)

2. **Connect to Arduino Serial:**
   - Wire HC-05 TX/RX to Arduino Serial1 or use SoftwareSerial
   - Open Arduino Serial Monitor at **38400 baud** (AT mode baud)

3. **Send AT commands** (must end with `\r\n` - set "Both NL & CR" in Serial Monitor):

```
AT                          // Test connection, returns "OK"
AT+NAME=openEMS            // Change device name
AT+UART=115200,0,0         // Change baud to 115200
AT+PSWD=1234               // Change PIN
AT+ROLE=0                  // Set as slave (default)
```

4. **Exit AT mode:** Power cycle the module

#### HM-10 Configuration

HM-10 doesn't need special mode - just send AT commands at default 9600 baud:

```
AT                          // Test connection, returns "OK"
AT+NAME=openEMS            // Change device name (max 12 chars)
AT+BAUD=4                  // Set baud to 115200 (4 = 115200)
AT+PASS=123456             // Change PIN (6 digits)
AT+RESET                   // Reset to apply changes
```

**HM-10 Baud Rate Codes:**
- 0 = 9600
- 1 = 19200
- 2 = 38400
- 3 = 57600
- 4 = 115200
- 5 = 4800
- 6 = 2400

### Recommended Settings for openEMS

**Option 1: Keep Default 9600 Baud (Easiest)**
- No configuration needed
- Works with openEMS Serial2 default (9600 baud)
- Sufficient for RealDash (updates at 10Hz)

**Option 2: Upgrade to 115200 Baud (Better Performance)**
- Configure module to 115200 baud using AT commands
- Configure openEMS serial port to match:
  ```
  BUS SERIAL 2 ENABLE 115200     # Enable Serial2 at 115200 baud
  SAVE                           # Persist to EEPROM
  ```
- Faster RealDash updates, more responsive commands

**Using Different Serial Ports (Teensy 4.x)**

Teensy 4.x supports up to 8 serial ports. You can use any available port for Bluetooth:
```
BUS SERIAL 5 ENABLE 9600         # Enable Serial5 for Bluetooth
TRANSPORT DATA SERIAL5           # Route data output to Serial5
SAVE
```

See [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#bus-configuration) for complete serial port configuration.

## Troubleshooting

### Module Not Pairing

**Symptoms:**
- Module not visible in Bluetooth device list
- Can't pair from phone/tablet

**Solutions:**
1. **Check power:**
   - LED should be blinking (HC-05/HC-06: fast blink = unpaired, slow blink = paired)
   - Measure VCC with multimeter (should be 3.3V for HM-10, 3.6-6V for HC-05/HC-06)

2. **Check voltage:**
   - HM-10 damaged if powered from 5V - replace module
   - HC-05/HC-06 can use 5V on VCC

3. **Reset module:**
   - Power cycle (disconnect/reconnect VCC)
   - Try factory reset via AT commands

4. **Check range:**
   - Move phone closer (within 1 meter during pairing)
   - Remove obstacles between phone and module

### No Data in RealDash

**Symptoms:**
- Bluetooth pairs successfully
- RealDash connects but shows "No Data" or "---"

**Solutions:**
1. **Check wiring:**
   - Verify TX/RX not swapped (Arduino TX → BT RX, Arduino RX → BT TX)
   - Check voltage divider if using 5V platform
   - Verify GND connection

2. **Check baud rate mismatch:**
   - Module baud must match Serial2 initialization (default 9600)
   - Use AT commands to verify/change module baud
   - Or change Serial2.begin() to match module

3. **Test with USB first:**
   - Verify RealDash works over USB
   - Confirms openEMS is sending data correctly
   - Then debug Bluetooth separately

4. **Check Serial2 initialization:**
   - Verify Serial2 is enabled: `BUS SERIAL 2` to check status
   - Enable if needed: `BUS SERIAL 2 ENABLE 9600`
   - Check that Serial2 is registered as transport
   - Look for "Serial2 initialized @ 9600 baud" debug message

### Garbled Data / Corrupted Messages

**Symptoms:**
- Random characters in Serial Monitor
- RealDash shows incorrect values or garbage

**Solutions:**
1. **Baud rate mismatch:**
   - Module baud and Serial2.begin() baud must match exactly
   - Try common rates: 9600, 19200, 38400, 115200

2. **Weak voltage divider signal:**
   - If using voltage divider, check resistor values
   - Resistor total too high (>100kΩ) can cause signal degradation
   - Try lower resistor values (1kΩ + 2kΩ instead of 10kΩ + 20kΩ)

3. **Electrical noise:**
   - Add 0.1μF capacitor between VCC and GND on module
   - Keep wires short (<15cm)
   - Route away from high-current wires (injectors, ignition, motors)

### Module Damaged After Connection

**Likely cause:** 5V applied to 3.3V-only pin

**HM-10 Damage Scenarios:**
- ❌ VCC connected to 5V (module destroyed - must replace)
- ❌ 5V TX connected to RX without voltage divider (RX pin damaged)

**HC-05/HC-06 Damage Scenarios:**
- ❌ 5V TX connected to RX without voltage divider (RX pin damaged)
- Note: HC-05/HC-06 VCC can handle 5V safely

**Prevention:**
- Always check module datasheet for voltage limits
- Always use voltage divider on 5V platforms
- Double-check wiring before applying power

## Hardware Checklist

Before powering on:

- [ ] Verify module VCC voltage (3.3V for HM-10, 3.3-6V for HC-05/HC-06)
- [ ] Check if voltage divider needed (5V platforms = YES, 3.3V platforms = NO)
- [ ] Verify voltage divider calculation if used (should output ~3.3V)
- [ ] Confirm TX/RX wiring (Arduino TX → BT RX, Arduino RX → BT TX)
- [ ] Check GND connection (module GND → Arduino GND)
- [ ] Verify module baud matches Serial2.begin() baud (default 9600)

## References

- [Transport Architecture Guide](../../architecture/TRANSPORT_ARCHITECTURE.md)
- [RealDash Setup Guide](../outputs/REALDASH_SETUP_GUIDE.md)
- [Build Configuration Guide](../configuration/BUILD_CONFIGURATION_GUIDE.md)
- [HC-05 AT Command Reference](https://www.electronicwings.com/sensors-modules/hc-05-bluetooth-module)
- [HM-10 AT Command Reference](http://www.martyncurrey.com/hm-10-bluetooth-4ble-modules/)
