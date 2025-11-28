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
- Changing the bias resistor value (use `VDO_BIAS_RESISTOR` in config.h)

---

## Quick Start: Custom Thermistor

**Step 1:** Define the input in `config.h`:

```cpp
#define USE_STATIC_CONFIG

#define INPUT_3_PIN            A0
#define INPUT_3_APPLICATION    OIL_TEMP
#define INPUT_3_SENSOR         VDO_150C_STEINHART  // Base sensor type
```

**Step 2:** Add custom calibration in `advanced_config.h`:

```cpp
#define INPUT_3_CUSTOM_CALIBRATION

DEFINE_CUSTOM_THERMISTOR(input_3,
    1000.0,         // bias_resistor (Ω)
    1.591e-03,      // steinhart_a
    2.659e-04,      // steinhart_b
    -1.610e-07      // steinhart_c
)
```

That's it. The custom calibration automatically overrides the preset values.

---

## How Custom Calibration Works

openEMS uses a two-level system:

1. **Preset calibration** — stored in flash (PROGMEM), loaded from sensor library
2. **Custom calibration** — stored in RAM, overrides the preset when defined

When you define `INPUT_N_CUSTOM_CALIBRATION`, the system:
1. Loads the base sensor configuration (read function, measurement type)
2. Replaces the preset calibration data with your custom values
3. Sets a flag so the read function uses your calibration

**Important:** Custom calibration only works in compile-time mode (`USE_STATIC_CONFIG`). Runtime serial configuration uses preset calibrations only.

---

## Calibration Macros

### DEFINE_CUSTOM_THERMISTOR

For NTC thermistors using Steinhart-Hart equation.

```cpp
DEFINE_CUSTOM_THERMISTOR(name,
    bias_resistor,    // Pull-down resistor value (Ω)
    steinhart_a,      // Steinhart-Hart A coefficient
    steinhart_b,      // Steinhart-Hart B coefficient
    steinhart_c       // Steinhart-Hart C coefficient
)
```

**Example:**
```cpp
#define INPUT_2_CUSTOM_CALIBRATION

DEFINE_CUSTOM_THERMISTOR(input_2,
    1000.0,         // 1kΩ bias resistor
    1.129e-03,      // A coefficient
    2.341e-04,      // B coefficient
    8.775e-08       // C coefficient
)
```

### DEFINE_CUSTOM_PRESSURE_LINEAR

For pressure sensors with linear voltage output (most 3-wire sensors).

```cpp
DEFINE_CUSTOM_PRESSURE_LINEAR(name,
    voltage_min,      // Output voltage at minimum pressure (V)
    voltage_max,      // Output voltage at maximum pressure (V)
    pressure_min,     // Minimum pressure (bar)
    pressure_max      // Maximum pressure (bar)
)
```

**Example:** 0.5-4.5V sensor, 0-10 bar range:
```cpp
#define INPUT_4_CUSTOM_CALIBRATION

DEFINE_CUSTOM_PRESSURE_LINEAR(input_4,
    0.5,    // 0.5V at 0 bar
    4.5,    // 4.5V at 10 bar
    0.0,    // 0 bar minimum
    10.0    // 10 bar maximum
)
```

### DEFINE_CUSTOM_PRESSURE_POLY

For VDO-style resistive pressure sensors using polynomial fit.

```cpp
DEFINE_CUSTOM_PRESSURE_POLY(name,
    bias_resistor,    // Pull-down resistor value (Ω)
    poly_a,           // Quadratic coefficient (P² term)
    poly_b,           // Linear coefficient (P term)
    poly_c            // Constant term
)
```

The polynomial relates resistance to pressure: `R = a×P² + b×P + c`

**Example:**
```cpp
#define INPUT_5_CUSTOM_CALIBRATION

DEFINE_CUSTOM_PRESSURE_POLY(input_5,
    1000.0,     // 1kΩ bias resistor
    -0.3682,    // poly_a
    36.465,     // poly_b
    10.648      // poly_c
)
```

### DEFINE_CUSTOM_RPM

For alternator W-phase or other RPM sensors.

```cpp
DEFINE_CUSTOM_RPM(name,
    poles,            // Number of alternator poles
    timeout_ms,       // Timeout for zero RPM detection (ms)
    min_rpm,          // Minimum valid RPM
    max_rpm           // Maximum valid RPM
)
```

**Example:** 18-pole alternator:
```cpp
#define INPUT_6_CUSTOM_CALIBRATION

DEFINE_CUSTOM_RPM(input_6,
    18,       // 18-pole alternator
    2000,     // 2 second timeout
    300,      // Ignore below 300 RPM
    8000      // Ignore above 8000 RPM
)
```

---

## Naming Convention

The calibration struct name **must** follow this pattern:

```
input_N_custom_cal
```

Where `N` matches your `INPUT_N_` definition in config.h.

| config.h Define | Calibration Name |
|-----------------|------------------|
| `INPUT_0_CUSTOM_CALIBRATION` | `input_0_custom_cal` |
| `INPUT_3_CUSTOM_CALIBRATION` | `input_3_custom_cal` |
| `INPUT_7_CUSTOM_CALIBRATION` | `input_7_custom_cal` |

The macro automatically creates the correctly-named struct.

---

## Finding Steinhart-Hart Coefficients

### Method 1: From Datasheet

Some manufacturers provide A, B, C coefficients directly. Look for "Steinhart-Hart" in the datasheet.

### Method 2: From β (Beta) Value

If your thermistor specifies a β value (e.g., β=3950):

For a 10kΩ NTC with β=3950:
```cpp
DEFINE_CUSTOM_THERMISTOR(input_N,
    10000.0,        // 10kΩ bias (match thermistor nominal)
    1.129241e-03,   // A
    2.341077e-04,   // B
    8.775468e-08    // C
)
```

For different β values, the B coefficient scales approximately:
```
B_actual ≈ B_3950 × (3950 / your_β)
```

### Method 3: Calculate from Three Points

If you have resistance measurements at three temperatures:

1. Measure resistance at three known temperatures (e.g., ice water, room temp, boiling water)
2. Use an online Steinhart-Hart calculator:
   - [SRS Thermistor Calculator](https://www.thinksrs.com/downloads/programs/therm%20calc/ntccalibrator/ntccalculator.html)
   - [Daycounter Calculator](https://www.daycounter.com/Calculators/Steinhart-Hart-Thermistor-Calculator.phtml)
3. Enter your R/T pairs and get A, B, C coefficients

**Example measurements:**
| Temperature | Resistance |
|-------------|------------|
| 0°C (ice water) | 32,650Ω |
| 25°C (room) | 10,000Ω |
| 100°C (boiling) | 680Ω |

### Method 4: Empirical Calibration

If you have a reference thermometer:

1. Start with the closest preset (e.g., `VDO_120C_STEINHART`)
2. Compare readings at several temperatures
3. If readings are consistently off, adjust coefficients
4. Small A changes shift the whole curve
5. B changes affect the slope
6. C changes affect curvature at extremes

---

## Common Thermistor Types

### Generic NTC 10K β=3950 (Most Common)

```cpp
DEFINE_CUSTOM_THERMISTOR(input_N,
    10000.0,        // 10kΩ bias resistor
    1.129241e-03,   // A
    2.341077e-04,   // B
    8.775468e-08    // C
)
```

### Generic NTC 10K β=3435

```cpp
DEFINE_CUSTOM_THERMISTOR(input_N,
    10000.0,        // 10kΩ bias resistor
    1.468e-03,      // A
    2.383e-04,      // B
    1.007e-07       // C
)
```

### Generic NTC 10K β=3380

```cpp
DEFINE_CUSTOM_THERMISTOR(input_N,
    10000.0,        // 10kΩ bias resistor
    1.579e-03,      // A
    2.349e-04,      // B
    1.027e-07       // C
)
```

---

## Bias Resistor Selection

The bias resistor value significantly affects ADC resolution. See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md) for detailed analysis.

**Quick summary:**

| Resistor | Best For |
|----------|----------|
| 1kΩ | Default, good balance (recommended) |
| 470Ω | Maximum resolution, data logging |
| 2.2kΩ | Low power |

Set the global default in `config.h`:

```cpp
#define VDO_BIAS_RESISTOR 1000.0
```

For custom calibration, specify the bias resistor in the macro:

```cpp
DEFINE_CUSTOM_THERMISTOR(input_3,
    470.0,          // Using 470Ω for this sensor
    1.591e-03,
    2.659e-04,
    -1.610e-07
)
```

---

## Complete Example

Here's a complete example with multiple custom sensors:

**config.h:**
```cpp
#define USE_STATIC_CONFIG

// Standard VDO sensor (uses preset)
#define INPUT_0_PIN            A2
#define INPUT_0_APPLICATION    COOLANT_TEMP
#define INPUT_0_SENSOR         VDO_120C_LOOKUP

// Custom thermistor (override calibration)
#define INPUT_1_PIN            A0
#define INPUT_1_APPLICATION    OIL_TEMP
#define INPUT_1_SENSOR         VDO_150C_STEINHART

// Custom pressure sensor (override calibration)
#define INPUT_2_PIN            A3
#define INPUT_2_APPLICATION    BOOST_PRESSURE
#define INPUT_2_SENSOR         GENERIC_LINEAR_PRESSURE
```

**advanced_config.h:**
```cpp
// Custom calibration for oil temp (Input 1)
#define INPUT_1_CUSTOM_CALIBRATION

DEFINE_CUSTOM_THERMISTOR(input_1,
    1000.0,         // 1kΩ bias (different from default)
    1.125e-03,      // Custom A coefficient
    2.347e-04,      // Custom B coefficient
    8.566e-08       // Custom C coefficient
)

// Custom calibration for boost sensor (Input 2)
#define INPUT_2_CUSTOM_CALIBRATION

DEFINE_CUSTOM_PRESSURE_LINEAR(input_2,
    0.5,            // 0.5V at 0 bar
    4.5,            // 4.5V at 3 bar
    0.0,            // 0 bar minimum
    3.0             // 3 bar maximum
)
```

---

## Troubleshooting

### Readings are completely wrong

- Verify the calibration name matches: `input_N_custom_cal`
- Check that `INPUT_N_CUSTOM_CALIBRATION` is defined (not commented out)
- Confirm the sensor type in config.h matches your calibration type

### Readings are close but not accurate

- Double-check your Steinhart-Hart coefficients
- Verify bias resistor value matches physical resistor
- Test against a reference thermometer at multiple temperatures

### Custom calibration not being used

- Ensure you're using compile-time mode (`USE_STATIC_CONFIG`)
- Check that advanced_config.h is included properly
- Verify the input index matches (INPUT_3 needs input_3_custom_cal)

### NAN or erratic readings

- Check wiring (sensor to analog pin, bias resistor to ground)
- Verify bias resistor is installed
- Check for loose connections

---

## Related Documentation

- [Bias Resistor Guide](../hardware/BIAS_RESISTOR_GUIDE.md) — Choosing the right bias resistor
- [Sensor Selection Guide](../sensor-types/SENSOR_SELECTION_GUIDE.md) — Preset sensor options
- [Adding Sensors](ADDING_SENSORS.md) — Adding entirely new sensor types to the library

---

**For the classic car community.**
