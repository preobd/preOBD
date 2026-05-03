# Advanced Calibration Guide

**Custom calibration for sensors not in the preset library**

---

## When Do You Need Custom Calibration?

Most users won't need this guide. The preset sensor library covers common automotive sensors including VDO temperature and pressure senders, thermocouples, and generic NTC thermistors.

**You need custom calibration when:**

- Using a thermistor not in the preset library
- Your sensor requires different Steinhart-Hart coefficients
- You have a pressure sensor with non-standard voltage output
- Fine-tuning accuracy based on testing with a reference thermometer
- Using a different bias resistor than the default

**You don't need custom calibration for:**

- VDO 120°C or 150°C temperature sensors (use presets)
- VDO 2-bar or 5-bar pressure sensors (use presets)
- MAX6675 or MAX31855 thermocouples (no calibration needed)
- Changing just the bias resistor value (use `SET <pin> BIAS <ohms>`)

---

## Quick Start

### Custom Thermistor (Steinhart-Hart)

```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 STEINHART 10000 1.129e-3 2.341e-4 8.775e-8
SAVE
```

### Custom Thermistor (Beta Equation)

```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 BETA 10000 3950 10000 25
SAVE
```

### Custom Linear Pressure Sensor

```
SET A1 BOOST_PRESSURE GENERIC_BOOST
SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 3.0
SAVE
```

### Adjust Bias Resistor Only

```
SET A0 BIAS 2200
SAVE
```

---

## Calibration Commands Reference

### STEINHART - Steinhart-Hart Thermistor

```
SET <pin> STEINHART <bias> <a> <b> <c>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `bias` | Pull-down resistor (Ω) | 10000 |
| `a` | Steinhart-Hart A coefficient | 1.129e-3 |
| `b` | Steinhart-Hart B coefficient | 2.341e-4 |
| `c` | Steinhart-Hart C coefficient | 8.775e-8 |

**Example:** 10kΩ NTC thermistor with known coefficients:
```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 STEINHART 10000 1.129e-3 2.341e-4 8.775e-8
SAVE
```

### BETA - Beta Equation Thermistor

```
SET <pin> BETA <bias> <beta> <r0> <t0>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `bias` | Pull-down resistor (Ω) | 10000 |
| `beta` | Beta coefficient (K) | 3950 |
| `r0` | Resistance at T0 (Ω) | 10000 |
| `t0` | Reference temperature (°C) | 25 |

**Example:** Generic 10kΩ NTC with β=3950:
```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 BETA 10000 3950 10000 25
SAVE
```

The Beta equation is simpler than Steinhart-Hart when you know the Beta value (often in datasheets). Accuracy is typically ±2°C.

### PRESSURE_LINEAR - Linear Voltage Sensor

```
SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `vmin` | Voltage at minimum pressure (V) | 0.5 |
| `vmax` | Voltage at maximum pressure (V) | 4.5 |
| `pmin` | Minimum pressure (bar) | 0.0 |
| `pmax` | Maximum pressure (bar) | 3.0 |

**Example:** 0.5-4.5V sensor, 0-3 bar range:
```
SET A1 BOOST_PRESSURE GENERIC_BOOST
SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 3.0
SAVE
```

**Disconnect detection:** Linear sensors return `NaN` when the ADC voltage falls more than ~0.05V outside `vmin`/`vmax` — disconnected, unpowered, or shorted sensors no longer produce a plausible mid-range reading. Built-in linear presets enable an internal pull-up so a disconnected pin rails high; **custom calibrations do not**, because the EEPROM-backed override layout is fixed. Wire an external pull-down (e.g. 100kΩ to GND) if you want disconnect detection on a custom-calibrated linear sensor.

**Voltage dividers:** When a 5V linear sensor is wired through a hardware divider for a 3.3V ADC, leave `vmin`/`vmax` at the sensor-side voltages (typically 0.5/4.5V) and apply the divider with `SET <pin> DIVIDER <ratio>` (see [DIVIDER](#divider--voltage-divider-ratio-linear-sensors-only) below). Custom calibrations don't carry a `divider_ratio` field of their own — `CalibrationOverride` is fixed at 16 bytes, so the divider lives on the input. Alternately, you can specify post-divider voltages directly in `PRESSURE_LINEAR` and skip `DIVIDER`.

### DIVIDER - Voltage Divider Ratio (linear sensors only)

```
SET <pin> DIVIDER <ratio>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `ratio` | `V_at_pin / V_at_sensor`, in `(0.0, 1.0]` | 0.6 |

Use when a linear sensor's output is scaled by a hardware voltage divider before reaching the ADC (typically a 5V sensor on a 3.3V board). The cal stays expressed in raw sensor terms; `readLinearSensor()` unscales the ADC voltage through this ratio before comparing to `vmin`/`vmax`.

This is a per-input property (different inputs can have different dividers) and is persisted to EEPROM on `SAVE`. Default is 1.0 (no divider). Only linear-calibration sensors honor the field — VDO polynomial, thermistor, and `VOLTAGE_DIVIDER` battery inputs ignore it.

**Example:** MPX4250 on a Teensy 4.1 behind a 2.2kΩ/3.3kΩ divider:
```
SET A1 BOOST_PRESSURE MPX4250AP
SET A1 DIVIDER 0.6
SAVE
```

### PRESSURE_POLY - Polynomial Pressure Sensor

```
SET <pin> PRESSURE_POLY <bias> <a> <b> <c>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `bias` | Pull-down resistor (Ω) | 1000 |
| `a` | Quadratic coefficient (P² term) | -0.3682 |
| `b` | Linear coefficient (P term) | 36.465 |
| `c` | Constant term | 10.648 |

The polynomial relates resistance to pressure: `R = a×P² + b×P + c`

**Example:** Custom VDO-style sensor:
```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 PRESSURE_POLY 1000 -0.3682 36.465 10.648
SAVE
```

### RPM - Alternator RPM Calibration

```
SET <pin> RPM <poles> <ratio> <timeout> <min> <max>
SET <pin> RPM <poles> <ratio> <mult> <timeout> <min> <max>
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `poles` | Alternator pole count | 12 |
| `ratio` | Pulley ratio (alt:engine) | 3.0 |
| `mult` | Fine-tuning multiplier (optional) | 1.02 |
| `timeout` | Zero RPM timeout (ms) | 2000 |
| `min` | Minimum valid RPM | 100 |
| `max` | Maximum valid RPM | 8000 |

**Example:** 14-pole alternator, 2.5:1 ratio:
```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 RPM 14 2.5 2000 100 8000
SAVE
```

**Example:** Fine-tuned after testing:
```
SET 5 RPM 12 3.0 1.02 2000 100 8000
SAVE
```

### BIAS - Change Bias Resistor Only

```
SET <pin> BIAS <ohms>
```

**Example:** Override bias for a sensor wired with a non-standard resistor:
```
SET A0 BIAS 2200
SAVE
```

### View Current Calibration

```
INFO <pin> CALIBRATION
```

### Revert to Preset

```
SET <pin> CALIBRATION PRESET
SAVE
```

---

## Finding Steinhart-Hart Coefficients

### Method 1: From Datasheet

Some manufacturers provide A, B, C coefficients directly. Look for "Steinhart-Hart" in the datasheet.

### Method 2: From Beta Value

Most cheap NTC thermistors specify a β (Beta) value. Use the `BETA` command directly:

```
SET A0 BETA 10000 3950 10000 25
```

Or convert to Steinhart-Hart approximately:

| Beta Value | Typical A | Typical B | Typical C |
|------------|-----------|-----------|-----------|
| β=3380 | 1.40e-3 | 2.37e-4 | 9.90e-8 |
| β=3435 | 1.35e-3 | 2.39e-4 | 9.50e-8 |
| β=3950 | 1.13e-3 | 2.34e-4 | 8.78e-8 |
| β=4050 | 1.10e-3 | 2.35e-4 | 8.50e-8 |

### Method 3: Calculate from Three Points

If you have resistance measurements at three known temperatures:

1. Measure resistance at three temperatures (e.g., ice water 0°C, room 25°C, hot water 60°C)
2. Use an online Steinhart-Hart calculator
3. Enter the three R/T pairs to get A, B, C coefficients

**Recommended calculator:** Search for "Steinhart-Hart coefficient calculator"

---

## Bias Resistor Selection

The correct bias resistor depends on the sensor's resistance family, not a single universal default.

| Sensor Family | Resistance Range | Recommended Bias |
|---------------|-----------------|-----------------|
| VDO, Smiths, Stewart Warner, pre-EFI Ford/GM, Jeep CJ | 2–330Ω (low-impedance) | **100Ω** |
| GM EFI NTC, Bosch NTC M12, Jeep XJ/Renix CTS | 135–9400Ω (high-impedance) | **2.49kΩ** |

**Rule of thumb:** Match the bias resistor to the sensor's resistance family. The built-in presets already encode the correct value — only use `SET <pin> BIAS` if your physical resistor differs from the preset default.

See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md) for detailed analysis.

---

## Examples

### Example 1: Generic 10kΩ NTC Thermistor

You have a cheap 10kΩ NTC thermistor with β=3950.

```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 BETA 10000 3950 10000 25
SET A0 ALARM 60 130
SAVE
```

### Example 2: Bosch Temperature Sender

You have a Bosch temperature sender with known coefficients.

```
SET A1 COOLANT_TEMP THERMISTOR_STEINHART
SET A1 STEINHART 1000 1.46e-3 2.38e-4 1.01e-7
SET A1 ALARM 60 105
SAVE
```

### Example 3: Chinese MAP Sensor

You have a generic 0.5-4.5V MAP sensor rated 0-3 bar.

```
SET A2 BOOST_PRESSURE GENERIC_BOOST
SET A2 PRESSURE_LINEAR 0.5 4.5 0.0 3.0
SET A2 ALARM -1 2.0
SAVE
```

### Example 4: Fine-Tuning VDO Sensor

Your VDO sensor reads 3% low compared to a reference thermometer.

```
# First, check current calibration
INFO A0 CALIBRATION

# Apply 3% correction by adjusting bias resistor
# If readings are low, try a slightly lower bias
SET A0 BIAS 970
SAVE
```

### Example 5: Custom RPM for Unusual Alternator

You have a 16-pole alternator with a 2.8:1 pulley ratio.

```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 RPM 16 2.8 2000 100 8000
SAVE

# After testing, readings are 1.5% high
SET 5 RPM 16 2.8 0.985 2000 100 8000
SAVE
```

---

## Troubleshooting

### Readings are completely wrong

- Verify the sensor type matches your physical sensor
- Check that calibration command syntax is correct
- Use `INFO <pin> CALIBRATION` to verify settings were saved

### Readings are close but not accurate

- Fine-tune bias resistor value
- For thermistors: verify Steinhart-Hart coefficients
- For RPM: adjust calibration multiplier
- Compare against a known reference at multiple points

### Calibration not being used

- Ensure you ran `SAVE` after setting calibration
- Check `INFO <pin>` to verify custom calibration is active
- Try `SET <pin> CALIBRATION PRESET` then re-enter your calibration

### NaN or erratic readings

- Check wiring (sensor to analog pin, bias resistor to ground)
- Verify bias resistor is installed
- Check for loose connections
- For linear sensors: a persistent `NaN` is the disconnect-detection signal — the ADC voltage is more than ~0.05V outside the calibrated `vmin`/`vmax` window. Verify sensor power, signal continuity, and (for custom calibrations) that an external pull-down is present so a disconnected pin rails predictably.
- If a linear sensor sits behind a hardware voltage divider, confirm `SET <pin> DIVIDER <ratio>` matches the actual divider — otherwise the unscaled voltage will fall outside `vmin`/`vmax` and read NaN.

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](../sensor-types/SENSOR_SELECTION_GUIDE.md) - Preset sensor options
- [THERMISTOR_GUIDE.md](../sensor-types/THERMISTOR_GUIDE.md) - Resistive temperature sensors (NTC, VDO senders)
- [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md) - Bias resistor selection
- [ADDING_SENSORS.md](ADDING_SENSORS.md) - Adding new sensor types to the library

