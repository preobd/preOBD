# Voltage Sensor Guide

**Battery and voltage monitoring for automotive applications**

---

## Overview

The `VOLTAGE_DIVIDER` sensor type monitors battery voltage using a resistor divider network. openEMS auto-configures the correct divider ratio based on your board's voltage level.

---

## Quick Start

### Serial Commands

```
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER
SAVE
```

For dual-battery setups:

```
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A9 AUXILIARY_BATTERY VOLTAGE_DIVIDER
SAVE
```

---

## Wiring

### Voltage Divider Circuit

You MUST use a voltage divider - connecting 12V directly to an Arduino pin will damage it!

```
Battery (+12V)
     |
   [R1]  ← High-side resistor (100kΩ)
     |
     +----→ Analog Pin
     |
   [R2]  ← Low-side resistor (varies by board)
     |
    GND
```

### Resistor Values by Board

| Board Type | R1 (High) | R2 (Low) | Max Input |
|------------|-----------|----------|-----------|
| 5V boards (Uno, Mega) | 100kΩ | 6.8kΩ | ~18V |
| 3.3V boards (Teensy) | 100kΩ | 22kΩ | ~15V |

**Formula:**
```
V_out = V_in × R2 / (R1 + R2)
V_in = V_out × (R1 + R2) / R2
```

### Example: 5V Arduino

With 100kΩ/6.8kΩ divider:
- At 12V input: 12 × 6.8 / (100 + 6.8) = 0.76V at pin
- At 15V input: 15 × 6.8 / (100 + 6.8) = 0.95V at pin
- Max safe input: 5 × (100 + 6.8) / 6.8 = 78.5V (but use 18V max for safety margin)

### Example: 3.3V Teensy

With 100kΩ/22kΩ divider:
- At 12V input: 12 × 22 / (100 + 22) = 2.16V at pin
- At 15V input: 15 × 22 / (100 + 22) = 2.70V at pin
- Max safe input: 3.3 × (100 + 22) / 22 = 18.3V

---

## Configuration Examples

### Primary Battery with Alarm

```
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A8 ALARM 11.5 15.0
SAVE
```

Triggers alarm below 11.5V (low battery) or above 15.0V (overcharging).

### Dual Battery System

```
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A8 ALARM 11.5 15.0
SET A9 AUXILIARY_BATTERY VOLTAGE_DIVIDER
SET A9 ALARM 11.5 15.0
SAVE
```

---

## Calibration

### Auto-Configuration

openEMS automatically detects your board voltage and configures the divider ratio. For most setups, no calibration is needed.

### Custom Divider Values

If you used different resistor values:

```
SET A8 VOLTAGE <R1> <R2> [correction] [offset]
```

**Example:** Using 47kΩ/10kΩ divider:
```
SET A8 VOLTAGE 47000 10000
SAVE
```

### Fine-Tuning

If readings are slightly off, add a correction factor:

```
SET A8 VOLTAGE 100000 6800 1.02 0
SAVE
```

- `1.02` = 2% correction multiplier
- `0` = no offset

---

## Troubleshooting

### Reading Shows 0V

**Check:**
1. Battery is connected
2. Divider circuit wired correctly
3. Correct analog pin specified

### Reading is Incorrect

**Possible causes:**
1. Wrong resistor values
2. Resistor tolerance (use 1% resistors for accuracy)
3. ADC reference voltage varies

**Solutions:**
1. Measure actual resistor values with multimeter
2. Use `SET A8 VOLTAGE <actual_R1> <actual_R2>`
3. Add correction factor if needed

### Reading is Noisy

**Solutions:**
1. Add 100nF capacitor at junction (to ground)
2. Use shorter wires
3. Route away from ignition components

---

## Hardware Recommendations

### Resistors

- Use 1% tolerance metal film resistors
- 1/4W rating is sufficient
- Consider SMD resistors for compact builds

### Protection (Optional)

Add a 5.1V zener (for 5V boards) or 3.3V zener (for 3.3V boards) from analog pin to ground for extra protection against voltage spikes.

```
Analog Pin
     |
   [Zener]
     |
    GND
```

---

## Multiple Voltage Inputs

You can monitor multiple voltage sources:

```
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A9 AUXILIARY_BATTERY VOLTAGE_DIVIDER
SET A10 IGNITION_VOLTAGE VOLTAGE_DIVIDER
SAVE
```

Each input needs its own voltage divider circuit.

---

## See Also

- [Sensor Selection Guide](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [Pin Requirements](../hardware/PIN_REQUIREMENTS_GUIDE.md) - Analog pin information

