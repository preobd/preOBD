# Pressure Sensor Configuration Guide

## Overview

openEMS supports two types of pressure sensors:
1. **Linear sensors** - Most modern automotive sensors (0.5-4.5V output)
2. **VDO polynomial sensors** - VDO brand sensors with non-linear characteristics

## Quick Selection Guide

### What sensor do you have?

**VDO 5-bar oil pressure sensor?**
```cpp
#define OIL_PRESSURE_SENSOR_TYPE  SENSOR_VDO_5BAR_PRESSURE
```

**VDO 2-bar boost pressure sensor?**
```cpp
#define BOOST_PRESSURE_SENSOR_TYPE  SENSOR_VDO_2BAR_PRESSURE
```

**Generic 0-5 bar sensor (0.5-4.5V)?**
```cpp
#define PRESSURE_SENSOR_TYPE  SENSOR_GENERIC_0_5V_5BAR
```

**Generic 0-10 bar sensor (0.5-4.5V)?**
```cpp
#define PRESSURE_SENSOR_TYPE  SENSOR_GENERIC_0_5V_10BAR
```

**Generic 0-100 psi sensor (0.5-4.5V)?**
```cpp
#define PRESSURE_SENSOR_TYPE  SENSOR_GENERIC_0_5V_100PSI
```

**Freescale MPX4250AP MAP sensor?**
```cpp
#define PRESSURE_SENSOR_TYPE  SENSOR_MPX4250AP
```

## Available Pressure Sensors

### VDO Sensors (Polynomial Calibration)

| Sensor ID | Range | Notes |
|-----------|-------|-------|
| `SENSOR_VDO_5BAR_PRESSURE` | 0-5 bar | Oil pressure sensor |
| `SENSOR_VDO_2BAR_PRESSURE` | 0-2 bar | Boost/intake pressure |

**Characteristics:**
- Non-linear voltage output
- Factory calibrated with polynomial equation
- Very accurate (±1% typical)
- Common in European vehicles

**Wiring:**
- Red: +5V
- Black: Ground
- White/Signal: Analog input

### Generic Linear Sensors

| Sensor ID | Range | Voltage | Notes |
|-----------|-------|---------|-------|
| `SENSOR_GENERIC_0_5V_5BAR` | 0-5 bar | 0.5-4.5V | Common automotive |
| `SENSOR_GENERIC_0_5V_10BAR` | 0-10 bar | 0.5-4.5V | High-pressure applications |
| `SENSOR_GENERIC_0_5V_100PSI` | 0-100 psi | 0.5-4.5V | Imperial units |

**Characteristics:**
- Linear voltage-to-pressure relationship
- 0.5V = minimum pressure
- 4.5V = maximum pressure
- Industry standard pinout

**Typical Wiring:**
- Pin 1: Signal output
- Pin 2: +5V supply
- Pin 3: Ground

### Specific Sensors

**Freescale MPX4250AP**
- Range: 20-250 kPa (0.2-2.5 bar)
- Output: 0.2-4.7V
- Common in automotive MAP applications

## Configuration Examples

### Example 1: VDO Oil Pressure Sensor

```cpp
// config.h
#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE    SENSOR_VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT          A3
#define OIL_PRESSURE_MIN            1    // Alarm if below 1 bar
#define OIL_PRESSURE_MAX            5
```

That's it! The VDO polynomial calibration is automatically loaded.

### Example 2: Generic Boost Sensor

```cpp
// config.h
#define ENABLE_BOOST_PRESSURE
#define BOOST_PRESSURE_SENSOR_TYPE  SENSOR_GENERIC_0_5V_5BAR
#define BOOST_PRESSURE_INPUT        A4
#define BOOST_PRESSURE_MIN          -1   // No minimum alarm
#define BOOST_PRESSURE_MAX          2    // Alarm if over 2 bar
```

### Example 3: Custom Pressure Range

If you have a sensor with a different voltage/pressure range:

```cpp
// config.h
#define ENABLE_MY_PRESSURE
#define MY_PRESSURE_SENSOR_TYPE     SENSOR_CUSTOM_PRESSURE_LINEAR
#define MY_PRESSURE_INPUT           A5
#define MY_PRESSURE_MIN             0
#define MY_PRESSURE_MAX             8

// Define custom calibration
#define MY_PRESSURE_CUSTOM_CALIBRATION
#define MY_PRESSURE_VOLTAGE_MIN     0.5   // Voltage at 0 bar
#define MY_PRESSURE_VOLTAGE_MAX     4.5   // Voltage at 8 bar
#define MY_PRESSURE_MIN_BAR         0.0
#define MY_PRESSURE_MAX_BAR         8.0
```

Then in `sensors.cpp`:
```cpp
#ifdef MY_PRESSURE_CUSTOM_CALIBRATION
    static PressureLinearCalibration my_pressure_cal = {
        .voltage_min = MY_PRESSURE_VOLTAGE_MIN,
        .voltage_max = MY_PRESSURE_VOLTAGE_MAX,
        .pressure_min = MY_PRESSURE_MIN_BAR,
        .pressure_max = MY_PRESSURE_MAX_BAR
    };
#endif

Sensor myPressure = {
    // ... standard fields ...
    #ifdef MY_PRESSURE_CUSTOM_CALIBRATION
    .calibrationData = &my_pressure_cal,
    .calibrationType = CAL_PRESSURE_LINEAR
    #else
    .calibrationData = my_pressure_config->calibrationData,
    .calibrationType = my_pressure_config->calibrationType
    #endif
};
```

## Understanding Pressure Units

openEMS stores pressure internally in **bar** and converts for display.

**Conversion factors:**
- 1 bar = 14.5038 psi
- 1 bar = 100 kPa
- 1 bar = 29.53 inHg

**Display units** are set per sensor in `sensors.cpp`:
```cpp
.displayUnits = BAR,    // or PSI, KPA, INHG
```

## Wiring Best Practices

### Power Supply
- Use clean, regulated 5V
- Add 100nF capacitor near sensor (power to ground)
- Keep power wires short and twisted

### Signal Wire
- Use shielded cable if possible
- Keep away from high-current wires (injectors, ignition)
- Add 100nF capacitor at ADC input (signal to ground)

### Grounding
- Use chassis ground for sensor ground
- Ensure good ground connection
- Star grounding preferred (all sensors to one point)

## Troubleshooting

### Sensor reads 0 or maximum value constantly

**Possible causes:**
- Disconnected sensor
- Wiring issue (check continuity)
- Wrong voltage range in calibration

**Check:**
```cpp
// Enable serial output to see raw values
#define ENABLE_SERIAL_OUTPUT
```

### Readings are unstable/noisy

**Solutions:**
1. Add capacitors (100nF) at sensor and ADC input
2. Use shielded cable
3. Check ground connections
4. Increase `LOOP_DELAY_MS` to reduce noise

### Readings are offset (e.g., shows 0.5 bar at 0 psi)

**For linear sensors:**
Check voltage_min and pressure_min match your sensor:
```cpp
.voltage_min = 0.5,    // What voltage at minimum pressure?
.pressure_min = 0.0,   // What is minimum pressure?
```

**For VDO sensors:**
This is normal - VDO sensors have a non-zero voltage at 0 pressure. The polynomial calibration handles this.

### Pressure reads high at low values

**For VDO sensors:**
Make sure you're using the polynomial calibration (`SENSOR_VDO_5BAR_PRESSURE`), not a linear calibration.

**For linear sensors:**
Verify your sensor actually has a linear output. Some cheap sensors are non-linear.

## Custom Polynomial Sensors

If you have a non-VDO sensor with a known polynomial calibration:

**Step 1:** Determine the polynomial equation from datasheet:
```
Voltage = A*Pressure² + B*Pressure + C
```

**Step 2:** Add calibration to `sensor_configs.h`:
```cpp
static const PressurePolynomialCalibration my_custom_poly_cal = {
    .poly_a = -0.123,   // Your A coefficient
    .poly_b = 12.345,   // Your B coefficient
    .poly_c = 0.567     // Your C coefficient
};
```

**Step 3:** Add to sensor database or use in custom sensor definition.

## Advanced: Validating Your Calibration

### Method 1: Known Pressure Source
Use a pressure gauge and pump to test:
1. Apply 0 pressure → should read ~0
2. Apply known pressure → compare reading
3. Apply maximum pressure → verify range

### Method 2: Compare to Working Gauge
If you have an existing working gauge:
1. Monitor both simultaneously
2. Record readings at various pressures
3. Adjust calibration if needed

### Method 3: Datasheet Validation
1. Measure sensor voltage with multimeter
2. Calculate expected pressure from voltage using datasheet
3. Compare to openEMS reading
4. Should match within ±2%

## Sensor Specifications

### Typical Operating Ranges

**Oil Pressure:**
- Idle: 0.5-1 bar (7-15 psi)
- Cruise: 2-3 bar (30-45 psi)
- Max: 5-7 bar (70-100 psi)

**Boost Pressure:**
- Naturally aspirated: ~1 bar (atmospheric)
- Low boost: 0-0.5 bar (0-7 psi)
- High boost: 1-2 bar (15-30 psi)

**Fuel Pressure:**
- Carbureted: 0.1-0.2 bar (1.5-3 psi)
- TBI: 0.5-1 bar (7-15 psi)
- Port injection: 2.5-4 bar (35-60 psi)
- Direct injection: 50-200 bar (700-3000 psi) - special sensors required

## Safety Notes

⚠️ **High Pressure Warning:**
- Sensors above 10 bar require special installation
- Always use appropriate fittings and hoses
- Test for leaks before operating

⚠️ **Electrical Safety:**
- Verify voltage range matches your system (5V vs 3.3V)
- Never apply more than 5.5V to sensor
- Use proper ESD precautions when handling sensors

## Getting Help

If your pressure sensor isn't listed:
1. Check the datasheet for voltage/pressure relationship
2. Determine if it's linear or polynomial
3. Post in GitHub Discussions with sensor details
4. Community can help add it to the library!
