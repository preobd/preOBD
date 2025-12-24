# VDO Temperature Sensor Guide

**Setup guide for VDO thermistor temperature sensors**

---

## Overview

VDO temperature sensors are single-wire NTC (Negative Temperature Coefficient) thermistors commonly used in European vehicles. openEMS includes factory calibrations for the two most common VDO sensors:

| Sensor | Range | Common Uses |
|--------|-------|-------------|
| VDO 120°C | -40 to 120°C | Coolant temperature |
| VDO 150°C | -40 to 150°C | Oil temperature, transfer case |

---

## Quick Start

### Coolant Temperature

```
SET A2 COOLANT_TEMP VDO_120C_LOOKUP
SAVE
```

### Oil Temperature

```
SET A0 OIL_TEMP VDO_150C_STEINHART
SAVE
```

---

## Sensor Selection

### VDO 120°C Sensors

| Sensor ID | Method | Accuracy | Speed |
|-----------|--------|----------|-------|
| `VDO_120C_LOOKUP` | Lookup table | ±0.5°C | Slower |
| `VDO_120C_STEINHART` | Steinhart-Hart | ±1°C | Faster |

**Best for:** Coolant temperature (normal operating range 80-100°C)

### VDO 150°C Sensors

| Sensor ID | Method | Accuracy | Speed |
|-----------|--------|----------|-------|
| `VDO_150C_LOOKUP` | Lookup table | ±0.5°C | Slower |
| `VDO_150C_STEINHART` | Steinhart-Hart | ±1°C | Faster |

**Best for:** Oil temperature, transfer case (can exceed 120°C)

### Lookup vs. Steinhart-Hart

**Lookup Table (`_LOOKUP`):**
- Uses manufacturer's exact resistance/temperature data points
- Interpolates between table values
- Most accurate method
- Recommended for critical monitoring

**Steinhart-Hart (`_STEINHART`):**
- Uses mathematical equation with curve-fitted coefficients
- Direct calculation (no interpolation)
- Very good accuracy for most applications
- Faster computation

**Recommendation:** Use `_LOOKUP` for critical sensors (coolant), `_STEINHART` for others.

---

## Wiring

### Basic Wiring Diagram

VDO temperature sensors are single-wire resistive sensors. They ground through the sensor body when threaded into the engine block.

```
+5V (or 3.3V)
    |
    |
[Thermistor] ← VDO sensor (grounds through engine block)
    |
    +----→ Analog Pin (e.g., A0)
    |
[1kΩ Resistor] ← Pull-down resistor
    |
   GND
```

### Wiring Steps

1. **Connect signal wire** from VDO sensor to analog pin
2. **Add pull-down resistor** from analog pin to GND (1kΩ default)
3. **Verify ground** - sensor body must be grounded through engine block

**Note:** The pull-down resistor creates a voltage divider with the thermistor. As temperature increases, thermistor resistance decreases, and voltage at the analog pin increases.

### Pull-Down Resistor Value

The default is 1kΩ (set by DEFAULT_BIAS_RESISTOR in config.h).

| Resistor | ADC Resolution | Best For |
|----------|---------------|----------|
| 1kΩ | Good balance | General use (recommended) |
| 470Ω | Better at high temps | Data logging, precision |
| 2.2kΩ | Better at low temps | Cold climate, ambient |

To use a different resistor:
```
SET A0 OIL_TEMP VDO_150C_LOOKUP
SET A0 BIAS 470
SAVE
```

---

## Configuration Examples

### Example 1: Coolant Temperature with Alarm

```
SET A2 COOLANT_TEMP VDO_120C_LOOKUP
SET A2 ALARM 60 105
SAVE
```

Alarm triggers below 60°C (not warmed up) or above 105°C (overheating).

### Example 2: Oil Temperature

```
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A0 ALARM 60 130
SAVE
```

### Example 3: Transfer Case Temperature

```
SET A1 TCASE_TEMP VDO_150C_LOOKUP
SET A1 ALARM 40 120
SAVE
```

### Example 4: Multiple Temperature Sensors

```
SET A2 COOLANT_TEMP VDO_120C_LOOKUP
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A1 TCASE_TEMP VDO_150C_STEINHART
SAVE
```

---

## Troubleshooting

### Reading shows -40°C or very low

**Possible causes:**
1. Open circuit (sensor disconnected)
2. Missing pull-down resistor
3. Wrong pin configured

**Check:**
- Verify sensor is connected
- Verify pull-down resistor is installed
- Check with `INFO <pin>` command

### Reading shows maximum or very high

**Possible causes:**
1. Short circuit (signal shorted to ground)
2. Sensor not grounded through engine block
3. Wrong sensor type selected

**Check:**
- Verify sensor body is grounded
- Check for shorts in wiring
- Verify correct sensor ID (120C vs 150C)

### Reading is inaccurate

**Possible causes:**
1. Wrong sensor type (120C vs 150C)
2. Wrong pull-down resistor value
3. Non-VDO sensor being used

**Solutions:**
1. Verify sensor part number matches calibration
2. Check pull-down resistor value
3. Use custom calibration for non-VDO sensors

### Reading is noisy/jumpy

**Solutions:**
1. Add 100nF capacitor from analog pin to GND
2. Check for loose connections
3. Route wires away from ignition components
4. Verify good ground connection

---

## Technical Details

### VDO 120°C Resistance Table

| Temperature | Resistance |
|-------------|------------|
| 0°C | 1743 Ω |
| 20°C | 677 Ω |
| 40°C | 291 Ω |
| 60°C | 134 Ω |
| 80°C | 70 Ω |
| 100°C | 38 Ω |
| 120°C | 22 Ω |

### VDO 150°C Resistance Table

| Temperature | Resistance |
|-------------|------------|
| 0°C | 3240 Ω |
| 25°C | 927 Ω |
| 50°C | 322 Ω |
| 75°C | 131 Ω |
| 100°C | 62 Ω |
| 125°C | 32 Ω |
| 150°C | 19 Ω |

### Steinhart-Hart Coefficients

**VDO 120°C:**
- A = 1.764e-03
- B = 2.499e-04
- C = 6.773e-08

**VDO 150°C:**
- A = 1.591e-03
- B = 2.659e-04
- C = -1.610e-07

---

## Identifying VDO Sensors

### Visual Identification

- VDO sensors typically have a brass or steel body
- Thread sizes: M10, M12, M14, M16 common
- Single terminal (signal wire only)
- Ground through threaded body

### Resistance Test

With sensor at room temperature (~25°C):
- VDO 120°C: ~540 Ω
- VDO 150°C: ~927 Ω

Use a multimeter to measure resistance between terminal and sensor body.

---

## Non-VDO Sensors

If you have a different brand of thermistor, you have two options:

### Option 1: Generic Thermistor with Custom Calibration

```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 STEINHART <bias> <a> <b> <c>
SAVE
```

Where `<a>`, `<b>`, `<c>` are Steinhart-Hart coefficients for your sensor.

### Option 2: Create Custom Lookup Table

See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for creating custom lookup tables.

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) - VDO pressure sensors
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

---

**For the classic car community.**
