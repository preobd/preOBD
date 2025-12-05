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

Most alternators have 3-phase stator windings. The W-phase outputs AC pulses proportional to **alternator** RPM. Since alternators typically spin 2.5-3.5 times faster than the engine (due to pulley ratio), we must account for this:

```
Formula: Engine_RPM = (60M / (interval_µs × pulses_per_alt_rev × pulley_ratio)) × calibration_mult

Where:
- pulses_per_alt_rev = poles / 2  (12-pole = 6 pulses per revolution)
- pulley_ratio = alternator/engine speed ratio (typically 3:1)
- calibration_mult = fine-tuning multiplier (default 1.0)

Example for 12-pole alternator, 3:1 pulley ratio at 2000 RPM:
- Alternator spins at: 2000 × 3 = 6000 RPM
- Alternator_Hz = (6000 × 12) / 120 = 600 Hz
- Engine_RPM calculated = 2000 RPM ✓
```

**Default calibration:** 12 poles, 3:1 pulley ratio (most common automotive setup)

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
SET 5 ENGINE_RPM W_PHASE_RPM

# (Optional) Customize RPM calibration:
# SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
SET 5 RPM 12 3.0 2000 100 8000    # 12 poles, 3:1 ratio (default)
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

**Method 1: Check alternator specs**
- Most automotive alternators: 12 poles (most common)
- High-output alternators: 14-16 poles
- Small engines: 8-12 poles
- Look for datasheet or part number

**Method 2: Calculate from known RPM**
1. Temporarily set poles=2, ratio=1.0 in configuration
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

**Method 2: Count pulley teeth (for toothed pulleys)**
```
Ratio = Crank_Teeth / Alt_Teeth
```

**Method 3: Empirical calibration (most accurate)**
1. Set correct poles (from Step 4)
2. Set estimated ratio (3.0 is most common)
3. Compare to external tachometer at various RPM
4. Adjust ratio OR use calibration multiplier for fine-tuning

**Common ratios:**
- **3:1** - Most common automotive (2.5-3.5 range)
- **2:1** - Older vehicles and light trucks
- **2.5:1** - Some modern vehicles

### Step 6: Fine-Tune Calibration (Optional)

After setting poles and pulley ratio, you may want to fine-tune using the calibration multiplier.

**Calibration multiplier workflow:**
1. Set poles and estimated pulley ratio
2. Compare openEMS RPM to external tachometer
3. Calculate: `calibration_mult = Actual_RPM / Displayed_RPM`
4. Update calibration with new multiplier

**Example:**
```
External tach shows: 2040 RPM
openEMS shows: 2000 RPM
calibration_mult = 2040 / 2000 = 1.02

Update configuration:
SET 5 RPM 12 3.0 1.02 2000 100 8000
```

This is similar to adjusting the potentiometer on an aftermarket tachometer.

---

## Configuration

### Basic Setup (Default Calibration)

Uses default: 12 poles, 3:1 pulley ratio

*Compile-Time:*
```cpp
#define INPUT_0_PIN            5
#define INPUT_0_APPLICATION    ENGINE_RPM
#define INPUT_0_SENSOR         W_PHASE_RPM
```

*Runtime:*
```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 ALARM 500 6500
SAVE
```

The alarm triggers below 500 RPM (stall warning) or above 6500 RPM (over-rev).

### Custom Calibration (Compile-Time)

For non-standard alternators, use `advanced_config.h`:

```cpp
// In config.h or sensors_config.h:
#define INPUT_0_PIN         5
#define INPUT_0_APPLICATION ENGINE_RPM
#define INPUT_0_SENSOR      W_PHASE_RPM

// In advanced_config.h:
#define INPUT_0_CUSTOM_CALIBRATION
#ifdef INPUT_0_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_RPM(input_0,
        18,      // poles (18-pole alternator)
        3.0,     // pulley_ratio (3:1 alternator to engine)
        1.0,     // calibration_mult (no fine-tuning)
        2000,    // timeout_ms
        300,     // min_rpm
        8000     // max_rpm
    )
#endif
```

### Custom Calibration (Runtime)

Adjust calibration via serial commands:

```
# Initial setup (5 parameters - calibration_mult defaults to 1.0):
SET 5 RPM 12 3.0 2000 100 8000

# Fine-tuned setup (6 parameters - custom calibration_mult):
SET 5 RPM 12 3.0 1.02 2000 100 8000

# Different alternator (14-pole, 2.5:1 ratio):
SET 5 RPM 14 2.5 2000 100 8000

# Query current calibration:
INFO 5

# Save to EEPROM:
SAVE
```

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
   - Default is 3:1 (most common)
   - Try 2:1 if reading is 50% too high
   - Try 2.5:1 if reading is 20% too high
3. **Fine-tune with calibration multiplier:** Follow Step 6
   - `calibration_mult = Actual_RPM / Displayed_RPM`
   - Example: If showing 1980 but should be 2000, use mult=1.01
4. Add shielding to signal wire if readings are unstable

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
