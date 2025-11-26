# Voltage Sensor Configuration Guide

**Complete guide to battery and voltage monitoring in openEMS**

---

## Overview

openEMS supports voltage monitoring through resistor voltage dividers. The system automatically configures the correct divider ratio based on your microcontroller platform (3.3V or 5V).

---

## Quick Start

### Standard 12V Battery Monitoring

*Compile-Time:*
```cpp
#define INPUT_0_PIN            A8
#define INPUT_0_APPLICATION    PRIMARY_BATTERY
#define INPUT_0_SENSOR         VOLTAGE_DIVIDER
```

*Runtime:*
```
SET A8 APPLICATION PRIMARY_BATTERY
SET A8 SENSOR VOLTAGE_DIVIDER
ENABLE A8
SAVE
```

**That's it!** The system automatically selects the correct divider ratio for your board.

---

## Available Voltage Sensors

| Sensor ID | Application | Notes |
|-----------|-------------|-------|
| `VOLTAGE_DIVIDER` | 12V battery monitoring | Auto-configured per platform |

**Platform Auto-Configuration:**
- **5V systems (Arduino Mega/Uno):** Expects 100kΩ/6.8kΩ divider
- **3.3V systems (Teensy/Due/ESP32):** Expects 100kΩ/22kΩ divider

---

## Wiring

### Standard 12V Battery Divider

**For 5V boards (Arduino Mega, Uno):**
```
Battery + ----[100kΩ]----+----[6.8kΩ]---- GND
                         |
                      [100nF]
                         |
                      Analog Pin (A8)
```

**For 3.3V boards (Teensy, Due, ESP32):**
```
Battery + ----[100kΩ]----+----[22kΩ]---- GND
                         |
                      [100nF]
                         |
                      Analog Pin (A8)
```

**Components:**
- R1: 100kΩ ±1% (high side)
- R2: 6.8kΩ or 22kΩ ±1% (low side, per platform)
- C1: 100nF ceramic capacitor (noise filtering)

### Protection Circuit (Recommended)

Add a zener diode for transient protection:
```
Battery + ----[100kΩ]----+----[R2]---- GND
                         |
                      [Zener]
                         |
                      [100nF]
                         |
                      Analog Pin

Zener: 5.1V for 5V boards, 3.3V for 3.3V boards
```

---

## Configuration Examples

### Example 1: Primary Battery with Alarm

*Compile-Time:*
```cpp
#define INPUT_0_PIN            A8
#define INPUT_0_APPLICATION    PRIMARY_BATTERY
#define INPUT_0_SENSOR         VOLTAGE_DIVIDER
```

*Runtime:*
```
SET A8 APPLICATION PRIMARY_BATTERY
SET A8 SENSOR VOLTAGE_DIVIDER
SET A8 ALARM 11.5 15
ENABLE A8
SAVE
```

Alarms at <11.5V (low battery) or >15V (overcharging).

### Example 2: Dual Battery System

*Compile-Time:*
```cpp
// Starter battery
#define INPUT_0_PIN            A8
#define INPUT_0_APPLICATION    PRIMARY_BATTERY
#define INPUT_0_SENSOR         VOLTAGE_DIVIDER

// House/auxiliary battery
#define INPUT_1_PIN            A7
#define INPUT_1_APPLICATION    AUXILIARY_BATTERY
#define INPUT_1_SENSOR         VOLTAGE_DIVIDER
```

*Runtime:*
```
SET A8 APPLICATION PRIMARY_BATTERY
SET A8 SENSOR VOLTAGE_DIVIDER
SET A8 NAME STRT
ENABLE A8

SET A7 APPLICATION AUXILIARY_BATTERY
SET A7 SENSOR VOLTAGE_DIVIDER
SET A7 NAME HOUS
ENABLE A7

SAVE
```

---

## Understanding Voltage Dividers

### Basic Principle

A voltage divider reduces high voltage to safe levels for the ADC:

```
V_in ----[R1]----+----[R2]---- GND
                 |
              V_out (to ADC)
```

**Voltage at ADC:**
```
V_out = V_in × (R2 / (R1 + R2))
```

**Measured voltage:**
```
V_in = V_out × ((R1 + R2) / R2)
```

### Standard Divider Calculations

**For 5V systems with 100kΩ/6.8kΩ:**
- Divider ratio: 6.8 / (100 + 6.8) = 0.0636
- At 12V battery: 12 × 0.0636 = 0.76V at ADC ✓
- At 15V battery: 15 × 0.0636 = 0.95V at ADC ✓
- Maximum safe: ~78V (would give 5V at ADC)

**For 3.3V systems with 100kΩ/22kΩ:**
- Divider ratio: 22 / (100 + 22) = 0.180
- At 12V battery: 12 × 0.180 = 2.16V at ADC ✓
- At 15V battery: 15 × 0.180 = 2.70V at ADC ✓
- Maximum safe: ~18V (would give 3.3V at ADC)

---

## Battery Voltage Reference

### 12V Lead-Acid Battery States

| Voltage | State | Action |
|---------|-------|--------|
| 12.6V+ | Fully charged (100%) | Normal |
| 12.4V | 75% charged | Normal |
| 12.2V | 50% charged | Consider charging |
| 12.0V | 25% charged | Recharge soon |
| 11.8V | Nearly discharged | Recharge now |
| <11.5V | Critically low | May damage battery |

### Charging System Voltages

| Voltage | Condition | Notes |
|---------|-----------|-------|
| 13.5-14.5V | Normal charging | Alternator working |
| <13.5V | Undercharging | Check alternator/belt |
| >14.8V | Overcharging | Check regulator |
| >15.5V | Dangerous | Disconnect load, investigate |

### 24V Systems (Trucks)

Multiply 12V values by 2:
- Fully charged: ~25.2V
- Normal charging: 27-29V
- Use 100kΩ/3.3kΩ divider

---

## Calibration

### Method 1: Measure Actual Resistors

Most accurate method:

1. Measure R1 with multimeter: e.g., 98.5kΩ
2. Measure R2 with multimeter: e.g., 6.75kΩ
3. Use custom calibration in advanced_config.h

### Method 2: Known Voltage Comparison

1. Measure battery with accurate voltmeter: 12.65V
2. Read openEMS display: 12.80V (0.15V high)
3. Note the error for future correction

### Method 3: Two-Point Calibration

For best accuracy:
1. Measure at low voltage (11V)
2. Measure at high voltage (14V)
3. Calculate correction factor

---

## Troubleshooting

### Voltage reads too high

**Possible causes:**
- R1 actual value lower than expected
- R2 actual value higher than expected
- ADC reference voltage incorrect

**Solutions:**
- Measure actual resistor values
- Check ADC reference voltage setting

### Voltage reads too low

**Possible causes:**
- R1 actual value higher than expected
- R2 actual value lower than expected
- Poor connection adding resistance

**Solutions:**
- Check all connections
- Measure voltage at ADC pin with multimeter

### Erratic readings

**Possible causes:**
- Missing filter capacitor
- Poor ground connection
- Electrical noise

**Solutions:**
- Add 100nF capacitor at ADC pin
- Use shielded cable
- Check ground connection
- Route wires away from ignition components

### Readings drift with temperature

**Possible causes:**
- Resistor temperature coefficient
- ADC temperature drift

**Solutions:**
- Use 1% metal film resistors (low tempco)
- Keep resistors away from heat sources

---

## Advanced Applications

### Monitoring Voltage Drop

Use two voltage sensors to monitor voltage drop across cables:

*Compile-Time:*
```cpp
#define INPUT_0_PIN            A8
#define INPUT_0_APPLICATION    PRIMARY_BATTERY
#define INPUT_0_SENSOR         VOLTAGE_DIVIDER

#define INPUT_1_PIN            A7
#define INPUT_1_APPLICATION    AUXILIARY_BATTERY
#define INPUT_1_SENSOR         VOLTAGE_DIVIDER
```

Measure at battery and at load - difference shows cable drop.

### Solar System Monitoring

Monitor solar panel and battery:

```cpp
#define INPUT_0_PIN            A8
#define INPUT_0_APPLICATION    PRIMARY_BATTERY
#define INPUT_0_SENSOR         VOLTAGE_DIVIDER

#define INPUT_1_PIN            A9
#define INPUT_1_APPLICATION    AUXILIARY_BATTERY
#define INPUT_1_SENSOR         VOLTAGE_DIVIDER
```

---

## Hardware Best Practices

### Resistor Selection
- Use 1% tolerance metal film resistors
- 1/4W rating is sufficient
- Match temperature coefficients

### PCB Design Tips
- Place resistors close to ADC pin
- Use ground plane under ADC traces
- Keep traces short
- Add TVS diode for surge protection

### Mounting
- Mount divider near microcontroller
- Protect from moisture and vibration
- Use proper automotive connectors

---

## Safety Considerations

⚠️ **Electrical Safety:**
- Always disconnect battery before wiring
- Double-check polarity
- Use fused connection (1A fuse recommended)
- Ensure proper insulation

⚠️ **Reverse Polarity Protection:**
Consider adding a diode:
```
Battery + ----[1N4148]----[100kΩ]----+----[R2]---- GND
                                      |
                                   ADC Pin
```

⚠️ **Transient Protection:**
Automotive electrical systems have spikes up to 100V. Use:
- Zener diode at ADC pin
- TVS diode at divider input
- MOV for surge protection

---

**For the classic car community.**
