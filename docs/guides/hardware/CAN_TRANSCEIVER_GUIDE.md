# CAN Transceiver Hardware Guide for openEMS

## Overview

**Critical requirement**: All CAN functionality in openEMS requires an external **CAN transceiver** to convert between logic-level signals (3.3V or 5V) and the differential CAN bus signals (CAN-H and CAN-L).

### Why You Need a Transceiver

Your microcontroller's CAN peripheral (FlexCAN, TWAI, or MCP2515) operates at **logic level** (TTL):
- Teensy/ESP32: 3.3V logic
- Arduino Mega/Uno: 5V logic

The physical CAN bus uses **differential signaling**:
- CAN-H (CAN High): Dominant ~3.5V, Recessive ~2.5V
- CAN-L (CAN Low): Dominant ~1.5V, Recessive ~2.5V
- Differential voltage: ~2V (dominant), ~0V (recessive)

**The transceiver bridges these incompatible voltage domains.** Without it, your microcontroller cannot communicate on the CAN bus.

### When Do You Need This?

You need a CAN transceiver for **any** CAN output use case:
- **RealDash over CAN** - Dashboard display on mobile/tablet
- **OBD-II scanners** - ELM327 Bluetooth adapters with Torque/OBD Fusion
- **Custom CAN devices** - Aftermarket gauges, data loggers, etc.

**Note**: RealDash can also work over Serial/UART/Bluetooth without CAN hardware. This guide only applies if you're using CAN output.

---

## Recommended CAN Transceivers

### For 5V Platforms (Arduino Mega, Uno, Nano)

| Chip | Voltage | Current | Availability | Notes |
|------|---------|---------|--------------|-------|
| **MCP2551** | 5V | 500kbps | Common, $1-2 | Industry standard, widely available |
| **TJA1050** | 5V | 1Mbps | Common, $2-3 | Automotive grade, NXP/Philips |
| **TJA1051** | 5V | 1Mbps | Common, $2-3 | Improved TJA1050 with slope control |

**Recommended module**: Generic MCP2551 breakout board (~$2-3 on eBay/AliExpress)

### For 3.3V Platforms (Teensy, ESP32, Due)

| Chip | Voltage | Current | Availability | Notes |
|------|---------|---------|--------------|-------|
| **SN65HVD230** | 3.3V | 1Mbps | Common, $2-3 | **Recommended** - TI automotive grade |
| **MCP2562** | 3.3V/5V | 1Mbps | Common, $2-4 | Dual voltage, more expensive |
| **TJA1042** | 3.3V/5V | 5Mbps | Less common | High-speed, overkill for openEMS |

**Recommended module**: Waveshare SN65HVD230 CAN Board (~$3-5)

**⚠️ CRITICAL**: Never use a 5V transceiver (MCP2551, TJA1050) directly with 3.3V platforms! The TX/RX pins are 5V and will **permanently damage** your microcontroller.

### Voltage Level Shifting Options

If you only have a 5V transceiver and a 3.3V platform:
1. **Best**: Buy a 3.3V transceiver (SN65HVD230) - safest, cleanest
2. **Acceptable**: Use a bi-directional logic level shifter between MCU and transceiver
3. **Not recommended**: Voltage divider on RX, direct connect TX (unreliable)

---

## Platform-Specific Wiring

### Teensy 4.x with Native FlexCAN + SN65HVD230

**Hardware**: Teensy 4.0/4.1 has built-in CAN controller (FlexCAN)

```
Teensy 4.x      SN65HVD230     Vehicle CAN Bus
----------      ----------     ---------------
Pin 22 (TX) →   CTX (TX)
Pin 23 (RX) ←   CRX (RX)
3.3V        →   VCC
GND         →   GND
                CANH       →   CAN-H (vehicle)
                CANL       →   CAN-L (vehicle)
                            →   120Ω termination (if bus end)
```

**Notes**:
- Teensy 4.x uses 3.3V logic - use SN65HVD230 or MCP2562
- FlexCAN_T4 library required (installed automatically)
- No external CAN controller needed (MCP2515 not required)

### Teensy 3.x with Native FlexCAN + SN65HVD230

**Hardware**: Teensy 3.2/3.5/3.6 has built-in CAN controller (FlexCAN)

```
Teensy 3.x      SN65HVD230     Vehicle CAN Bus
----------      ----------     ---------------
Pin 3 (TX)  →   CTX (TX)       (Teensy 3.2)
Pin 4 (RX)  ←   CRX (RX)       (Teensy 3.2)
  -or-
Pin 29 (TX) →   CTX (TX)       (Teensy 3.5/3.6 CAN0)
Pin 30 (RX) ←   CRX (RX)       (Teensy 3.5/3.6 CAN0)
3.3V        →   VCC
GND         →   GND
                CANH       →   CAN-H (vehicle)
                CANL       →   CAN-L (vehicle)
                            →   120Ω termination (if bus end)
```

**Notes**:
- Check pinout for your specific Teensy 3.x model
- Teensy 3.5/3.6 have dual CAN (CAN0/CAN1) - use CAN0

### ESP32 with Native TWAI + SN65HVD230

**Hardware**: ESP32 has built-in CAN controller (TWAI, compatible with CAN 2.0B)

```
ESP32           SN65HVD230     Vehicle CAN Bus
----------      ----------     ---------------
GPIO21 (TX) →   CTX (TX)       (ESP32 original)
GPIO22 (RX) ←   CRX (RX)       (ESP32 original)
  -or-
GPIO20 (TX) →   CTX (TX)       (ESP32-S3/C3)
GPIO21 (RX) ←   CRX (RX)       (ESP32-S3/C3)
3.3V        →   VCC
GND         →   GND
                CANH       →   CAN-H (vehicle)
                CANL       →   CAN-L (vehicle)
                            →   120Ω termination (if bus end)
```

**Notes**:
- **ESP32 original**: GPIO21 (TX), GPIO22 (RX)
- **ESP32-S3/C3**: GPIO20 (TX), GPIO21 (RX) - GPIO22 doesn't exist on S3
- ESP32 uses 3.3V logic - use SN65HVD230 or MCP2562
- ESP32-TWAI-CAN library required (installed automatically)
- No external CAN controller needed (MCP2515 not required)

### Arduino Mega with MCP2515 + MCP2551

**Hardware**: Arduino Mega requires external MCP2515 CAN controller via SPI

```
Arduino Mega    MCP2515        MCP2551        Vehicle CAN Bus
------------    --------       --------       ---------------
Pin 13 (SCK)→   SCK
Pin 12 (MISO)←  SO
Pin 11 (MOSI)→  SI
Pin 9 (CS)  →   CS
Pin 2 (INT) ←   INT
5V          →   VCC
GND         →   GND
                TX         →   TXD
                RX         ←   RXD
                               VCC        →   5V
                               GND        →   GND
                               CANH       →   CAN-H (vehicle)
                               CANL       →   CAN-L (vehicle)
                                           →   120Ω termination
```

**Notes**:
- Requires **two chips**: MCP2515 (CAN controller) + MCP2551 (transceiver)
- Many modules combine both chips on one PCB (~$3-5)
- CS and INT pins configurable in config.h
- sandeepmistry/CAN library required (installed automatically)

### Arduino Uno with MCP2515 + MCP2551

**Hardware**: Same as Mega, but limited RAM

```
Arduino Uno     MCP2515        MCP2551        Vehicle CAN Bus
-----------     --------       --------       ---------------
Pin 13 (SCK)→   SCK
Pin 12 (MISO)←  SO
Pin 11 (MOSI)→  SI
Pin 10 (CS) →   CS
Pin 2 (INT) ←   INT
5V          →   VCC
GND         →   GND
                TX         →   TXD
                RX         ←   RXD
                               VCC        →   5V
                               GND        →   GND
                               CANH       →   CAN-H (vehicle)
                               CANL       →   CAN-L (vehicle)
                                           →   120Ω termination
```

**Notes**:
- Same wiring as Mega
- **Limited features** due to 2KB RAM - use compile-time configuration only
- See [STATIC_BUILDS_GUIDE.md](../../advanced/STATIC_BUILDS_GUIDE.md)

---

## Connecting to Vehicle CAN Bus

### Option 1: OBD-II Port (Recommended for Testing)

Most modern vehicles have a standardized OBD-II connector under the dashboard.

**OBD-II Pinout (DLC connector)**:
```
      1  2  3  4  5  6  7  8
      9 10 11 12 13 14 15 16

Pin 4:  Chassis ground
Pin 5:  Signal ground
Pin 6:  CAN-H (ISO 15765-4)
Pin 14: CAN-L (ISO 15765-4)
Pin 16: +12V battery power (unswitched)
```

**Wiring from transceiver to OBD-II**:
```
Transceiver CANH  → OBD-II Pin 6 (CAN-H)
Transceiver CANL  → OBD-II Pin 14 (CAN-L)
Transceiver GND   → OBD-II Pin 5 (Signal GND)
```

**Notes**:
- OBD-II CAN runs at **500 kbps** (openEMS default)
- Built-in 120Ω termination in vehicle ECU
- **Be careful**: Don't disrupt factory ECU communication!
- Use a Y-splitter if you have an existing OBD-II device plugged in

### Option 2: Direct Wiring (Permanent Installation)

For classic cars or custom installations without OBD-II.

**CAN bus wire colors** (no standard, verify with multimeter):
- Twisted pair: CAN-H and CAN-L (often yellow/green or orange/blue)
- Usually shielded cable for noise immunity

**Termination resistors**:
- CAN bus requires **120Ω resistors** at **both ends** of the bus
- openEMS is typically a node in the middle (no termination)
- If openEMS is at the end of the bus, add 120Ω between CAN-H and CAN-L

**Verifying termination**:
1. Disconnect all devices from bus
2. Measure resistance between CAN-H and CAN-L
3. Should read **~60Ω** (two 120Ω resistors in parallel)
4. If >100Ω, bus is under-terminated (add resistor)
5. If <50Ω, bus is over-terminated (remove resistor)

---

## Common Transceiver Modules

### Waveshare SN65HVD230 CAN Board

**Specifications**:
- Chip: Texas Instruments SN65HVD230
- Voltage: 3.3V logic
- Speed: 1Mbps max
- Size: 15mm x 10mm
- Price: ~$3-5

**Pinout**:
```
 ┌─────────────┐
 │  CTX    VCC │  VCC: 3.3V
 │  CRX    GND │  GND: Ground
 │ CANH   120R │  120R: Termination jumper (optional)
 │ CANL        │
 └─────────────┘
```

**Termination jumper**:
- Bridges 120Ω resistor between CANH/CANL
- **Remove jumper** for mid-bus nodes (default for openEMS)
- **Install jumper** only if openEMS is at the end of the bus

### Generic MCP2551 Breakout Board

**Specifications**:
- Chip: Microchip MCP2551
- Voltage: 5V logic
- Speed: 500kbps max (125kbps typical)
- Size: 20mm x 15mm
- Price: ~$1-2

**Pinout**:
```
 ┌─────────────┐
 │  TXD    VCC │  VCC: 5V
 │  RXD    GND │  GND: Ground
 │ CANH        │
 │ CANL        │
 └─────────────┘
```

**Notes**:
- Some modules have built-in 120Ω termination (check schematic)
- Older chip, slower than SN65HVD230
- Works fine for openEMS (500kbps standard)

### MCP2515 + MCP2551 Combo Module

**Specifications**:
- Chips: MCP2515 (CAN controller) + MCP2551 (transceiver)
- Voltage: 5V logic
- Speed: 1Mbps (MCP2515), 500kbps (MCP2551)
- Interface: SPI
- Size: 50mm x 30mm
- Price: ~$3-5

**Pinout**:
```
 ┌─────────────────┐
 │ VCC   GND       │  Power: 5V
 │ SCK   MISO      │  SPI interface
 │ MOSI  CS        │
 │ INT             │  Interrupt pin
 │ CANH  CANL      │  CAN bus output
 └─────────────────┘
```

**Notes**:
- All-in-one solution for Arduino Mega/Uno
- Check CS and INT pin configuration in config.h
- Crystal frequency: 8MHz or 16MHz (auto-detected by library)

---

## Troubleshooting

### "CAN init failed" on Boot

**Symptoms**:
```
⚠ MCP2515 CAN init failed!
  -or-
⚠ ESP32 TWAI init failed!
```

**Possible causes**:
1. **No transceiver connected** - Check wiring
2. **Wrong voltage** - 5V transceiver on 3.3V platform
3. **Bad solder joints** - Reflow connections
4. **Incorrect CS/INT pins** - Verify config.h settings (MCP2515 only)
5. **Crystal mismatch** - MCP2515 module uses wrong crystal (8MHz vs 16MHz)

**Fix**:
- Double-check wiring against diagrams above
- Use multimeter to verify 3.3V/5V on transceiver VCC pin
- For MCP2515: Verify CS and INT pins match config.h

### No Communication on CAN Bus

**Symptoms**:
- CAN initializes successfully
- No errors in serial output
- RealDash/Torque shows "No connection" or "No data"

**Possible causes**:
1. **Missing termination resistors** - Bus needs 120Ω at both ends
2. **Wrong baud rate** - openEMS uses 500 kbps (standard)
3. **CAN-H/CAN-L swapped** - Try reversing wires
4. **Bus off state** - Too many errors, CAN controller shut down
5. **No transceiver power** - Check VCC on transceiver module

**Fix**:
- Measure resistance between CAN-H and CAN-L (~60Ω when properly terminated)
- Verify 500 kbps baud rate in receiving device (ELM327, RealDash, etc.)
- Swap CAN-H and CAN-L wires (polarity matters!)
- Power cycle openEMS to reset CAN controller
- Use multimeter to verify 3.3V or 5V on transceiver VCC pin

### Intermittent Communication / Random Errors

**Symptoms**:
- Communication works sometimes
- Errors increase with cable length
- Works when close, fails when far from ECU

**Possible causes**:
1. **Poor termination** - Incorrect resistor values
2. **Unshielded cable** - Noise from ignition, alternator, etc.
3. **Ground loops** - Different ground potentials
4. **Cable too long** - Exceeds max length for 500 kbps (~100m)
5. **Loose connections** - Vibration causing intermittent contact

**Fix**:
- Use proper 120Ω resistors (not 100Ω or 150Ω)
- Use **twisted pair** cable (CAN-H and CAN-L twisted together)
- Use **shielded** cable and ground shield at one end only
- Keep cable runs short (<10m ideal, <100m max at 500kbps)
- Use crimped or soldered connections, not breadboard jumpers

### 3.3V Transceiver on 5V Platform

**Symptoms**:
- Unreliable communication
- Transceiver gets warm/hot
- Works at short distances only

**Problem**:
5V microcontroller driving 3.3V transceiver can cause:
- Voltage stress on transceiver inputs
- Reduced noise margins
- Overheating

**Fix**:
- **Best**: Use 5V transceiver (MCP2551, TJA1050)
- **Acceptable**: Use dual-voltage transceiver (MCP2562, TJA1042)
- **Not recommended**: Add resistor divider on TX line (degrades signal integrity)

### 5V Transceiver on 3.3V Platform

**Symptoms**:
- **Microcontroller damaged immediately**
- Magic smoke released
- Board stops responding

**Problem**:
5V transceiver outputs 5V on RX pin → **destroys 3.3V GPIO**

**Fix**:
- **Immediately disconnect power**
- Microcontroller may be permanently damaged
- Replace with 3.3V transceiver (SN65HVD230)
- If repairing: Check if microcontroller still works after replacing transceiver

---

## Hardware Recommendations by Use Case

### Budget Build (~$2-5)

**Platform**: Arduino Mega
**Transceiver**: Generic MCP2515 + MCP2551 combo module
**Use case**: Basic RealDash or OBD-II scanner functionality
**Pros**: Cheapest option, easy to find
**Cons**: Requires two chips, slower, 5V only

### Recommended Build (~$20-30)

**Platform**: Teensy 4.0
**Transceiver**: Waveshare SN65HVD230 module
**Use case**: Full-featured openEMS with native CAN
**Pros**: Fast, reliable, 3.3V safe, automotive grade
**Cons**: Slightly more expensive

### High-End Build (~$30-40)

**Platform**: Teensy 4.1
**Transceiver**: SN65HVD230 module
**Use case**: Professional installation with SD logging
**Pros**: SD card slot, Ethernet, 8MB flash, dual CAN
**Cons**: Most expensive option

---

## See Also

- [OBD-II Scanner Setup Guide](../outputs/OBD2_SCANNER_GUIDE.md) - Using ELM327 adapters
- [RealDash Setup Guide](../outputs/REALDASH_SETUP_GUIDE.md) - Mobile dashboard over CAN
- [Build Configuration Guide](../configuration/BUILD_CONFIGURATION_GUIDE.md) - Platform selection
- [PIN_REQUIREMENTS_GUIDE.md](PIN_REQUIREMENTS_GUIDE.md) - Pin capabilities by platform

