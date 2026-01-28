# Thermistor Temperature Sensor Guide

**Setup guide for NTC thermistor and linear temperature sensors**

---

## Overview

openEMS supports several types of temperature sensors for automotive monitoring:

1. **NTC Thermistors** - Resistive sensors with built-in or custom calibrations
2. **Linear Temperature Sensors** - 0.5-4.5V voltage output sensors

For NTC thermistors, openEMS provides three calibration methods:
- **Lookup Table** - Most accurate, uses manufacturer data points
- **Steinhart-Hart** - Mathematical equation with 3 coefficients
- **Beta Equation** - Simplified equation, common in datasheets

---

## Quick Start

### Using a Built-in Calibration

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SAVE
```

### Using Custom Steinhart-Hart Coefficients

```
SET A0 OIL_TEMP GENERIC_NTC_STEINHART
SET A0 STEINHART 1000 1.591e-03 2.659e-04 -1.610e-07
SAVE
```

### Using Beta Equation (Common in Datasheets)

```
SET A0 OIL_TEMP GENERIC_NTC_BETA
SET A0 BETA 10000 3950 10000 25
SAVE
```

### Using a Linear Temperature Sensor

```
SET A1 AMBIENT_TEMP GENERIC_TEMP_LINEAR
SAVE
```

---

## Built-in Calibrations

### VDO Sensors

| Sensor ID | Range | Method | Common Uses |
|-----------|-------|--------|-------------|
| `VDO_120C_TABLE` | -40 to 120°C | Lookup table | Coolant temperature |
| `VDO_120C_STEINHART` | -40 to 120°C | Steinhart-Hart | Coolant temperature |
| `VDO_150C_TABLE` | -40 to 150°C | Lookup table | Oil, transfer case |
| `VDO_150C_STEINHART` | -40 to 150°C | Steinhart-Hart | Oil, transfer case |

### Generic Sensors

| Sensor ID | Description |
|-----------|-------------|
| `GENERIC_NTC_TABLE` | Custom thermistor with user-defined lookup table |
| `GENERIC_NTC_STEINHART` | Custom thermistor with Steinhart-Hart coefficients |
| `GENERIC_NTC_BETA` | Custom thermistor with Beta equation |
| `GENERIC_TEMP_LINEAR` | 0.5-4.5V linear sensor (-40 to 150°C) |

---

## Calibration Methods

### Lookup Table (`_TABLE`)
- Uses manufacturer's exact resistance/temperature data points
- Interpolates between table values
- Most accurate method
- Recommended for critical monitoring

### Steinhart-Hart (`_STEINHART`)
- Uses mathematical equation: 1/T = A + B*ln(R) + C*ln(R)³
- Requires 3 coefficients (A, B, C)
- Very good accuracy for most applications
- Faster computation than table lookup

### Beta Equation (`_BETA`)
- Simplified equation: 1/T = 1/T₀ + (1/β)*ln(R/R₀)
- Requires only Beta value, reference resistance, and reference temperature
- Good accuracy over limited temperature ranges
- **Most common format in thermistor datasheets**

### Linear Sensors
- Direct voltage to temperature conversion
- No resistance measurement needed
- Typically 0.5-4.5V output range
- Common in modern aftermarket sensors

**Recommendation:** Use `_TABLE` for critical sensors (coolant), `_STEINHART` or `_BETA` for others depending on datasheet availability.

---

## Wiring

### NTC Thermistor Wiring

NTC thermistors are single-wire resistive sensors. Automotive sensors typically ground through the sensor body when threaded into the engine block.

```
+5V (or 3.3V)
    |
    |
[Thermistor] <-- Sensor (may ground through engine block)
    |
    +-----> Analog Pin (e.g., A0)
    |
[1k Resistor] <-- Pull-down resistor
    |
   GND
```

**Note:** The pull-down resistor creates a voltage divider with the thermistor. As temperature increases, thermistor resistance decreases, and voltage at the analog pin increases.

### Linear Sensor Wiring

Linear temperature sensors have 3 wires: power, ground, and signal.

```
+5V ---------> Sensor VCC
               Sensor Signal -----> Analog Pin (e.g., A1)
GND ---------> Sensor GND
```

**Warning:** Most linear sensors require 5V power. For 3.3V systems, use a voltage divider on the signal or choose 3.3V-compatible sensors.

### Pull-Down Resistor Value (NTC Only)

The default is 1k (set by DEFAULT_BIAS_RESISTOR in config.h).

| Resistor | ADC Resolution | Best For |
|----------|---------------|----------|
| 1k | Good balance | General use (recommended) |
| 470 | Better at high temps | Data logging, precision |
| 2.2k | Better at low temps | Cold climate, ambient |
| 10k | For 10k thermistors | Generic NTC sensors |

To use a different resistor:
```
SET A0 OIL_TEMP VDO_150C_TABLE
SET A0 BIAS 470
SAVE
```

---

## Custom Thermistor Configuration

### Using Beta Equation

The Beta equation is the most common format in thermistor datasheets. It requires:
- **bias_r** - Pull-down resistor value in ohms
- **beta** - Beta coefficient (typically 3000-4500 for automotive)
- **r0** - Reference resistance in ohms (typically at 25°C)
- **t0** - Reference temperature in °C (typically 25)

```
SET <pin> <function> GENERIC_NTC_BETA
SET <pin> BETA <bias_r> <beta> <r0> <t0>
SAVE
```

**Example for a 10k NTC with β=3950:**
```
SET A0 AMBIENT_TEMP GENERIC_NTC_BETA
SET A0 BETA 10000 3950 10000 25
SAVE
```

This means: 10k bias resistor, β=3950K, 10k resistance at 25°C.

### Using Steinhart-Hart Coefficients

If your datasheet provides Steinhart-Hart coefficients:

```
SET <pin> <function> GENERIC_NTC_STEINHART
SET <pin> STEINHART <bias> <a> <b> <c>
SAVE
```

**Example:**
```
SET A0 OIL_TEMP GENERIC_NTC_STEINHART
SET A0 STEINHART 1000 1.591e-03 2.659e-04 -1.610e-07
SAVE
```

### Common Thermistor Values

| Thermistor Type | Beta | R₀ @ 25°C | Steinhart A | Steinhart B | Steinhart C |
|-----------------|------|-----------|-------------|-------------|-------------|
| VDO 120°C | ~3435 | 540 | 1.764e-03 | 2.499e-04 | 6.773e-08 |
| VDO 150°C | ~3477 | 927 | 1.591e-03 | 2.659e-04 | -1.610e-07 |
| 10k NTC (generic) | 3950 | 10000 | 1.125e-03 | 2.347e-04 | 8.566e-08 |
| 100k NTC (generic) | 3950 | 100000 | 8.271e-04 | 2.088e-04 | 8.059e-08 |

### Using Custom Lookup Tables

For maximum accuracy, create a custom lookup table. See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for details.

---

## Configuration Examples

### Example 1: Coolant Temperature with Alarm

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
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

### Example 3: Generic 10k NTC Using Beta

```
SET A1 TCASE_TEMP GENERIC_NTC_BETA
SET A1 BETA 10000 3950 10000 25
SET A1 ALARM 40 120
SAVE
```

### Example 4: Linear Temperature Sensor

```
SET A3 AMBIENT_TEMP GENERIC_TEMP_LINEAR
SAVE
```

### Example 5: Multiple Temperature Sensors

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A1 TCASE_TEMP GENERIC_NTC_BETA
SET A1 BETA 10000 3950 10000 25
SET A3 AMBIENT_TEMP GENERIC_TEMP_LINEAR
SAVE
```

---

## Troubleshooting

### Reading shows -40°C or very low

**Possible causes:**
1. Open circuit (sensor disconnected)
2. Missing pull-down resistor (NTC sensors)
3. Wrong pin configured

**Check:**
- Verify sensor is connected
- Verify pull-down resistor is installed (for NTC sensors)
- Check with `INFO <pin>` command

### Reading shows maximum or very high

**Possible causes:**
1. Short circuit (signal shorted to ground)
2. Sensor not grounded (for body-ground sensors)
3. Wrong sensor type selected

**Check:**
- Verify sensor body is grounded (if applicable)
- Check for shorts in wiring
- Verify correct sensor ID

### Reading is inaccurate

**Possible causes:**
1. Wrong sensor calibration selected
2. Wrong pull-down/bias resistor value
3. Using wrong coefficients (Beta vs Steinhart-Hart)

**Solutions:**
1. Verify sensor matches calibration (check resistance at known temp)
2. Check pull-down resistor value matches your configuration
3. Try different calibration method if datasheet values don't work

### Reading is noisy/jumpy

**Solutions:**
1. Add 100nF capacitor from analog pin to GND
2. Check for loose connections
3. Route wires away from ignition components
4. Verify good ground connection

---

## Technical Reference

### VDO 120°C Resistance Table

| Temperature | Resistance |
|-------------|------------|
| 0°C | 1743 |
| 20°C | 677 |
| 40°C | 291 |
| 60°C | 134 |
| 80°C | 70 |
| 100°C | 38 |
| 120°C | 22 |

### VDO 150°C Resistance Table

| Temperature | Resistance |
|-------------|------------|
| 0°C | 3240 |
| 25°C | 927 |
| 50°C | 322 |
| 75°C | 131 |
| 100°C | 62 |
| 125°C | 32 |
| 150°C | 19 |

### Identifying Your Thermistor

**Resistance test at room temperature (~25°C):**
- VDO 120°C: ~540
- VDO 150°C: ~927
- 10k NTC: ~10000
- 100k NTC: ~100000

Use a multimeter to measure resistance between terminals (or terminal and body for body-ground sensors).

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [THERMOCOUPLE_GUIDE.md](THERMOCOUPLE_GUIDE.md) - Thermocouple temperature sensors
- [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) - Pressure sensors
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

