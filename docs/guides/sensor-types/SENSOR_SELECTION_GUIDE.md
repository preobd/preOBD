# Sensor Selection Guide

**How to choose and configure sensors for openEMS**

---

## For Basic Setup - Just Pick Your Sensor!

The sensor library makes configuration easy. You just need to know what physical sensor you have, then pick the matching ID from the catalog.

---

## Quick Start Examples

### Example 1: Compile-Time Mode (config.h)

```cpp
// Enable compile-time mode
#define USE_STATIC_CONFIG

// Input 0: CHT with K-type thermocouple
#define INPUT_0_PIN            6
#define INPUT_0_APPLICATION    CHT
#define INPUT_0_SENSOR         MAX6675

// Input 1: Coolant with VDO sensor
#define INPUT_1_PIN            A2
#define INPUT_1_APPLICATION    COOLANT_TEMP
#define INPUT_1_SENSOR         VDO_120C_LOOKUP

// Input 2: Oil pressure with VDO sensor
#define INPUT_2_PIN            A3
#define INPUT_2_APPLICATION    OIL_PRESSURE
#define INPUT_2_SENSOR         VDO_5BAR
```

### Example 2: Runtime Mode (Serial Commands)

```
SET 6 APPLICATION CHT
SET 6 SENSOR MAX6675
ENABLE 6

SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2

SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
ENABLE A3

SAVE
```

**That's it!** The system automatically handles calibration, conversion functions, and display formatting.

---

## Sensor Catalog

### Temperature Sensors - Thermocouples

| Sensor ID | Description | Range | Interface |
|-----------|-------------|-------|-----------|
| `MAX6675` | K-Type thermocouple (MAX6675 amplifier) | 0-1024°C | SPI |
| `MAX31855` | K-Type thermocouple (MAX31855 amplifier) | -200-1350°C | SPI |

**Best for:** CHT (Cylinder Head Temperature), EGT (Exhaust Gas Temperature)

**Wiring:**
```
MAX6675 VCC → 5V (or 3.3V)
MAX6675 GND → GND
MAX6675 SCK → Pin 13 (SPI clock)
MAX6675 SO  → Pin 12 (SPI MISO)
MAX6675 CS  → Your configured pin (e.g., Pin 6)
```

### Temperature Sensors - VDO Thermistors

| Sensor ID | Description | Range | Method |
|-----------|-------------|-------|--------|
| `VDO_120C_LOOKUP` | VDO 120°C sender (lookup table) | -40 to 120°C | Most accurate |
| `VDO_120C_STEINHART` | VDO 120°C sender (Steinhart-Hart) | -40 to 120°C | Faster |
| `VDO_150C_LOOKUP` | VDO 150°C sender (lookup table) | -40 to 150°C | Most accurate |
| `VDO_150C_STEINHART` | VDO 150°C sender (Steinhart-Hart) | -40 to 150°C | Faster |

**Best for:** Coolant temperature, oil temperature, transfer case temperature

**Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body)
Add 1kΩ resistor (see guide for options): Analog pin → resistor → GND
```

### Pressure Sensors

| Sensor ID | Description | Range |
|-----------|-------------|-------|
| `VDO_2BAR` | VDO 0-2 bar pressure sender | 0-29 PSI |
| `VDO_5BAR` | VDO 0-5 bar pressure sender | 0-73 PSI |
| `GENERIC_BOOST` | Generic 0.5-4.5V boost sensor | Configurable |
| `MPX4250AP` | Freescale MAP sensor | 20-250 kPa |

**Best for:** Oil pressure, boost pressure, fuel pressure

**VDO Pressure Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body)
Add 1kΩ resistor (see guide for options): Analog pin → resistor → GND
```

### Voltage Sensors

| Sensor ID | Description | Notes |
|-----------|-------------|-------|
| `VOLTAGE_DIVIDER` | Standard 12V battery monitoring | Auto-configured per platform |

**Wiring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → Lower resistor → GND
  - 22kΩ for 3.3V boards (Teensy)
  - 6.8kΩ for 5V boards (Arduino)
```

**Note:** Platform auto-detects board voltage and configures the correct divider ratio.

### RPM Sensors

| Sensor ID | Description | Notes |
|-----------|-------------|-------|
| `W_PHASE_RPM` | W-phase alternator RPM | Requires voltage protection circuit |

**⚠️ CRITICAL:** See [W_PHASE_RPM_GUIDE.md](W_PHASE_RPM_GUIDE.md) for required voltage protection. 3.3V boards require zener protection!

### Environmental Sensors (BME280)

| Sensor ID | Description |
|-----------|-------------|
| `BME280_TEMP` | Ambient temperature |
| `BME280_PRESSURE` | Barometric pressure |
| `BME280_HUMIDITY` | Relative humidity |
| `BME280_ELEVATION` | Altitude estimation |

**Wiring:**
```
BME280 VCC → 3.3V
BME280 GND → GND
BME280 SDA → SDA pin
BME280 SCL → SCL pin
```

### Digital Sensors

| Sensor ID | Description |
|-----------|-------------|
| `FLOAT_SWITCH` | Digital float switch (coolant level, etc.) |

---

## Lookup vs. Steinhart-Hart

For VDO thermistors, you can choose between two calibration methods:

### Lookup Table Method (`_LOOKUP`)
- **More accurate** - Uses manufacturer's exact resistance/temperature table
- **Slightly slower** - Interpolates between table values
- **Recommended for:** Critical sensors (coolant, oil temp)

### Steinhart-Hart Method (`_STEINHART`)
- **Faster** - Direct mathematical calculation
- **Very good accuracy** - Within 1-2°C of lookup
- **Recommended for:** Non-critical sensors, faster loop times

**Example - mixing methods:**

```cpp
// Compile-time config
#define INPUT_0_SENSOR         VDO_120C_LOOKUP      // Critical - maximum accuracy
#define INPUT_1_SENSOR         VDO_150C_STEINHART   // Less critical - faster
```

---

## Application Types

Each input needs both an **Application** (what you're measuring) and a **Sensor** (physical hardware).

| Application | Description | Typical Sensors |
|-------------|-------------|-----------------|
| `CHT` | Cylinder Head Temperature | MAX6675, MAX31855 |
| `EGT` | Exhaust Gas Temperature | MAX31855 |
| `COOLANT_TEMP` | Engine Coolant Temperature | VDO_120C_LOOKUP |
| `OIL_TEMP` | Engine Oil Temperature | VDO_150C_LOOKUP |
| `TCASE_TEMP` | Transfer Case Temperature | VDO_150C_LOOKUP |
| `OIL_PRESSURE` | Engine Oil Pressure | VDO_5BAR |
| `BOOST_PRESSURE` | Turbo/Supercharger Boost | VDO_2BAR, GENERIC_BOOST |
| `FUEL_PRESSURE` | Fuel Rail Pressure | VDO_5BAR |
| `PRIMARY_BATTERY` | Main Battery Voltage | VOLTAGE_DIVIDER |
| `AUXILIARY_BATTERY` | Secondary Battery Voltage | VOLTAGE_DIVIDER |
| `COOLANT_LEVEL` | Coolant Level Switch | FLOAT_SWITCH |
| `AMBIENT_TEMP` | Ambient Temperature | BME280_TEMP |
| `BAROMETRIC_PRESSURE` | Barometric Pressure | BME280_PRESSURE |
| `HUMIDITY` | Relative Humidity | BME280_HUMIDITY |
| `ELEVATION` | Altitude | BME280_ELEVATION |
| `ENGINE_RPM` | Engine RPM | W_PHASE_RPM |

---

## Complete Configuration Example

### Compile-Time Mode (config.h)

```cpp
// Build mode
#define USE_STATIC_CONFIG

// Output enables
#define ENABLE_LCD
#define ENABLE_SERIAL_OUTPUT

// Input 0: CHT with K-type thermocouple
#define INPUT_0_PIN            6
#define INPUT_0_APPLICATION    CHT
#define INPUT_0_SENSOR         MAX6675

// Input 1: Coolant temperature
#define INPUT_1_PIN            A2
#define INPUT_1_APPLICATION    COOLANT_TEMP
#define INPUT_1_SENSOR         VDO_120C_LOOKUP

// Input 2: Oil temperature
#define INPUT_2_PIN            A0
#define INPUT_2_APPLICATION    OIL_TEMP
#define INPUT_2_SENSOR         VDO_150C_STEINHART

// Input 3: Oil pressure
#define INPUT_3_PIN            A3
#define INPUT_3_APPLICATION    OIL_PRESSURE
#define INPUT_3_SENSOR         VDO_5BAR

// Input 4: Battery voltage
#define INPUT_4_PIN            A8
#define INPUT_4_APPLICATION    PRIMARY_BATTERY
#define INPUT_4_SENSOR         VOLTAGE_DIVIDER

// Input 5: Engine RPM
#define INPUT_5_PIN            5
#define INPUT_5_APPLICATION    ENGINE_RPM
#define INPUT_5_SENSOR         W_PHASE_RPM
```

### Runtime Mode (Serial Commands)

```
SET 6 APPLICATION CHT
SET 6 SENSOR MAX6675
ENABLE 6

SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2

SET A0 APPLICATION OIL_TEMP
SET A0 SENSOR VDO_150C_STEINHART
ENABLE A0

SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
ENABLE A3

SET A8 APPLICATION PRIMARY_BATTERY
SET A8 SENSOR VOLTAGE_DIVIDER
ENABLE A8

SET 5 APPLICATION ENGINE_RPM
SET 5 SENSOR W_PHASE_RPM
ENABLE 5

SAVE
```

---

## Common Questions

**Q: Can I use multiple of the same sensor type?**
A: Yes! Each input is independent. You can have multiple VDO_120C sensors on different pins.

**Q: What's the difference between LOOKUP and STEINHART?**
A: Same physical sensor, different math. Lookup is more accurate (±0.5°C), Steinhart is faster (±1°C).

**Q: Do I need to specify the bias resistor value?**
A: No! The presets use the default 1kΩ bias resistor (defined by VDO_BIAS_RESISTOR in config.h).

**Q: What if I used a different bias resistor?**
A: See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) to override.

**Q: How do I know which generic NTC thermistor I have?**
A: Check your sensor datasheet for the β (beta) value. Most cheap NTC thermistors are β=3950.

**Q: My sensor isn't listed - what do I do?**
A: See [ADDING_SENSORS.md](../configuration/ADDING_SENSORS.md) for adding custom sensors.

---

## Related Guides

- [Pressure Sensor Guide](PRESSURE_SENSOR_GUIDE.md) - Detailed pressure sensor info
- [Voltage Sensor Guide](VOLTAGE_SENSOR_GUIDE.md) - Battery monitoring details
- [W-Phase RPM Guide](W_PHASE_RPM_GUIDE.md) - RPM sensing for classics
- [Digital Sensor Guide](DIGITAL_SENSOR_GUIDE.md) - Float switches and digital inputs
- [Advanced Calibration](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom sensor setup

---

**For the classic car community.**
