# Sensor Selection Guide

**How to choose and configure sensors for openEMS**

---

## Quick Start - Two-Layer Selection

The sensor library is organized into **categories** (sensor types) and **presets** (specific calibrations).

### Browse Categories
```
LIST SENSORS                    # Show all sensor categories
LIST SENSORS NTC_THERMISTOR     # Show NTC thermistor presets
LIST SENSORS TEMPERATURE        # Show ALL temperature sensors
```

### Configure with Category + Preset
```
SET A2 SENSOR NTC_THERMISTOR VDO_120C_TABLE   # Two-layer syntax (preferred)
SET A2 SENSOR VDO_120C_TABLE                   # Legacy flat syntax (still works)
```

### Quick Examples
```
SET 6 CHT MAX6675                              # Thermocouple CHT
SET A2 COOLANT_TEMP NTC VDO_120C_TABLE        # NTC coolant sensor
SET A3 OIL_PRESSURE RESISTIVE_PRESSURE VDO_5BAR_CURVE  # Resistive oil pressure
SAVE
```

**That's it!** The system automatically handles calibration, conversion functions, and display formatting.

### Category Aliases
For convenience, these shorter aliases work:
- `NTC`, `THERMISTOR` → `NTC_THERMISTOR`
- `TC` → `THERMOCOUPLE`
- `RESISTIVE`, `PIEZO` → `RESISTIVE_PRESSURE`

---

## Sensor Categories

| Category | Aliases | Description |
|----------|---------|-------------|
| `THERMOCOUPLE` | TC | K-Type thermocouple amplifiers |
| `NTC_THERMISTOR` | NTC, THERMISTOR | NTC thermistor temperature sensors |
| `LINEAR_TEMP` | - | Linear voltage temperature sensors |
| `LINEAR_PRESSURE` | - | Linear voltage pressure sensors (0.5-4.5V) |
| `RESISTIVE_PRESSURE` | RESISTIVE, PIEZO | Resistive pressure senders (VDO, etc.) |
| `VOLTAGE` | - | Battery/voltage monitoring |
| `RPM` | - | Engine RPM sensors |
| `SPEED` | - | Vehicle speed sensors |
| `ENVIRONMENTAL` | - | Environmental sensors (BME280) |
| `DIGITAL` | - | Digital input sensors |

---

## Sensor Catalog (by Category)

### Temperature Sensors - Thermocouples

| Sensor ID | Description | Range | Interface |
|-----------|-------------|-------|-----------|
| `MAX6675` | K-Type thermocouple (MAX6675 amplifier) | 0-1024°C | SPI |
| `MAX31855` | K-Type thermocouple (MAX31855 amplifier) | -200-1350°C | SPI |

**Best for:** CHT (Cylinder Head Temperature), EGT (Exhaust Gas Temperature)

**Wiring:**
```
MAX6675/MAX31855 VCC → 5V (or 3.3V)
MAX6675/MAX31855 GND → GND
MAX6675/MAX31855 SCK → Pin 13 (SPI clock - shared)
MAX6675/MAX31855 SO  → Pin 12 (SPI MISO - shared)
MAX6675/MAX31855 CS  → Your configured pin (unique per sensor)
```

See [THERMOCOUPLE_GUIDE.md](THERMOCOUPLE_GUIDE.md) for detailed setup.

### Temperature Sensors - VDO Thermistors

| Sensor ID | Description | Range | Method |
|-----------|-------------|-------|--------|
| `VDO_120C_TABLE` | VDO 120°C sender (table) | -40 to 120°C | Most accurate |
| `VDO_120C_STEINHART` | VDO 120°C sender (Steinhart-Hart) | -40 to 120°C | Faster |
| `VDO_150C_TABLE` | VDO 150°C sender (table) | -40 to 150°C | Most accurate |
| `VDO_150C_STEINHART` | VDO 150°C sender (Steinhart-Hart) | -40 to 150°C | Faster |

**Best for:** Coolant temperature, oil temperature, transfer case temperature

**Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body grounds through engine block)
Add pull-down resistor: Analog pin → 1kΩ resistor → GND
```

See [VDO_SENSOR_GUIDE.md](VDO_SENSOR_GUIDE.md) for detailed setup.

### Temperature Sensors - Generic NTC Thermistors

| Sensor ID | Description | Notes |
|-----------|-------------|-------|
| `NTC_TABLE` | Generic NTC (custom lookup table) | Requires custom calibration |
| `NTC_STEINHART` | Generic NTC (custom Steinhart-Hart) | Requires custom calibration |

**Best for:** Custom NTC thermistors, non-VDO sensors

**Example with custom Steinhart-Hart coefficients:**
```
SET A2 COOLANT_TEMP NTC_THERMISTOR NTC_STEINHART
SET A2 STEINHART 10000 1.129e-3 2.341e-4 8.775e-8
```

See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for custom calibration.

### Pressure Sensors

| Sensor ID | Description | Range |
|-----------|-------------|-------|
| `VDO_2BAR_CURVE` | VDO 0-2 bar pressure sender | 0-29 PSI |
| `VDO_5BAR_CURVE` | VDO 0-5 bar pressure sender | 0-73 PSI |
| `GENERIC_BOOST` | Generic 0.5-4.5V boost sensor | Configurable |
| `MPX4250AP` | Freescale MAP sensor | 20-250 kPa |
| `MPX5700AP` | Freescale MAP sensor | 15-700 kPa |

**Best for:** Oil pressure, boost pressure, fuel pressure

**VDO Pressure Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body)
Add pull-down resistor: Analog pin → 1kΩ resistor → GND
```

See [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) for detailed setup.

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

See [BME280_GUIDE.md](BME280_GUIDE.md) for detailed setup.

### Digital Sensors

| Sensor ID | Description |
|-----------|-------------|
| `FLOAT_SWITCH` | Digital float switch (coolant level, etc.) |

---

## Lookup vs. Steinhart-Hart

For VDO thermistors, you can choose between two calibration methods:

### _TABLE Method (`_LOOKUP`)
- **More accurate** - Uses manufacturer's exact resistance/temperature table
- **Slightly slower** - Interpolates between table values
- **Recommended for:** Critical sensors (coolant, oil temp)

### Steinhart-Hart Method (`_STEINHART`)
- **Faster** - Direct mathematical calculation
- **Very good accuracy** - Within 1-2°C of lookup
- **Recommended for:** Non-critical sensors, faster loop times

**Example - mixing methods:**
```
SET A0 COOLANT_TEMP VDO_120C_TABLE     # Critical - maximum accuracy
SET A1 OIL_TEMP VDO_150C_STEINHART      # Less critical - faster
SAVE
```

---

## Application Types

Each input needs both an **Application** (what you're measuring) and a **Sensor** (physical hardware).

| Application | Description | Typical Sensors |
|-------------|-------------|-----------------|
| `CHT` | Cylinder Head Temperature | MAX6675, MAX31855 |
| `EGT` | Exhaust Gas Temperature | MAX31855 |
| `COOLANT_TEMP` | Engine Coolant Temperature | VDO_120C_TABLE |
| `OIL_TEMP` | Engine Oil Temperature | VDO_150C_TABLE |
| `TCASE_TEMP` | Transfer Case Temperature | VDO_150C_TABLE |
| `OIL_PRESSURE` | Engine Oil Pressure | VDO_5BAR_CURVE |
| `BOOST_PRESSURE` | Turbo/Supercharger Boost | VDO_2BAR_CURVE, GENERIC_BOOST |
| `FUEL_PRESSURE` | Fuel Rail Pressure | VDO_5BAR_CURVE |
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

```
# Thermocouple for CHT
SET 6 CHT MAX6675

# VDO sensors for coolant, oil temp, oil pressure
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A3 OIL_PRESSURE VDO_5BAR_CURVE

# Battery voltage
SET A8 PRIMARY_BATTERY VOLTAGE_DIVIDER

# Engine RPM from alternator
SET 5 ENGINE_RPM W_PHASE_RPM

# Set alarms
SET 6 ALARM 50 260
SET A2 ALARM 60 105
SET A0 ALARM 60 130
SET A3 ALARM 0.5 6

# Save to EEPROM
SAVE
```

---

## Common Questions

**Q: Can I use multiple of the same sensor type?**
A: Yes! Each input is independent. You can have multiple VDO_120C sensors on different pins.

**Q: What's the difference between LOOKUP and STEINHART?**
A: Same physical sensor, different math. Lookup is more accurate (±0.5°C), Steinhart is faster (±1°C).

**Q: Do I need to specify the bias resistor value?**
A: No. The presets use the default 1kΩ bias resistor (defined by DEFAULT_BIAS_RESISTOR).

**Q: What if I used a different bias resistor?**
A: Use `SET <pin> BIAS <ohms>` command. Example: `SET A0 BIAS 2200`

**Q: How do I see what sensors are available?**
A: Use `LIST SENSORS` command.

**Q: How do I see what applications are available?**
A: Use `LIST APPLICATIONS` command.

---

## See Also

- [THERMOCOUPLE_GUIDE.md](THERMOCOUPLE_GUIDE.md) - MAX6675/MAX31855 setup
- [VDO_SENSOR_GUIDE.md](VDO_SENSOR_GUIDE.md) - VDO temperature sensors
- [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) - Pressure sensor setup
- [W_PHASE_RPM_GUIDE.md](W_PHASE_RPM_GUIDE.md) - RPM from alternator
- [BME280_GUIDE.md](BME280_GUIDE.md) - Environmental sensor
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

