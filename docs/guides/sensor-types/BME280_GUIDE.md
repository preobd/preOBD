# BME280 Environmental Sensor Guide

**Setup guide for BME280 temperature, pressure, humidity, and altitude sensing**

---

## Overview

The BME280 is a combined environmental sensor that measures:
- **Temperature** (-40 to +85°C)
- **Barometric Pressure** (300-1100 hPa)
- **Relative Humidity** (0-100%)
- **Altitude** (calculated from pressure)

It's useful for ambient conditions monitoring, altitude estimation, and weather data logging.

---

## Quick Start

### Ambient Temperature
```
SET I2C AMBIENT_TEMP BME280_TEMP
SAVE
```

### Barometric Pressure
```
SET I2C BAROMETRIC_PRESSURE BME280_PRESSURE
SAVE
```

### All Four Measurements
```
SET I2C AMBIENT_TEMP BME280_TEMP
SET I2C BAROMETRIC_PRESSURE BME280_PRESSURE
SET I2C HUMIDITY BME280_HUMIDITY
SET I2C ELEVATION BME280_ELEVATION
SAVE
```

---

## Available Sensor Types

| Sensor ID | Measurement | Units |
|-----------|-------------|-------|
| `BME280_TEMP` | Ambient temperature | °C / °F |
| `BME280_PRESSURE` | Barometric pressure | hPa / mbar |
| `BME280_HUMIDITY` | Relative humidity | % |
| `BME280_ELEVATION` | Estimated altitude | m / ft |

---

## Wiring

### I2C Connection

The BME280 uses the I2C bus, which is shared with other I2C devices (like the LCD display).

```
BME280 Module:
  VCC → 3.3V (NOT 5V for most modules!)
  GND → GND
  SDA → SDA pin (board-specific)
  SCL → SCL pin (board-specific)
```

### Platform-Specific I2C Pins

| Board | SDA | SCL |
|-------|-----|-----|
| Arduino Uno | A4 | A5 |
| Arduino Mega | 20 | 21 |
| Teensy 4.0/4.1 | 18 | 19 |
| Arduino Due | 20 | 21 |
| ESP32 | 21 | 22 |

### I2C Address

The BME280 can have two I2C addresses:
- **0x76** - SDO pin connected to GND (most common)
- **0x77** - SDO pin connected to VCC

openEMS auto-detects the address at startup.

---

## Configuration Examples

### Example 1: Ambient Temperature Only

```
SET I2C AMBIENT_TEMP BME280_TEMP
SAVE
```

### Example 2: Weather Station

```
SET I2C AMBIENT_TEMP BME280_TEMP
SET I2C BAROMETRIC_PRESSURE BME280_PRESSURE
SET I2C HUMIDITY BME280_HUMIDITY
SAVE
```

### Example 3: Altitude Monitoring

```
SET I2C ELEVATION BME280_ELEVATION
SAVE
```

**Note:** Altitude is calculated from barometric pressure using the standard atmosphere model. For accurate absolute altitude, you may need to calibrate the sea-level pressure reference.

---

## Altitude Calculation

### How It Works

Altitude is estimated using the barometric formula:

```
Altitude = 44330 × (1 - (P/P₀)^0.1903)

Where:
- P = measured pressure (hPa)
- P₀ = sea level reference pressure (default 1013.25 hPa)
```

### Sea Level Pressure Calibration

For accurate altitude readings, set the current sea level pressure:

```
SYSTEM SEA_LEVEL 1015.2
SAVE
```

You can find current sea level pressure from local weather reports or aviation METAR data.

---

## Troubleshooting

### Sensor Not Detected

**Symptoms:**
- Readings show NaN or error
- `INFO I2C` shows no sensor

**Check:**
1. Wiring connections (especially SDA/SCL)
2. Power (3.3V, not 5V for most modules)
3. I2C address (try both 0x76 and 0x77)
4. I2C bus conflicts with other devices

**Debug:**
```
# Check if sensor is responding
LIST INPUTS
INFO I2C
```

### Wrong Temperature Reading

**Possible causes:**
1. Sensor is near heat source (engine, exhaust)
2. Self-heating from frequent reads
3. Sensor damaged

**Solutions:**
1. Mount sensor away from heat sources
2. Use adequate ventilation
3. Shield sensor from radiant heat

### Altitude is Inaccurate

**Possible causes:**
1. Sea level pressure not calibrated
2. Weather conditions changing
3. Sensor needs warm-up time

**Solutions:**
1. Set current sea level pressure: `SYSTEM SEA_LEVEL <hPa>`
2. Recalibrate periodically during changing weather
3. Allow 5-10 minutes for sensor warm-up

### Humidity Reading Stuck at 100%

**Possible causes:**
1. Condensation on sensor
2. Sensor damaged by water exposure

**Solutions:**
1. Allow sensor to dry completely
2. Use conformal coating for humid environments
3. Replace sensor if damaged

---

## Technical Details

### Sensor Specifications

| Parameter | Range | Accuracy |
|-----------|-------|----------|
| Temperature | -40 to +85°C | ±1°C |
| Pressure | 300-1100 hPa | ±1 hPa |
| Humidity | 0-100% RH | ±3% RH |
| Altitude | -500 to +9000m | ±1m (relative) |

### Read Timing

- Measurement time: ~10ms per parameter
- Default read interval: 500ms
- Minimum read interval: 100ms

### Power Consumption

- Normal mode: 3.6 µA @ 1 Hz
- Sleep mode: 0.1 µA
- Peak current: 714 µA during measurement

---

## Module Recommendations

### Budget Option
- Generic GY-BME280 module (~$3-5)
- Works well for basic applications
- Some modules are BMP280 (no humidity) - verify before buying

### Quality Option
- Adafruit BME280 breakout (#2652)
- Proper voltage regulation and level shifting
- Works with both 3.3V and 5V systems

### Waterproof Option
- BME280 in weatherproof enclosure
- Required for outdoor/under-hood mounting
- Ensure adequate ventilation for accurate readings

---

## Mounting Considerations

### Best Practices

1. **Mount away from heat sources** - Engine heat will affect readings
2. **Ensure airflow** - Stagnant air gives inaccurate readings
3. **Protect from water** - Use weatherproof enclosure for engine bay
4. **Avoid vibration** - Use rubber mounting to reduce noise

### Suggested Locations

| Application | Location |
|-------------|----------|
| Ambient temp | Air intake, front bumper area |
| Cabin temp | Dashboard, hidden location |
| Under-hood | Use for reference only (heat affected) |
| Altitude | Any location (pressure is consistent) |

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [PIN_REQUIREMENTS_GUIDE.md](../hardware/PIN_REQUIREMENTS_GUIDE.md) - Pin type requirements

