# Sensor Selection Guide

## For Simple Users - Just Pick Your Sensor!

The new calibration system makes sensor configuration incredibly easy. You just need to know what physical sensor you have, then pick the matching ID from the catalog.

## Step-by-Step Example

### Example: Adding a Coolant Temperature Sensor

**What you have:** VDO 120°C coolant temperature sensor

**Steps:**

1. Open `src/config.h`

2. Find the coolant temperature section:
```cpp
// === Coolant Temperature ===
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   ????                  // What goes here?
#define COOLANT_TEMP_INPUT    A2                    // Your analog pin
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100
```

3. Open `src/sensor_library.h` and find VDO sensors:
```cpp
// VDO Thermistors - Lookup Table Method (Most Accurate)
#define VDO_120C_LOOKUP              10
#define VDO_150C_LOOKUP              11

// VDO Thermistors - Steinhart-Hart Method (Faster)
#define VDO_120C_STEINHART           12
#define VDO_150C_STEINHART           13
```

4. Pick one and use it in config.h:
```cpp
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP      // That's it!
```

**Done!** The system automatically:
- Selects the right read function
- Loads the correct calibration data
- Sets up proper conversions

## Sensor Catalog

### Temperature Sensors

| Sensor Type | Sensor ID | Method | Notes |
|-------------|-----------|--------|-------|
| K-Type Thermocouple (MAX6675) | `K_TYPE_THERMOCOUPLE_MAX6675` | Direct | For CHT, EGT |
| K-Type Thermocouple (MAX31855) | `K_TYPE_THERMOCOUPLE_MAX31855` | Direct | For CHT, EGT |
| VDO 120°C Thermistor | `VDO_120C_LOOKUP` | Lookup | Coolant, transfer case |
| VDO 120°C Thermistor | `VDO_120C_STEINHART` | Steinhart | Same sensor, faster |
| VDO 150°C Thermistor | `VDO_150C_LOOKUP` | Lookup | Oil temperature |
| VDO 150°C Thermistor | `VDO_150C_STEINHART` | Steinhart | Same sensor, faster |
| Generic 10K NTC (β=3950) | `GENERIC_NTC_10K_3950` | Steinhart | Common Amazon sensor |
| Generic 10K NTC (β=3435) | `GENERIC_NTC_10K_3435` | Steinhart | Another variant |
| Generic 10K NTC (β=3380) | `GENERIC_NTC_10K_3380` | Steinhart | Another variant |

### Pressure Sensors

| Sensor Type | Sensor ID | Range | Notes |
|-------------|-----------|-------|-------|
| VDO 5-bar | `VDO_5BAR_PRESSURE` | 0-5 bar | Oil pressure |
| VDO 2-bar | `VDO_2BAR_PRESSURE` | 0-2 bar | Boost pressure |
| Freescale MPX4250AP | `MPX4250AP_SENSOR` | 20-250 kPa | Generic MAP |

### Voltage Sensors

| Sensor Type | Sensor ID | Notes |
|-------------|-----------|-------|
| 12V Battery Monitor | `STANDARD_12V_DIVIDER` | Auto-configured for your board |

### Environmental Sensors (BME280)

| Sensor Type | Sensor ID |
|-------------|-----------|
| Temperature | `BME280_TEMPERATURE` |
| Pressure | `BME280_PRESSURE` |
| Humidity | `BME280_HUMIDITY` |
| Altitude | `BME280_ALTITUDE` |

## Lookup vs. Steinhart-Hart

For VDO thermistors, you can choose between two methods:

### Lookup Table Method
- **More accurate** - Uses manufacturer's exact resistance table
- **Slightly slower** - Interpolates between table values
- **Recommended for:** Critical sensors (CHT, EGT)

### Steinhart-Hart Method
- **Faster** - Direct calculation
- **Very good accuracy** - Within 1-2°C of lookup
- **Recommended for:** Non-critical sensors, faster loop times

**Example:**
```cpp
// Most accurate (uses manufacturer's lookup table)
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP

// Faster but still accurate (uses Steinhart-Hart equation)
#define COOLANT_SENSOR_TYPE   VDO_120C_STEINHART
```

You can even mix methods:
```cpp
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP      // Critical - use lookup
#define OIL_TEMP_SENSOR_TYPE  VDO_150C_STEINHART   // Less critical - faster
```

## Complete Example config.h

```cpp
// === CHT (Cylinder Head Temperature) ===
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_MIN               -1
#define CHT_MAX               495

// === EGT (Exhaust Gas Temperature) ===
#define ENABLE_EGT
#define EGT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX31855
#define EGT_INPUT             7
#define EGT_MIN               -1
#define EGT_MAX               600

// === Coolant Temperature ===
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100

// === Oil Temperature ===
#define ENABLE_OIL_TEMP
#define OIL_TEMP_SENSOR_TYPE  VDO_150C_STEINHART
#define OIL_TEMP_INPUT        A0
#define OIL_TEMP_MIN          -1
#define OIL_TEMP_MAX          150

// === Oil Pressure ===
#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE  VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT        A3
#define OIL_PRESSURE_MIN          1
#define OIL_PRESSURE_MAX          5

// === Primary Battery ===
#define ENABLE_PRIMARY_BATTERY
#define PRIMARY_BATTERY_SENSOR_TYPE  STANDARD_12V_DIVIDER
#define PRIMARY_BATTERY_INPUT        A8

// === Ambient Temperature (BME280) ===
#define ENABLE_AMBIENT_TEMP
#define AMBIENT_TEMP_SENSOR_TYPE  BME280_TEMPERATURE
```

That's it! No need to specify read functions, conversion functions, or calibration data - it's all handled automatically.

## What If My Sensor Isn't Listed?

See `docs/ADVANCED_CALIBRATION.md` for information on:
- Adding custom thermistor coefficients
- Creating custom lookup tables
- Defining completely new sensor types

But for 90% of users, the presets will work perfectly!

## Common Questions

**Q: Can I use multiple of the same sensor type?**  
A: Yes! Each sensor is independent. You can have multiple VDO_120C sensors on different pins.

**Q: What's the difference between VDO_120C_LOOKUP and VDO_120C_STEINHART?**  
A: Same physical sensor, different math. Lookup is more accurate, Steinhart is faster.

**Q: Do I need to know what bias resistor I used?**  
A: No! The presets include the correct bias resistor for each sensor.

**Q: What if I used a different bias resistor?**  
A: See the advanced calibration guide to override the preset.

**Q: How do I know which generic NTC thermistor I have?**  
A: Check your sensor datasheet for the β (beta) value. Most cheap NTC thermistors are β=3950.
