# Display Units Configuration Guide

## Overview

openEMS allows you to configure display units globally and override them per sensor. This gives you complete flexibility - for example, you can display most temperatures in Celsius but show CHT in Fahrenheit.

## Quick Start

### Set Global Defaults

In `config.h`, set your preferred units for each measurement type:

```cpp
// ===== GLOBAL DISPLAY UNITS DEFAULTS =====
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS    // or FAHRENHEIT
#define DEFAULT_PRESSURE_UNITS     BAR        // or PSI, KPA, INHG
#define DEFAULT_ELEVATION_UNITS     FEET       // or METERS
```

All sensors will use these defaults unless you override them individually.

### Override Individual Sensors

For any sensor, add a `_DISPLAY_UNITS` definition:

```cpp
// CHT in Fahrenheit (override global default)
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Override here

// EGT in Celsius (uses global default)
#define ENABLE_EGT
#define EGT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX31855
#define EGT_INPUT             7
// No override - uses DEFAULT_TEMPERATURE_UNITS
```

## Available Units

### Temperature Sensors
- `CELSIUS` - Metric (°C)
- `FAHRENHEIT` - Imperial (°F)

**Sensors:**
- CHT, EGT, Coolant, Oil, Transfer Case, Ambient Temperature

### Pressure Sensors
- `BAR` - Metric (bar)
- `PSI` - Imperial (psi)
- `KPA` - Metric (kPa)
- `INHG` - Imperial (inHg) - For barometric pressure

**Sensors:**
- Oil Pressure, Boost Pressure, Barometric Pressure

### Altitude Sensors
- `METERS` - Metric (m)
- `FEET` - Imperial (ft)

**Sensors:**
- Altitude (BME280)

### Fixed Units
- **Voltage** - Always displayed in VOLTS (V)
- **Humidity** - Always displayed in PERCENT (%)

## Configuration Examples

### Example 1: All Metric

```cpp
// Global defaults
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS     METERS

// All sensors will use metric units
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
// Uses CELSIUS (global default)

#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE  VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT        A3
// Uses BAR (global default)
```

### Example 2: All Imperial

```cpp
// Global defaults
#define DEFAULT_TEMPERATURE_UNITS  FAHRENHEIT
#define DEFAULT_PRESSURE_UNITS     PSI
#define DEFAULT_ELEVATION_UNITS     FEET

// All sensors will use imperial units
```

### Example 3: Mixed Units

```cpp
// Global defaults (mostly metric)
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS     FEET

// CHT override to Fahrenheit (pilot preference)
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Override

// Oil pressure override to PSI (mechanic preference)
#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE  VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT        A3
#define OIL_PRESSURE_DISPLAY_UNITS  PSI  // Override

// Barometric pressure in inHg (weather stations use this)
#define ENABLE_BAROMETRIC_PRESSURE
#define BARO_PRESSURE_SENSOR_TYPE  BME280_BAROMETRIC_PRESSURE
#define BARO_DISPLAY_UNITS         INHG  // Override

// All other sensors use global defaults (CELSIUS, BAR, FEET)
```

### Example 4: US Aviation Setup

```cpp
// Aviation typically uses:
// - Temperature: Celsius (international standard)
// - Pressure: inHg (US altimeter setting)
// - Altitude: Feet (US standard)

#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR        // For oil/boost
#define DEFAULT_ELEVATION_UNITS     FEET

// Override barometric pressure for altimeter setting
#define ENABLE_BAROMETRIC_PRESSURE
#define BARO_PRESSURE_SENSOR_TYPE  BME280_BAROMETRIC_PRESSURE
#define BARO_DISPLAY_UNITS         INHG
```

### Example 5: European Car Setup

```cpp
// European standards
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS     METERS

// No overrides needed - everything metric
```

### Example 6: US Car Setup

```cpp
// American standards
#define DEFAULT_TEMPERATURE_UNITS  FAHRENHEIT
#define DEFAULT_PRESSURE_UNITS     PSI
#define DEFAULT_ELEVATION_UNITS     FEET

// No overrides needed - everything imperial
```

## Override Reference

For each sensor type, here's the override define name:

| Sensor | Override Define | Available Units |
|--------|----------------|-----------------|
| CHT | `CHT_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| EGT | `EGT_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| Coolant Temp | `COOLANT_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| Oil Temp | `OIL_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| Transfer Case Temp | `TCASE_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| Ambient Temp | `AMBIENT_DISPLAY_UNITS` | CELSIUS, FAHRENHEIT |
| Oil Pressure | `OIL_PRESSURE_DISPLAY_UNITS` | BAR, PSI, KPA |
| Boost Pressure | `BOOST_DISPLAY_UNITS` | BAR, PSI, KPA |
| Barometric Pressure | `BARO_DISPLAY_UNITS` | BAR, PSI, KPA, INHG |
| Altitude | `ELEVATION_DISPLAY_UNITS` | METERS, FEET |
| Voltage | (none) | Always VOLTS |
| Humidity | (none) | Always PERCENT |

## Unit Conversion Reference

### Temperature
- 0°C = 32°F
- 100°C = 212°F
- Formula: °F = (°C × 9/5) + 32

### Pressure
- 1 bar = 14.5038 psi
- 1 bar = 100 kPa
- 1 bar = 29.53 inHg
- Standard atmosphere: 1.01325 bar = 14.696 psi = 29.92 inHg

### Altitude
- 1 meter = 3.28084 feet
- 1000 feet ≈ 305 meters

## FAQ

**Q: Can I change units without recompiling?**  
A: Not yet, but future versions may support runtime unit switching via serial commands or button presses.

**Q: Why are some units fixed (voltage, humidity)?**  
A: These measurements have universally accepted units:
- Voltage: Always in volts (V)
- Humidity: Always as percentage (%)

**Q: Can I add custom units (like bar absolute vs gauge)?**  
A: This would require code modifications. Consider using separate sensors with calibration offsets instead.

**Q: Will OBDII output match my display units?**  
A: No. OBDII output always uses standard OBDII formats regardless of display units. This ensures compatibility with diagnostic tools.

**Q: What if I forget to set global defaults?**  
A: The code will fail to compile with a clear error. You must define all three defaults:
- `DEFAULT_TEMPERATURE_UNITS`
- `DEFAULT_PRESSURE_UNITS`
- `DEFAULT_ELEVATION_UNITS`

**Q: Can I override a sensor to use metric when global is imperial?**  
A: Yes! Overrides work in both directions. Example:
```cpp
#define DEFAULT_TEMPERATURE_UNITS  FAHRENHEIT  // Global

#define ENABLE_COOLANT_TEMP
#define COOLANT_DISPLAY_UNITS      CELSIUS     // This sensor only
```

## Troubleshooting

### Compilation Error: "DEFAULT_TEMPERATURE_UNITS not defined"

Make sure you have all three global defaults in config.h:
```cpp
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS     FEET
```

### Sensor shows wrong units on LCD

Check your override:
```cpp
// Make sure override uses correct enum value
#define CHT_DISPLAY_UNITS  FAHRENHEIT  // Correct
#define CHT_DISPLAY_UNITS  fahrenheit  // Wrong - case sensitive!
```

### All sensors show same units even with overrides

Verify the override is **above** the sensor definition in config.h:
```cpp
// Correct order:
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Before other sensors

// Not after:
#define ENABLE_EGT
#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Too late!
```

## Best Practices

1. **Set global defaults first** - Choose units most of your sensors will use
2. **Override exceptions** - Only override sensors that need different units
3. **Document your choices** - Add comments explaining why you chose specific units
4. **Test on LCD** - Verify units display correctly before installing
5. **Consider your audience** - Who will read the display? Pilots? Mechanics? Engineers?

## Example Complete config.h

```cpp
// ===== GLOBAL DISPLAY UNITS =====
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS     FEET

// ===== CHT - Critical sensor, pilot wants Fahrenheit =====
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6
#define CHT_MIN               -1
#define CHT_MAX               495
#define CHT_DISPLAY_UNITS     FAHRENHEIT  // Override for pilot

// ===== EGT - Uses global default (Celsius) =====
#define ENABLE_EGT
#define EGT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX31855
#define EGT_INPUT             7
#define EGT_MIN               -1
#define EGT_MAX               600
// No override - uses CELSIUS

// ===== Oil Pressure - Mechanic wants PSI =====
#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE    VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT          A3
#define OIL_PRESSURE_MIN            1
#define OIL_PRESSURE_MAX            5
#define OIL_PRESSURE_DISPLAY_UNITS  PSI  // Override for mechanic

// ===== Barometric - Weather stations use inHg =====
#define ENABLE_BAROMETRIC_PRESSURE
#define BARO_PRESSURE_SENSOR_TYPE   BME280_BAROMETRIC_PRESSURE
#define BARO_DISPLAY_UNITS          INHG  // Override for weather

// All other sensors use global defaults
```

This gives you complete flexibility while keeping configuration simple!
