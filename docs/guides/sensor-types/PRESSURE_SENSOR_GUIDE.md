# Pressure Sensor Guide

**Complete guide to pressure sensor setup in openEMS**

---

## Overview

openEMS supports two types of pressure sensors:

1. **VDO Resistive Sensors** - VDO brand sensors with non-linear characteristics
2. **Linear Voltage Sensors** - Generic 0.5-4.5V output sensors

---

## Quick Start

### VDO 5-Bar Oil Pressure

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SAVE
```

### VDO 2-Bar Boost Pressure

```
SET A4 BOOST_PRESSURE VDO_2BAR_CURVE
SAVE
```

### Generic Linear Sensor

```
SET A5 BOOST_PRESSURE GENERIC_BOOST
SAVE
```

---

## Available Pressure Sensors

### VDO Sensors

VDO pressure sensors are available with two calibration methods:

| Sensor ID | Calibration | Range | Notes |
|-----------|-------------|-------|-------|
| `VDO_5BAR_CURVE` | Polynomial curve fit | 0-5 bar (0-73 PSI) | Oil pressure, fuel pressure |
| `VDO_2BAR_CURVE` | Polynomial curve fit | 0-2 bar (0-29 PSI) | Boost/intake pressure |
| `VDO_5BAR_TABLE` | Lookup table | 0-5 bar (0-73 PSI) | Datasheet points interpolated |
| `VDO_2BAR_TABLE` | Lookup table | 0-2 bar (0-29 PSI) | Datasheet points interpolated |

**CURVE vs TABLE:**
- **CURVE** (polynomial): Uses curve-fitted equation, smoother between points
- **TABLE** (lookup): Uses manufacturer datasheet points with linear interpolation, most accurate at specified points

**Characteristics:**
- Single-wire resistive sensors
- Non-linear resistance output
- Very accurate (±1% typical)
- Common in European vehicles

### Generic Linear Sensors

| Sensor ID | Range | Voltage |
|-----------|-------|---------|
| `GENERIC_BOOST` | 0-5 bar | 0.5-4.5V |
| `MPX4250AP` | 20-250 kPa | 0.2-4.7V |
| `MPX5700AP` | 15-700 kPa | 0.2-4.7V |

**Characteristics:**
- Three-wire voltage output sensors
- Linear voltage-to-pressure relationship
- Industry standard pinout
- Available from many manufacturers

---

## Wiring

### VDO Pressure Sensors (Single-Wire Resistive)

VDO pressure sensors are single-wire resistive sensors that ground through the sensor body when mounted to the engine block.

```
VDO Sensor:
  Signal wire → Analog pin (e.g., A3)
  Ground → Chassis ground (sensor body grounds through engine block)

Required: Pull-down resistor from analog pin to GND
  Analog pin → 1kΩ resistor → GND
```

**Note:** The pull-down resistor value is set by DEFAULT_BIAS_RESISTOR in config.h (default 1kΩ). You can override per-sensor with the `SET <pin> BIAS <ohms>` command.

### Generic 3-Wire Voltage Sensors

```
Generic Sensor:
  Pin 1 (Signal) → Analog pin
  Pin 2 (+5V)    → 5V supply
  Pin 3 (Ground) → GND

No external resistors needed!
```

**⚠️ Warning for 3.3V boards:** Many 0.5-4.5V sensors output up to 4.5V which can damage 3.3V boards (Teensy, Due, ESP32). Use a voltage divider or level shifter, or choose a 3.3V-compatible sensor.

---

## Configuration Examples

### Example 1: Oil Pressure with Alarm

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 ALARM 0.5 6
SAVE
```

The alarm triggers below 0.5 bar (low oil pressure warning) or above 6 bar.

### Example 2: Turbo Boost Pressure

```
SET A4 BOOST_PRESSURE VDO_2BAR_CURVE
SET A4 ALARM -1 1.5
SAVE
```

The alarm triggers if boost exceeds 1.5 bar (-1 means no low alarm).

### Example 3: Multiple Pressure Sensors

```
# Oil pressure
SET A3 OIL_PRESSURE VDO_5BAR_CURVE

# Boost pressure
SET A4 BOOST_PRESSURE VDO_2BAR_CURVE

# Fuel pressure
SET A5 FUEL_PRESSURE VDO_5BAR_CURVE

SAVE
```

---

## Custom Calibration

### Adjust Bias Resistor

If you're using a different bias resistor than the default 1kΩ:

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 BIAS 2200
SAVE
```

### Custom Linear Pressure Sensor

For a linear 0.5-4.5V sensor with a different range:

```
SET A5 BOOST_PRESSURE GENERIC_BOOST
SET A5 PRESSURE_LINEAR 0.5 4.5 0.0 3.0
SAVE
```

Parameters: `voltage_min voltage_max pressure_min pressure_max`

### Custom Polynomial Sensor

For VDO-style polynomial calibration with custom coefficients:

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 PRESSURE_POLY 1000 -0.3682 36.465 10.648
SAVE
```

Parameters: `bias_resistor poly_a poly_b poly_c`

See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for more custom calibration options.

---

## Troubleshooting

### Sensor reads 0 or maximum constantly

**Check:**
1. Wiring connections - most common issue
2. Pull-down resistor installed (for VDO sensors)
3. Sensor has power (for 3-wire sensors)
4. Measure voltage at analog pin with multimeter

**Debug with serial:**
```
INFO A3
```
This shows the raw ADC value and calculated pressure.

### Readings are unstable/noisy

**Solutions:**
1. Add 100nF capacitor at analog pin (signal to ground)
2. Add 100Ω series resistor before capacitor
3. Check ground connections
4. Route wires away from ignition components
5. Use shielded cable for long runs

### Readings are offset

**For VDO sensors:**
This is normal - VDO sensors have a non-zero resistance at 0 pressure. The polynomial calibration handles this correctly.

**For linear sensors:**
Check the voltage_min parameter matches your sensor's actual zero-pressure output (typically 0.5V).

### Wrong units displayed

```
SET A3 UNITS PSI
SAVE
```

Or use `BAR` or `KPA` as needed.

---

## Technical Details

### VDO Polynomial Equation

VDO resistive pressure sensors follow a polynomial resistance curve:

```
Resistance = A×P² + B×P + C

Where:
- P = Pressure in bar
- A, B, C = Polynomial coefficients (sensor-specific)
```

**VDO 5-bar coefficients:**
- A = -0.3682
- B = 36.465
- C = 10.648

**VDO 2-bar coefficients:**
- A = -3.1515
- B = 93.686
- C = 9.6307

### Linear Sensor Equation

Linear sensors follow a simple voltage-to-pressure relationship:

```
Pressure = (Voltage - V_min) × (P_max - P_min) / (V_max - V_min) + P_min

Where:
- V_min = Voltage at minimum pressure (typically 0.5V)
- V_max = Voltage at maximum pressure (typically 4.5V)
- P_min = Minimum pressure (typically 0)
- P_max = Maximum pressure (sensor range)
```

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [VDO_SENSOR_GUIDE.md](VDO_SENSOR_GUIDE.md) - VDO temperature sensors
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

