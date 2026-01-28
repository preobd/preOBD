# W-Phase Alternator RPM Guide

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

Most alternators have 3-phase stator windings. The W-phase outputs AC pulses proportional to **alternator** RPM. Since alternators typically spin 2.5-3.5 times faster than the engine (due to pulley ratio), we must account for this:

```
Engine_RPM = (Alternator_Pulses × 60) / (poles/2 × pulley_ratio)

Where:
- poles = alternator pole count (typically 12)
- pulley_ratio = alternator/engine speed ratio (typically 3:1)
```

**Default calibration:** 12 poles, 3:1 pulley ratio (most common automotive setup)

---

## Quick Start

```
SET 5 ENGINE_RPM W_PHASE_RPM
SAVE
```

With custom calibration (14-pole alternator, 2.5:1 ratio):
```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 RPM 14 2.5 2000 100 8000
SAVE
```

---

## Required Hardware

### Components (Universal Circuit)

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

### Universal Protection Circuit

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

**Method 1: Check alternator specs**
- Most automotive alternators: 12 poles (most common)
- High-output alternators: 14-16 poles
- Small engines: 8-12 poles
- Look for datasheet or part number

**Method 2: Calculate from known RPM**
1. Set poles=2, ratio=1.0 in configuration
2. Run engine at known RPM (use external tachometer)
3. Read displayed RPM value
4. Calculate: `Actual_Poles = Displayed_RPM / Known_RPM × 2`

**Example:**
```
Known engine RPM: 2000
Displayed with poles=2, ratio=1.0: 12000
Actual poles = 12000 / 2000 × 2 = 12 poles ✓
```

### Step 5: Determine Pulley Ratio

The pulley ratio determines how much faster the alternator spins than the engine.

**Method 1: Measure pulley diameters**
```
Pulley Ratio = Crank_Pulley_Diameter / Alternator_Pulley_Diameter

Example:
Crank pulley: 6 inches
Alternator pulley: 2 inches
Ratio = 6 / 2 = 3.0 (3:1)  ← Most common
```

**Common ratios:**
- **3:1** - Most common automotive (2.5-3.5 range)
- **2:1** - Older vehicles and light trucks
- **2.5:1** - Some modern vehicles

---

## Configuration

### Basic Setup (Default Calibration)

Uses default: 12 poles, 3:1 pulley ratio

```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 ALARM 500 6500
SAVE
```

The alarm triggers below 500 RPM (stall warning) or above 6500 RPM (over-rev).

### Custom Calibration

```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 RPM <poles> <ratio> <timeout> <min> <max>
SAVE
```

**Parameters:**
- `poles` - Alternator pole count (8-18 typical)
- `ratio` - Pulley ratio (1.0-4.0 typical)
- `timeout` - Timeout in ms before showing 0 RPM (default 2000)
- `min` - Minimum valid RPM (default 100)
- `max` - Maximum valid RPM (default 8000)

**Examples:**
```
# 12-pole, 3:1 ratio (default)
SET 5 RPM 12 3.0 2000 100 8000

# 14-pole alternator, 2.5:1 ratio
SET 5 RPM 14 2.5 2000 100 8000

# High-revving engine
SET 5 RPM 12 3.0 2000 500 10000
```

### Fine-Tuning with Calibration Multiplier

If readings are close but not exact:

```
SET 5 RPM 12 3.0 1.02 2000 100 8000
```

The `1.02` is a calibration multiplier (similar to adjusting a potentiometer on an aftermarket tachometer).

**Calibration workflow:**
1. Set poles and pulley ratio
2. Compare openEMS RPM to external tachometer
3. Calculate: `mult = Actual_RPM / Displayed_RPM`
4. Update with multiplier

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
2. Wrong pulley ratio (most common issue!)
3. Calibration multiplier needs adjustment
4. Interference from ignition system

**Solutions:**
1. **Check pole count:** Follow Step 4 to determine correct poles
2. **Measure pulley ratio:** Follow Step 5 methods
3. **Fine-tune:** Use calibration multiplier
4. Add shielding to signal wire

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

**Universal circuit:**
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
- Set appropriate max RPM alarm
- Consider adding warning LED
- Don't rely solely on openEMS - maintain mechanical backup

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

