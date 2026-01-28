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

**Example:** Use 2.2kΩ instead of default 1kΩ:
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

The bias (pull-down) resistor affects measurement resolution. Default is 1kΩ.

| Resistor | Best Resolution At | Use Case |
|----------|-------------------|----------|
| 470Ω | High temperatures | Oil temp, EGT monitoring |
| 1kΩ | Mid-range (default) | General purpose |
| 2.2kΩ | Low temperatures | Ambient, intake air |
| 10kΩ | Very low temps | Cold climate |

**Rule of thumb:** Match the bias resistor to the thermistor's resistance at your most critical temperature range.

See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md) for detailed analysis.

---

## Static Builds

For static builds (`USE_STATIC_CONFIG`), use `tools/configure.py` which will prompt for custom calibrations:

```bash
python3 tools/configure.py
```

When adding sensors, it will ask:
```
Add custom calibration? [y/N]: y
```

Then guide you through the calibration parameters. The tool generates the appropriate `advanced_config.h` entries automatically.

See `tools/README_ADVANCED_CONFIG.md` for detailed static build calibration documentation.

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

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](../sensor-types/SENSOR_SELECTION_GUIDE.md) - Preset sensor options
- [VDO_SENSOR_GUIDE.md](../sensor-types/VDO_SENSOR_GUIDE.md) - VDO thermistor details
- [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md) - Bias resistor selection
- [ADDING_SENSORS.md](ADDING_SENSORS.md) - Adding new sensor types to the library

