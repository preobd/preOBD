# Bias Resistor Selection Guide

**Optimizing ADC resolution for VDO temperature and pressure sensors**

---

## Quick Answer

| Your Priority | Recommended Resistor | Notes |
|--------------|---------------------|-------|
| **Default / Most users** | 1kΩ | Good balance of resolution and simplicity |
| **Maximum accuracy** | 470Ω | Best for data logging, slightly higher current |
| **Low power** | 2.2kΩ | Works, but wastes ADC range |

The default in openEMS is **1kΩ**. You can change this in `config.h`:

```cpp
#define DEFAULT_BIAS_RESISTOR 1000.0    // Default: 1kΩ
// #define DEFAULT_BIAS_RESISTOR 470.0  // Maximum resolution
// #define DEFAULT_BIAS_RESISTOR 2200.0 // Low power
```

---

## Why This Matters

VDO sensors are **variable resistors** — their resistance changes with temperature or pressure. To read them with a microcontroller, we create a voltage divider with a fixed "bias" resistor:

```
VCC (5V or 3.3V)
    │
    ─┤├─  VDO Sensor (variable resistance)
    │
    ├────► ADC Input (measured voltage)
    │
    ─┤├─  Bias Resistor (fixed)
    │
   GND
```

The ADC measures the voltage at the junction. From this voltage and the known bias resistor value, we calculate the sensor's resistance, then convert to temperature or pressure.

**The problem:** VDO sensors have very low resistance at operating temperatures (30-100Ω), which produces very small voltage changes with a 2.2kΩ bias resistor. A lower bias resistor produces larger voltage swings, giving better ADC resolution where it matters most.

---

## VDO Sensor Resistance Ranges

### Temperature Sensors

| Sensor | Cold (0°C) | Warm (50°C) | Hot (100°C) | Very Hot (120°C) |
|--------|-----------|-------------|-------------|------------------|
| VDO 120°C | 1,743Ω | 197Ω | 38Ω | 11Ω |
| VDO 150°C | 3,240Ω | 322Ω | 54Ω | 29Ω |

### Pressure Sensors

| Sensor | 0 bar | 2.5 bar | 5 bar |
|--------|-------|---------|-------|
| VDO 5-bar | ~11Ω | ~100Ω | ~180Ω |
| VDO 2-bar | ~10Ω | ~100Ω | ~180Ω |

Notice that at **operating temperatures** (where accuracy matters most), sensor resistance is very low — typically 30-100Ω.

---

## Resolution Comparison

### 5V Platforms (Arduino Uno/Mega) — 10-bit ADC

**Temperature sensor across operating range (50-120°C):**

| Bias Resistor | ADC Counts Used | Resolution |
|---------------|-----------------|------------|
| 2.2kΩ | 79 counts | ~0.9°C per count |
| 1kΩ | 157 counts | ~0.4°C per count |
| 470Ω | 280 counts | ~0.25°C per count |

**Pressure sensor across full range (0-5 bar):**

| Bias Resistor | ADC Counts Used | Resolution |
|---------------|-----------------|------------|
| 2.2kΩ | 72 counts | ~0.07 bar per count |
| 1kΩ | 145 counts | ~0.03 bar per count |
| 470Ω | 260 counts | ~0.02 bar per count |

### 3.3V Platforms (Teensy/Due/ESP32) — 12-bit ADC

**Temperature sensor across operating range (50-120°C):**

| Bias Resistor | ADC Counts Used | Resolution |
|---------------|-----------------|------------|
| 2.2kΩ | 316 counts | ~0.22°C per count |
| 1kΩ | 623 counts | ~0.11°C per count |
| 470Ω | 1,119 counts | ~0.06°C per count |

**Pressure sensor across full range (0-5 bar):**

| Bias Resistor | ADC Counts Used | Resolution |
|---------------|-----------------|------------|
| 2.2kΩ | 289 counts | ~0.017 bar per count |
| 1kΩ | 580 counts | ~0.009 bar per count |
| 470Ω | 1,038 counts | ~0.005 bar per count |

---

## ADC Range Utilization

This shows how much of the ADC range is actually used at **operating temperatures**:

```
                    Temperature Sensor (50-120°C)
                    
Bias R     5V/10-bit                    3.3V/12-bit
           ├──────────────────┤         ├──────────────────┤
2.2kΩ      ██░░░░░░░░░░░░░░░░  8%       ██░░░░░░░░░░░░░░░░  8%
1kΩ        ████░░░░░░░░░░░░░░ 15%       ████░░░░░░░░░░░░░░ 15%
470Ω       ██████░░░░░░░░░░░░ 27%       ███████░░░░░░░░░░░ 27%
           0%              100%         0%              100%
```

With a 2.2kΩ resistor, you're only using about **8%** of the ADC's capability at the temperatures you actually care about.

---

## Tradeoffs

### Current Draw

Lower resistance = higher current through the voltage divider:

| Bias Resistor | Current at 50Ω sensor (5V) | Current at 50Ω sensor (3.3V) |
|---------------|---------------------------|------------------------------|
| 2.2kΩ | 2.2 mA | 1.5 mA |
| 1kΩ | 4.8 mA | 3.1 mA |
| 470Ω | 9.6 mA | 6.3 mA |

For most applications, even 10mA per sensor is negligible. With 4 sensors at 470Ω, you'd draw ~40mA total — well within any Arduino or Teensy's capability.

**When current matters:**
- Battery-powered installations with no charging
- Solar-only power systems
- Many sensors (8+) on a single board

### Cold Temperature Range

Lower bias resistors compress the cold end of the range. However:
- Cold readings are typically only at **startup**
- Once the engine warms up, you're in the optimal range
- The resolution improvement at operating temps far outweighs any cold-end compression

### Self-Heating

Higher current through the sensor causes slight self-heating. With 10mA through a VDO sensor, the self-heating is typically less than 0.5°C — negligible compared to the resolution improvement.

---

## Recommendations by Use Case

### Dashboard Display Only
**Use: 1kΩ (default) or 2.2kΩ**

If you're just showing temps on an LCD and don't need precise logging, even 2.2kΩ works fine. The display shows whole numbers anyway.

### Data Logging / CAN Output
**Use: 1kΩ (default) or 470Ω**

When you're logging data or sending to OBDII apps, the extra resolution helps identify trends and spot problems early.

### Racing / Performance Monitoring
**Use: 470Ω**

Maximum resolution means you can detect smaller temperature changes faster. Worth the slightly higher current draw.

### Battery-Powered / Solar
**Use: 2.2kΩ**

When every milliamp counts, the higher resistance reduces current draw. You'll still get usable readings.

### Mixed Sensors
**Use: 1kΩ (default)**

Works well for both temperature and pressure sensors. Good compromise across all sensor types.

---

## Configuration

### Setting the Bias Resistor Value

In `config.h`, set your chosen value:

```cpp
// Bias resistor for VDO temperature and pressure sensors
// Options: 470.0 (max resolution), 1000.0 (balanced), 2200.0 (low power)
#define DEFAULT_BIAS_RESISTOR 1000.0
```

This value is used by the calibration system to correctly calculate resistance from ADC readings.

### Hardware Wiring

Use the **same physical resistor value** as configured in software:

```
VCC (5V or 3.3V)
    │
    VDO Sensor
    │
    ├────► Analog Pin (e.g., A2)
    │
    1kΩ Resistor (or your chosen value)
    │
   GND
```

**Important:** The software value must match your physical resistor. If you have 2.2kΩ resistors installed but set `DEFAULT_BIAS_RESISTOR` to 1000.0, your readings will be incorrect.

### Resistor Specifications

- **Tolerance:** 1% metal film recommended (5% works but less accurate)
- **Power rating:** 1/4W is sufficient
- **Type:** Metal film preferred for temperature stability

---

## Troubleshooting

### Readings seem wrong after changing resistor

- Verify `DEFAULT_BIAS_RESISTOR` in config.h matches physical resistor
- Check resistor is installed correctly (between analog pin and GND)
- Measure actual resistor value with multimeter (1% tolerance matters)

### Readings are stuck at maximum or minimum

- Check for open circuit (broken wire, bad connection)
- Check for short circuit (sensor wire touching ground)
- Verify sensor is actually a VDO type (resistance should change with temp)

### Noisy or unstable readings

- Add 100nF ceramic capacitor between analog pin and GND
- Check for loose connections
- Ensure good ground connection
- Try increasing `LOOP_DELAY_MS` in config.h

### Resolution still seems poor

- Verify you're using 12-bit ADC platform (Teensy, Due, ESP32) for best results
- 10-bit platforms (Uno, Mega) have inherently lower resolution
- Consider 470Ω for maximum resolution if using 10-bit ADC

---

## Technical Details

### Voltage Divider Formula

The voltage at the ADC input is:

```
V_adc = VCC × (R_bias / (R_sensor + R_bias))
```

From the ADC reading, we calculate sensor resistance:

```
R_sensor = R_bias × (VCC / V_adc - 1)
```

Or in terms of ADC counts:

```
R_sensor = R_bias × ((ADC_MAX - reading) / reading)
```

### Where Did 2.2kΩ Come From?

There's no official "industry standard" for VDO sensor bias resistors with microcontrollers. VDO sensors were originally designed for analog gauge movements (coil-based meters), not voltage dividers.

The 2.2kΩ value became common in hobbyist projects because:

1. It roughly matches the sensor resistance at 25°C (~500-1000Ω)
2. It provides usable readings across the full temperature range
3. It follows the generic thermistor rule of thumb: "match the bias to nominal resistance"
4. It's a common resistor value

However, this approach optimizes for **room temperature accuracy**, not operating temperature accuracy. For engine monitoring where we care most about 80-120°C readings, a lower bias resistor makes more sense.

### Runtime Calibration Override

In runtime configuration mode, you can override the bias resistor for individual sensors without recompiling:

```
SET A0 BIAS 2200    # Use 2.2kΩ for this specific sensor
```

This allows you to use different bias resistor values for different sensors, or adjust calibration without changing `DEFAULT_BIAS_RESISTOR` in config.h. The runtime bias setting applies to Steinhart-Hart, Lookup Table, and Pressure Polynomial calibrations.

See [SERIAL_COMMANDS.md](../../reference/SERIAL_COMMANDS.md) for details on runtime calibration commands.

### Optimal Bias Resistor Theory

For maximum sensitivity, the bias resistor should equal the sensor resistance at your most critical measurement point. For engine monitoring, that's typically around 80-100°C (coolant) or 100-120°C (oil), where sensor resistance is 30-70Ω.

However, going too low (matching 50Ω exactly) causes problems at cold temperatures. The 470Ω-1kΩ range provides a good compromise: significantly better than 2.2kΩ at operating temps, while still giving usable readings when cold.

---

## Summary

| Resistor | Resolution | Current | Best For |
|----------|------------|---------|----------|
| **470Ω** | Excellent | ~10mA | Racing, data logging, maximum accuracy |
| **1kΩ** | Good | ~5mA | **Default choice**, balanced performance |
| **2.2kΩ** | Fair | ~2mA | Low power, battery-only systems |

**The default 1kΩ is recommended for most users.** It provides 2x the resolution of 2.2kΩ with minimal tradeoffs.

