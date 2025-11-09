# Advanced Calibration Guide

This guide is for advanced users who need to customize sensor calibrations beyond the presets.

## When Do You Need Custom Calibration?

- Your thermistor uses a non-standard bias resistor
- You have a thermistor not in the preset library
- You want to fine-tune accuracy based on testing
- You have custom lookup tables from a datasheet
- Your sensor requires unique calibration constants

## Method 1: Override Steinhart-Hart Coefficients

### Example: Custom Coolant Sensor

You have a thermistor with known Steinhart-Hart coefficients that differ from the presets.

**In config.h:**
```cpp
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   CUSTOM_THERMISTOR_STEINHART  // Use custom type
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100

// Define custom calibration
#define COOLANT_CUSTOM_CALIBRATION
#define COOLANT_BIAS_RESISTOR     470.0         // Your bias resistor
#define COOLANT_STEINHART_A       1.597491234   // From datasheet
#define COOLANT_STEINHART_B       2.63014794    // or calculated
#define COOLANT_STEINHART_C      -1.184237497   // from curve fit
```

### Finding Steinhart-Hart Coefficients

#### Method A: From Datasheet
Some manufacturers provide A, B, C coefficients directly.

#### Method B: From β (Beta) Value
If you only have β:
```
A = 1.129241e-3
B = 2.341077e-4  
C = 8.775468e-8

// Adjust B based on β:
B_actual = B × (3950 / your_β)
```

#### Method C: Calculate from Three Points
If you have resistance at three temperatures:

1. Use an online Steinhart-Hart calculator
2. Input your three R/T pairs
3. Get A, B, C coefficients

Good websites:
- https://www.thinksrs.com/downloads/programs/therm%20calc/ntccalibrator/ntccalculator.html
- https://www.daycounter.com/Calculators/Steinhart-Hart-Thermistor-Calculator.phtml

## Method 2: Custom Lookup Tables

### Example: Custom Thermistor with Manufacturer Table

You have a datasheet with a resistance vs. temperature table.

**Step 1: Add your table to `sensor_configs.h`**

```cpp
// Custom sensor lookup tables
static const float custom_thermistor_resistance[] = {
    5000.0, 4200.0, 3500.0, 2900.0, 2400.0, 2000.0, 1650.0, 1370.0,
    // ... your values from datasheet
};

static const float custom_thermistor_temperature[] = {
    0, 10, 20, 30, 40, 50, 60, 70,
    // ... matching temperatures
};

static const ThermistorLookupCalibration custom_lookup_cal = {
    .bias_resistor = 10000.0,  // Your bias resistor
    .resistance_table = custom_thermistor_resistance,
    .temperature_table = custom_thermistor_temperature,
    .table_size = 8  // Number of points
};
```

**Step 2: Add to sensor config database**

```cpp
{
    .sensorId = CUSTOM_THERMISTOR_LOOKUP,
    .name = "My Custom Thermistor",
    .internalType = THERMISTOR_LOOKUP,
    .readFunction = readThermistorLookup,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp,
    .calibrationType = CAL_THERMISTOR_LOOKUP,
    .calibrationData = (void*)&custom_lookup_cal
},
```

**Step 3: Use in config.h**

```cpp
#define COOLANT_SENSOR_TYPE   CUSTOM_THERMISTOR_LOOKUP
```

## Method 3: Multiple Custom Sensors

If you have multiple custom sensors with different calibrations:

**In config.h:**
```cpp
// Coolant - custom 470Ω bias
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   CUSTOM_THERMISTOR_STEINHART
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_CUSTOM_CALIBRATION
#define COOLANT_BIAS_RESISTOR     470.0
#define COOLANT_STEINHART_A       1.299e-3
#define COOLANT_STEINHART_B       2.401e-4
#define COOLANT_STEINHART_C       1.301e-7

// Oil - different custom calibration
#define ENABLE_OIL_TEMP
#define OIL_TEMP_SENSOR_TYPE  CUSTOM_THERMISTOR_STEINHART
#define OIL_TEMP_INPUT        A0
#define OIL_TEMP_CUSTOM_CALIBRATION
#define OIL_BIAS_RESISTOR         2200.0
#define OIL_STEINHART_A           1.456e-3
#define OIL_STEINHART_B           2.567e-4
#define OIL_STEINHART_C           1.456e-7
```

Each sensor gets its own calibration!

## Method 4: Runtime Calibration from SD Card

### Coming Soon!

The architecture supports loading calibrations from SD card at runtime:

```cpp
// Pseudo-code for future feature
ThermistorSteinhartCalibration cal;
loadCalibrationFromSD("coolant_cal.bin", &cal);
coolantTemp.calibrationData = &cal;
```

This will allow:
- Storing multiple calibration profiles
- Swapping sensors without recompiling
- Fine-tuning calibration in the field
- Sharing calibrations between users

## Calibration Best Practices

### 1. Start with a Preset
Even if you need custom calibration, start with the closest preset to ensure your wiring is correct.

### 2. Validate Against Known Temperature
Use an accurate thermometer to validate your sensor at a known temperature (ice water = 0°C, boiling water = 100°C).

### 3. Document Your Calibration
Add comments to config.h explaining your calibration source:
```cpp
// Calibration from XYZ Sensor datasheet rev 2.3
// Validated with ice water test: 0.2°C error
#define COOLANT_STEINHART_A  1.299e-3
```

### 4. Use Lookup Tables for Critical Sensors
For critical sensors (CHT, EGT), prefer lookup tables over Steinhart-Hart when available.

### 5. Test Over Expected Range
Don't just test at room temperature - test at the actual operating range.

## Calculating Bias Resistor Value

The bias resistor affects the temperature range and resolution:

**Formula:**
```
R_bias ≈ R_thermistor_at_midpoint
```

For example:
- If your sensor is 10kΩ at 25°C, use a 10kΩ bias
- If your sensor is 2.2kΩ at 50°C, use a 2.2kΩ bias

**Why it matters:**
- Too high: Poor resolution at high temperatures
- Too low: Poor resolution at low temperatures
- Just right: Best resolution across your operating range

## Common Thermistor Types

### NTC 10K β=3950 (Most Common)
```cpp
#define BIAS_RESISTOR  10000.0
#define STEINHART_A    1.129241e-3
#define STEINHART_B    2.341077e-4
#define STEINHART_C    8.775468e-8
```

### NTC 10K β=3435
```cpp
#define BIAS_RESISTOR  10000.0
#define STEINHART_A    1.468e-3
#define STEINHART_B    2.383e-4
#define STEINHART_C    1.007e-7
```

### VDO 120°C (120°C range, for coolant)
```cpp
#define BIAS_RESISTOR  2200.0
// Use VDO_120C_LOOKUP or VDO_120C_STEINHART preset
```

### VDO 150°C (150°C range, for oil)
```cpp
#define BIAS_RESISTOR  2200.0
// Use VDO_150C_LOOKUP or VDO_150C_STEINHART preset
```

## Troubleshooting Calibration

### Readings Too High/Low Across Range
- Check bias resistor value
- Verify wiring (thermistor to ADC, bias to ground)
- Confirm ADC reference voltage setting

### Readings Accurate at One Point, Wrong Elsewhere
- Your Steinhart coefficients may be incorrect
- Consider using lookup table instead
- Verify β value if using beta-parameter

### Erratic Readings
- Add 100nF capacitor across thermistor
- Check for loose connections
- Increase LOOP_DELAY_MS
- Enable ADC averaging in platform.h

### Readings Always Show NAN
- Check that calibrationData pointer is valid
- Verify calibrationType matches read function
- Check sensor is actually connected

## Example: Complete Custom Sensor

Here's a complete example of adding a totally custom sensor:

**1. Add calibration to sensor_configs.h:**
```cpp
static const ThermistorSteinhartCalibration my_sensor_cal = {
    .bias_resistor = 4700.0,
    .steinhart_a = 1.234e-3,
    .steinhart_b = 2.345e-4,
    .steinhart_c = 3.456e-7
};

// Add to SENSOR_CONFIGS array:
{
    .sensorId = 99,  // Pick unused number
    .name = "My Custom Sensor",
    .internalType = THERMISTOR_STEINHART,
    .readFunction = readThermistorSteinhart,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp,
    .calibrationType = CAL_THERMISTOR_STEINHART,
    .calibrationData = (void*)&my_sensor_cal
}
```

**2. Add to sensor_library.h:**
```cpp
#define MY_CUSTOM_SENSOR  99
```

**3. Use in config.h:**
```cpp
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE  MY_CUSTOM_SENSOR
#define COOLANT_TEMP_INPUT   A2
```

Done! Your custom sensor is now part of the library.

## Getting Help

If you're stuck on calibration:
1. Post your sensor datasheet on GitHub Discussions
2. Share your three-point resistance/temperature measurements
3. Community can help calculate coefficients
4. Consider contributing your calibration back to the project!
