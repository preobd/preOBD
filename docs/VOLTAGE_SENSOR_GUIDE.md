# Voltage Sensor Configuration Guide

## Overview

openEMS supports two types of voltage monitoring:
1. **Voltage dividers** - For voltages above ADC range (battery monitoring)
2. **Direct voltage** - For voltages within ADC range (5V rail monitoring)

The system automatically selects the correct calibration based on your microcontroller platform.

## Quick Selection Guide

### Standard Applications

**Monitoring 12V battery on Arduino Mega (5V system)?**
```cpp
#define PRIMARY_BATTERY_SENSOR_TYPE  SENSOR_STANDARD_12V_DIVIDER
```

**Monitoring 12V battery on Teensy 4.0 (3.3V system)?**
```cpp
#define PRIMARY_BATTERY_SENSOR_TYPE  SENSOR_STANDARD_12V_DIVIDER
```
*Note: Same sensor type - calibration is auto-selected based on platform!*

**Monitoring 24V truck battery?**
```cpp
#define PRIMARY_BATTERY_SENSOR_TYPE  SENSOR_STANDARD_24V_DIVIDER
```

**Monitoring a 5V power rail directly?**
```cpp
#define VOLTAGE_SENSOR_TYPE  SENSOR_DIRECT_VOLTAGE_5V
```

## Available Voltage Sensors

### Standard Battery Monitoring

| Sensor ID | Application | Divider | Notes |
|-----------|-------------|---------|-------|
| `SENSOR_STANDARD_12V_DIVIDER` | 12V battery | Auto-configured | Uses platform.h defaults |
| `SENSOR_STANDARD_24V_DIVIDER` | 24V truck battery | 100kΩ/3.3kΩ | For large vehicles |

**Platform Auto-Configuration:**
- **5V systems (Mega):** 100kΩ/6.8kΩ divider
- **3.3V systems (Teensy):** 100kΩ/22kΩ divider

### Custom Voltage Dividers

| Sensor ID | Divider Ratio | Max Voltage | Use Case |
|-----------|---------------|-------------|----------|
| `SENSOR_CUSTOM_VOLTAGE_100K_10K` | 11:1 | ~50V | High voltage monitoring |
| `SENSOR_CUSTOM_VOLTAGE_100K_22K` | 5.5:1 | ~18V | 12V with more resolution |
| `SENSOR_CUSTOM_VOLTAGE_47K_10K` | 5.7:1 | ~19V | Alternative 12V divider |

### Direct Voltage Reading

| Sensor ID | Range | Use Case |
|-----------|-------|----------|
| `SENSOR_DIRECT_VOLTAGE_5V` | 0-5V | Power rail monitoring |
| `SENSOR_DIRECT_VOLTAGE_3V3` | 0-3.3V | Low voltage signals |

## Configuration Examples

### Example 1: Standard 12V Battery Monitoring

```cpp
// config.h
#define ENABLE_PRIMARY_BATTERY
#define PRIMARY_BATTERY_SENSOR_TYPE   SENSOR_STANDARD_12V_DIVIDER
#define PRIMARY_BATTERY_INPUT         A8
```

**Hardware:**
- 100kΩ resistor from battery + to A8
- 6.8kΩ resistor from A8 to ground (5V systems)
- OR 22kΩ resistor from A8 to ground (3.3V systems)
- 100nF capacitor from A8 to ground (noise filtering)

**That's it!** The system automatically:
- Selects correct divider ratio for your board
- Uses appropriate ADC reference voltage
- Scales reading correctly

### Example 2: Dual Battery System

```cpp
// config.h
#define ENABLE_PRIMARY_BATTERY
#define PRIMARY_BATTERY_SENSOR_TYPE   SENSOR_STANDARD_12V_DIVIDER
#define PRIMARY_BATTERY_INPUT         A8

#define ENABLE_AUXILIARY_BATTERY
#define SECONDARY_BATTERY_SENSOR_TYPE SENSOR_STANDARD_12V_DIVIDER
#define SECONDARY_BATTERY_INPUT       A7
```

Monitor house and starter batteries separately!

### Example 3: 24V Truck System

```cpp
// config.h
#define ENABLE_PRIMARY_BATTERY
#define PRIMARY_BATTERY_SENSOR_TYPE   SENSOR_STANDARD_24V_DIVIDER
#define PRIMARY_BATTERY_INPUT         A8
```

**Hardware:**
- 100kΩ resistor from battery + to A8
- 3.3kΩ resistor from A8 to ground
- 100nF capacitor from A8 to ground

### Example 4: Custom Voltage Divider

If you need a different divider ratio:

```cpp
// config.h
#define ENABLE_CUSTOM_VOLTAGE
#define CUSTOM_VOLTAGE_SENSOR_TYPE    SENSOR_CUSTOM_VOLTAGE_DIVIDER
#define CUSTOM_VOLTAGE_INPUT          A9

// Define custom calibration
#define CUSTOM_VOLTAGE_CUSTOM_CALIBRATION
#define CUSTOM_VOLTAGE_R1             150000.0  // 150kΩ high side
#define CUSTOM_VOLTAGE_R2             10000.0   // 10kΩ low side
#define CUSTOM_VOLTAGE_CORRECTION     1.0
#define CUSTOM_VOLTAGE_OFFSET         0.0
```

Then in `sensors.cpp`:
```cpp
#ifdef CUSTOM_VOLTAGE_CUSTOM_CALIBRATION
    static VoltageDividerCalibration custom_voltage_cal = {
        .r1 = CUSTOM_VOLTAGE_R1,
        .r2 = CUSTOM_VOLTAGE_R2,
        .correction = CUSTOM_VOLTAGE_CORRECTION,
        .offset = CUSTOM_VOLTAGE_OFFSET
    };
#endif

Sensor customVoltage = {
    // ... standard fields ...
    #ifdef CUSTOM_VOLTAGE_CUSTOM_CALIBRATION
    .calibrationData = &custom_voltage_cal,
    .calibrationType = CAL_VOLTAGE_DIVIDER
    #else
    .calibrationData = custom_voltage_config->calibrationData,
    .calibrationType = custom_voltage_config->calibrationType
    #endif
};
```

### Example 5: Direct 5V Rail Monitoring

```cpp
// config.h
#define ENABLE_5V_RAIL
#define RAIL_5V_SENSOR_TYPE    SENSOR_DIRECT_VOLTAGE_5V
#define RAIL_5V_INPUT          A9
```

**Hardware:**
- Connect 5V rail directly to A9
- Add 1kΩ series resistor for protection (optional)
- Add 100nF capacitor to ground

## Understanding Voltage Dividers

### Basic Principle

A voltage divider reduces voltage to safe levels for the ADC:

```
Battery + ----[R1]----+----[R2]---- GND
                      |
                   ADC Pin
```

**Voltage at ADC:**
```
V_ADC = V_Battery × (R2 / (R1 + R2))
```

**Measured voltage:**
```
V_Battery = V_ADC × ((R1 + R2) / R2)
```

### Choosing Resistor Values

**For 12V battery on 5V system:**
- Want V_ADC < 1.1V for internal reference
- Using 100kΩ / 6.8kΩ: 12V → 0.76V ✓

**For 12V battery on 3.3V system:**
- Want V_ADC < 3.0V
- Using 100kΩ / 22kΩ: 12V → 2.16V ✓

**General rules:**
1. Use high resistance (100kΩ+) to minimize current draw
2. Keep ratio such that max voltage → ~70% of ADC range
3. Add capacitor for noise filtering

### Standard Divider Ratios

| Application | R1 | R2 | Max Voltage | System |
|-------------|-----|-----|-------------|--------|
| 12V battery | 100kΩ | 6.8kΩ | ~15V | 5V ADC |
| 12V battery | 100kΩ | 22kΩ | ~15V | 3.3V ADC |
| 24V battery | 100kΩ | 3.3kΩ | ~30V | 5V ADC |
| High voltage | 1MΩ | 10kΩ | ~100V | 5V ADC |

## Hardware Setup

### Recommended Circuit

```
Battery + ----[100kΩ]----+----[R2]---- GND
                         |
                      [100nF]
                         |
                      ADC Pin
```

**Components:**
- R1: 100kΩ ±1% (high side)
- R2: Selected based on application (see table above)
- C1: 100nF ceramic capacitor (noise filtering)

### Breadboard Testing

1. Build voltage divider on breadboard
2. Measure with multimeter at ADC pin
3. Verify voltage is < ADC reference
4. Connect to Arduino and test

### PCB Design Tips

1. **Place resistors close to ADC pin**
2. **Use 1% tolerance resistors for accuracy**
3. **Add TVS diode for transient protection**
4. **Keep traces short**
5. **Ground plane under ADC traces**

### Protection

**Optional protection circuit:**
```
Battery + ----[100kΩ]----+----[R2]---- GND
                         |
                      [Zener]  (5.1V for 5V system)
                         |
                      [100nF]
                         |
                      ADC Pin
```

Zener diode clamps voltage to safe level if battery voltage exceeds expected range.

## Calibration

### Method 1: Resistance Measurement

Most accurate - measure actual resistor values:

1. Measure R1 with DMM: 98.5kΩ (example)
2. Measure R2 with DMM: 6.75kΩ (example)
3. Update config:
```cpp
#define CUSTOM_VOLTAGE_R1  98500.0
#define CUSTOM_VOLTAGE_R2  6750.0
```

### Method 2: Known Voltage Calibration

Compare to accurate voltmeter:

1. Measure battery with accurate meter: 12.65V
2. Read openEMS display: 12.80V (0.15V high)
3. Calculate correction factor: 12.65 / 12.80 = 0.988
4. Update config:
```cpp
#define CUSTOM_VOLTAGE_CORRECTION  0.988
```

### Method 3: Two-Point Calibration

For best accuracy across range:

1. Measure at low voltage (11V) and high voltage (14V)
2. Calculate linear regression
3. Determine correction and offset
4. Update calibration

### Validating Calibration

1. Compare to known accurate meter
2. Test at multiple voltages (11V, 12V, 13V, 14V)
3. Should be within ±0.1V across range
4. If not, check resistor values and connections

## Advanced Features

### Correction Factor

Used to compensate for:
- ADC non-linearity
- Resistor tolerance
- Temperature drift

```cpp
.correction = 1.05  // 5% correction
```

### Voltage Offset

Used to compensate for:
- ADC offset error
- Systematic measurement error

```cpp
.offset = 0.2  // Add 0.2V to all readings
```

### Example: Full Custom Calibration

After testing with accurate meter:
```cpp
static VoltageDividerCalibration my_custom_cal = {
    .r1 = 98500.0,      // Measured R1
    .r2 = 6750.0,       // Measured R2
    .correction = 0.988, // Calibration factor
    .offset = 0.05      // Small offset correction
};
```

## Troubleshooting

### Voltage Reads High

**Possible causes:**
1. R1 measured incorrectly (actual value lower)
2. R2 measured incorrectly (actual value higher)
3. Bad ADC reference

**Solutions:**
- Re-measure resistors with accurate DMM
- Check AREF_VOLTAGE in platform.h
- Use correction factor

### Voltage Reads Low

**Possible causes:**
1. Opposite of above
2. Poor connection
3. Voltage drop in wiring

**Solutions:**
- Check all connections
- Measure voltage at ADC pin
- Use thicker wire from battery

### Erratic Readings

**Possible causes:**
1. Missing capacitor
2. Poor ground
3. Electrical noise

**Solutions:**
- Add 100nF capacitor at ADC pin
- Use shielded cable
- Keep wiring away from ignition/injectors
- Increase LOOP_DELAY_MS

### Readings Drift with Temperature

**Possible causes:**
1. Resistor temperature coefficient
2. ADC temperature drift

**Solutions:**
- Use 1% metal film resistors (low tempco)
- Keep resistors away from heat sources
- Recalibrate at operating temperature

## Safety Considerations

⚠️ **Electrical Safety:**
- Always disconnect battery before wiring
- Double-check polarity before connecting
- Use fused connection (1A fuse recommended)
- Ensure proper insulation of all connections

⚠️ **Reverse Polarity Protection:**
Consider adding a diode:
```
Battery + ----[Diode]----[100kΩ]----+----[R2]---- GND
                                     |
                                  ADC Pin
```
Diode (1N4148) protects against reverse connection.

⚠️ **Overvoltage Protection:**
- Add Zener diode for transient protection
- Use TVS diode on automotive applications
- Consider MOV for surge protection

## Battery Voltage Interpretation

### 12V Lead-Acid Battery States

| Voltage | State | Notes |
|---------|-------|-------|
| 12.6V+ | Fully charged | 100% SOC |
| 12.4V | 75% charged | Good condition |
| 12.2V | 50% charged | Recharge soon |
| 12.0V | 25% charged | Recharge now |
| 11.8V | Nearly dead | Critical |
| <11.5V | Damaged | May not recover |

**While running (alternator charging):**
- Normal: 13.5-14.5V
- Low: <13.5V (weak alternator)
- High: >14.8V (overcharging - check regulator)

### 24V System (Truck)

Multiply above values by 2:
- Fully charged: ~25.2V
- Running: 27-29V

## Common Applications

### Monitoring Voltage Drop

Use two voltage sensors to monitor voltage drop:
```cpp
#define ENABLE_BATTERY_POSITIVE
#define BATTERY_POS_SENSOR_TYPE  SENSOR_STANDARD_12V_DIVIDER
#define BATTERY_POS_INPUT        A8

#define ENABLE_VOLTAGE_AT_LOAD
#define LOAD_VOLTAGE_SENSOR_TYPE SENSOR_STANDARD_12V_DIVIDER
#define LOAD_VOLTAGE_INPUT       A7
```

Calculate drop in main loop:
```cpp
float voltage_drop = batteryPos.value - loadVoltage.value;
```

### Dual Battery System

Monitor house and starter batteries:
```cpp
#define ENABLE_STARTER_BATTERY
#define STARTER_BATTERY_SENSOR_TYPE  SENSOR_STANDARD_12V_DIVIDER
#define STARTER_BATTERY_INPUT        A8

#define ENABLE_HOUSE_BATTERY
#define HOUSE_BATTERY_SENSOR_TYPE    SENSOR_STANDARD_12V_DIVIDER
#define HOUSE_BATTERY_INPUT          A7
```

### Solar System Monitoring

Monitor solar panel and battery:
```cpp
#define ENABLE_SOLAR_PANEL
#define SOLAR_PANEL_SENSOR_TYPE      SENSOR_STANDARD_12V_DIVIDER
#define SOLAR_PANEL_INPUT            A9

#define ENABLE_BATTERY_VOLTAGE
#define BATTERY_VOLTAGE_SENSOR_TYPE  SENSOR_STANDARD_12V_DIVIDER
#define BATTERY_VOLTAGE_INPUT        A8
```

## Getting Help

Need a custom voltage divider?
1. Calculate required ratio
2. Post in GitHub Discussions
3. Community can help with resistor values
4. Contribute calibration back to library!
