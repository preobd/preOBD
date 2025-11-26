# W-Phase Alternator RPM Sensing Guide

**RPM measurement for classic cars without electronic ignition**

---

## ⚠️ CRITICAL SAFETY WARNING

**3.3V boards (Teensy, Due, ESP32) are NOT 5V tolerant!**

The alternator W-phase output can reach 30V+ at high RPM. You **MUST** use a voltage protection circuit.

- **3.3V systems:** Use 22kΩ/4.7kΩ divider with **3.3V zener** (REQUIRED)
- **5V systems:** Use 22kΩ/4.7kΩ divider with **5.1V zener** (recommended)

**Applying more than 3.3V to a Teensy pin WILL damage it permanently!**

---

## Overview

This guide explains how to add engine RPM sensing using the alternator's W-phase output. This method is ideal for classic cars and vehicles without electronic ignition systems.

### How It Works

Most alternators have 3-phase stator windings. The W-phase outputs AC pulses proportional to engine RPM:

```
Alternator_Hz = (Engine_RPM × Poles) / 120

Example for 12-pole alternator at 3000 RPM:
Hz = (3000 × 12) / 120 = 300 Hz
```

---

## Quick Start

### Compile-Time Configuration

```cpp
#define INPUT_0_PIN            5
#define INPUT_0_APPLICATION    ENGINE_RPM
#define INPUT_0_SENSOR         W_PHASE_RPM
```

### Runtime Configuration

```
SET 5 APPLICATION ENGINE_RPM
SET 5 SENSOR W_PHASE_RPM
ENABLE 5
SAVE
```

---

## Required Hardware

### Components (Universal Circuit - Recommended)

| Component | Value | Notes |
|-----------|-------|-------|
| R1 | 22kΩ 1/4W | High side resistor |
| R2 | 4.7kΩ 1/4W | Low side resistor |
| D1 | 1N4148 | Fast switching diode |
| C1 | 100nF ceramic | Noise filter |
| Z1 | 3.3V or 5.1V zener | Voltage clamp (see table) |

### Zener Selection

| Board Type | Zener Voltage | Part Number |
|------------|---------------|-------------|
| Teensy 3.x/4.x | 3.3V | BZX55C3V3 |
| Arduino Due | 3.3V | BZX55C3V3 |
| ESP32 | 3.3V | BZX55C3V3 |
| Arduino Mega | 5.1V | BZX55C5V1 |
| Arduino Uno | 5.1V | BZX55C5V1 |

**Pro tip:** The universal circuit with 22kΩ/4.7kΩ works for ALL boards - just swap the zener!

---

## Circuit Design

### Universal Protection Circuit (Recommended)

This circuit works with both 3.3V and 5V boards - just change the zener:

```
Alternator W-Phase
        |
      [22kΩ]    ← High side resistor
        |
        +----[1N4148]----+
        |                |
      [4.7kΩ]         [100nF]
        |                |
       GND            MCU Pin
                         |
                      [Zener]  ← 3.3V or 5.1V
                         |
                        GND
```

### Signal Levels

| Stage | Voltage |
|-------|---------|
| Alternator W-phase | 10-30V AC |
| After divider | 1.8-5.3V AC |
| After diode | 0-5.3V pulses |
| After zener | Clamped to zener voltage |

### Why 22kΩ/4.7kΩ Works for Both

- Divider ratio: 4.7 / (22 + 4.7) = 0.176
- At 30V alternator output: 30 × 0.176 = 5.3V
- 3.3V zener clamps to 3.3V → Safe for Teensy ✓
- 5.1V zener clamps to 5.1V → Safe for Arduino ✓
- **Same resistors, just swap the zener!**

---

## Wiring Diagram

### Complete Circuit

```
                    Alternator
                    +-----+
                    |  W  |--------+
                    |  V  |        |
                    |  U  |      [22kΩ]
                    |     |        |
                    | B+  |-----+  +--[1N4148]--+--[100nF]--+
                    | GND |--+  |               |           |
                    +-----+  |  +-------------+ |           |
                             |                | |           |
                             +----------------|-|-----------+
                                              | |           |
                                              | +--[Zener]--+
                                              |  3.3V or 5.1V
                                              +--[4.7kΩ]----+
                                                      |
                                              MCU Pin 5
                                                      |
                                                     GND
```

---

## Installation Steps

### Step 1: Locate W-Phase Terminal

**On most alternators:**
- Look for a small terminal labeled "W", "STA", or "TACH"
- Often a single spade connector on the back
- Some alternators have it internally

**Testing the W-phase:**
1. Start engine
2. Use AC voltmeter on W terminal (to ground)
3. Should read 5-30V AC that increases with RPM
4. If no voltage, try other small terminals

### Step 2: Build Protection Circuit

**On breadboard (for testing):**
1. Use 22kΩ/4.7kΩ resistors
2. Double-check zener voltage rating
3. Test voltage at MCU pin with multimeter before connecting
4. Should not exceed 3.3V for Teensy or 5V for Arduino

**On PCB (permanent install):**
1. Mount components near microcontroller
2. Use socket for zener (allows board swaps)
3. Heat shrink on alternator connection
4. Route wire away from spark plug wires

### Step 3: Connect to Microcontroller

**Recommended pins (interrupt-capable):**

| Board | Best Pins | Notes |
|-------|-----------|-------|
| Teensy 4.0/4.1 | Any digital | All support interrupts |
| Teensy 3.x | Any digital | All support interrupts |
| Arduino Mega | 2, 3, 18, 19, 20, 21 | Hardware interrupt pins |
| Arduino Uno | 2, 3 | Only two interrupt pins |

### Step 4: Determine Alternator Poles

**Method 1: Test and calculate**
1. Connect circuit and run test code
2. Read frequency at known RPM (use tachometer)
3. Calculate: `Poles = (Frequency × 120) / RPM`

**Method 2: Common specs**
- Most automotive: 12 poles
- High-output alternators: 14-16 poles
- Small engines: 8-12 poles

**Example calculation:**
```
Measured: 200 Hz at 2000 RPM
Poles = (200 × 120) / 2000 = 12 poles
```

---

## Configuration

### Basic Setup

*Compile-Time:*
```cpp
#define INPUT_0_PIN            5
#define INPUT_0_APPLICATION    ENGINE_RPM
#define INPUT_0_SENSOR         W_PHASE_RPM
```

*Runtime:*
```
SET 5 APPLICATION ENGINE_RPM
SET 5 SENSOR W_PHASE_RPM
SET 5 ALARM 500 6500
ENABLE 5
SAVE
```

The alarm triggers below 500 RPM (stall warning) or above 6500 RPM (over-rev).

---

## Troubleshooting

### No RPM reading

**Check:**
1. W-phase terminal is correct
2. Protection circuit is wired correctly
3. Voltage at MCU pin (should pulse 0-3.3V or 0-5V)
4. Pin is interrupt-capable

### RPM reads incorrectly

**Possible causes:**
1. Wrong pole count setting
2. Alternator pulley ratio not 1:1 with engine
3. Interference from ignition system

**Solutions:**
1. Recalculate pole count
2. Account for pulley ratio: `Engine_RPM = Alt_RPM × (Alt_pulley / Crank_pulley)`
3. Add shielding to signal wire

### RPM is erratic/jumpy

**Solutions:**
1. Verify 100nF capacitor is installed
2. Add second 100nF at MCU pin
3. Use shielded cable
4. Route away from ignition wires
5. Check ground connection

### RPM shows zero intermittently

**Check:**
1. Loose connections
2. Alternator belt slipping
3. Timeout setting (default 2 seconds)

---

## Parts List

**Universal circuit (recommended):**
| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | 22kΩ resistor 1/4W | |
| 1 | 4.7kΩ resistor 1/4W | |
| 1 | 1N4148 diode | |
| 1 | 100nF ceramic capacitor | |
| 1 | Zener diode | 3.3V or 5.1V per board |

**Where to buy:** Any electronics supplier (Mouser, Digikey, etc.)
**Total cost:** ~$2-3 USD

---

## Performance

- **Update rate:** Real-time (every pulse)
- **Accuracy:** ±10 RPM typical
- **Range:** 300-8000 RPM
- **CPU overhead:** Very low (interrupt-based)
- **Latency:** <10ms typical

---

## Safety Considerations

⚠️ **Electrical Safety:**
- Alternator can produce 30V+ at high RPM
- Disconnect battery before working on alternator
- Use zener diode protection always

⚠️ **Mounting:**
- Secure all connections with heat shrink
- Route away from hot engine parts
- Use proper crimped connections (not just twisted wires)
- Keep away from rotating parts

⚠️ **Over-Rev Protection:**
- Set appropriate RPM_MAX alarm
- Consider adding warning LED
- Don't rely solely on openEMS - maintain mechanical backup

---

## Related Guides

- [Sensor Selection Guide](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [Advanced Calibration](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom pole counts
- [Adding Sensors](../configuration/ADDING_SENSORS.md) - Adding new sensor types

---

**Questions? Post in GitHub Discussions with your alternator specs!**

**For the classic car community.**
