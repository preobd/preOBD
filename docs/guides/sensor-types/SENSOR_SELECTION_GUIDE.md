# Sensor Selection Guide

**How to choose and configure sensors for preOBD**

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
Add pull-down resistor: Analog pin → 100Ω resistor → GND
```

See [THERMISTOR_GUIDE.md](THERMISTOR_GUIDE.md) for detailed setup.

### Temperature Sensors - Jeep/AMC

| Sensor ID | Description | Range | Era | Bias Position |
|-----------|-------------|-------|-----|---------------|
| `JEEP_4_0_TEMP_GAUGE` | XJ Cherokee 4.0L coolant gauge sender (Mopar 56027012) | 0–120°C | 1984–1996 | **2.49kΩ** |
| `JEEP_RENIX_CTS` | XJ Cherokee 4.0L Renix ECU coolant temp sensor | -20–180°C | 1987–1990 | **2.49kΩ** |
| `JEEP_CJ_TEMP_GAUGE` | Jeep CJ coolant gauge sender (Crown J3212002) | 49–127°C (approx) | 1972–1986 | 100Ω |

**Important — XJ vs CJ distinction:**
- The XJ 4.0L gauge sender (56027012, 135–7800Ω) is **not** the same family as the CJ-era sender (9–73Ω). They require different bias positions.
- `JEEP_CJ_TEMP_GAUGE` temperature calibration is community-derived (AMC FSM documents gauge positions, not R-vs-T). Accuracy ±5°C.

**Bias:**
```
JEEP_4_0_TEMP_GAUGE / JEEP_RENIX_CTS: use 2.2kΩ–2.49kΩ bias resistor
JEEP_CJ_TEMP_GAUGE: use 100Ω bias resistor (or preOBD PCB 100Ω position)
```

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
| `JEEP_4_0_OIL_GAUGE` | XJ Cherokee 4.0L oil pressure sender (Mopar 56026779) | 0–80 PSI |
| `JEEP_CJ_OIL_THIN` | Jeep CJ oil pressure sender, 1" thin gauge (Crown #3212004) | 0–80 PSI |
| `JEEP_CJ_OIL_DEEP` | Jeep CJ oil pressure sender, 2" deep gauge (1976–~1982) | 0–80 PSI |

**Best for:** Oil pressure, boost pressure, fuel pressure

**Jeep oil sender selection:**
- **XJ Cherokee (1984–1996):** use `JEEP_4_0_OIL_GAUGE` (2–90Ω, 100Ω bias)
- **Jeep CJ with 1" thin gauge (~1982–1986):** use `JEEP_CJ_OIL_THIN` (9–70Ω, 100Ω bias)
- **Jeep CJ with 2" deep gauge (1976–~1982):** use `JEEP_CJ_OIL_DEEP` (33–240Ω, 100Ω bias)

**VDO Pressure Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body)
Add pull-down resistor: Analog pin → 100Ω resistor → GND
```

See [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) for detailed setup.

### Fluid Level Sensors

| Sensor ID | Description | Range | Bias Position |
|-----------|-------------|-------|---------------|
| `VDO_FUEL_LEVEL_180` | VDO 3–180Ω (European, ascending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_240` | VDO 240–34Ω (European, descending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_75` | VDO 75–3Ω (tubular, descending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_90` | VDO 0–90Ω (US standard, ascending) | 0–100% | 100Ω |
| `JEEP_CJ_FUEL_LEVEL` | Jeep CJ fuel tank sender (1972–1986) | 0–100% | **100Ω** |

**Application:** `FUEL_LEVEL`

**Jeep CJ fuel sender notes:**
- Resistance range 10–73Ω (Full→Empty) — use 100Ω bias position on preOBD PCB
- Calibration based on 3 AMC FSM data points (Empty/Half/Full); quarter-tank values are linearly interpolated. Accuracy sufficient for gauge use.

**Wiring:**
```
Sender signal wire → Analog pin
Sender body / ground wire → Chassis ground
Bias resistor → see Bias Position column above
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
A: For most presets, no — each preset has its correct default baked in. **VDO and other
low-impedance gauge senders default to 100Ω**; **high-impedance NTC sensors (Jeep XJ temp,
Renix CTS) default to 2.49kΩ**. These defaults match the required PCB bias position and take
effect automatically when you assign the sensor — no `SET <pin> BIAS` command needed unless
you've wired a non-standard value. See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md).

**Q: What if I used a different bias resistor?**
A: Use `SET <pin> BIAS <ohms>` command. Example: `SET A0 BIAS 2200`

**Q: How do I see what sensors are available?**
A: Use `LIST SENSORS` command.

**Q: How do I see what applications are available?**
A: Use `LIST APPLICATIONS` command.

---

## See Also

- [THERMOCOUPLE_GUIDE.md](THERMOCOUPLE_GUIDE.md) - MAX6675/MAX31855 setup
- [THERMISTOR_GUIDE.md](THERMISTOR_GUIDE.md) - Resistive temperature sensors (NTC, VDO senders)
- [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) - Pressure sensor setup
- [W_PHASE_RPM_GUIDE.md](W_PHASE_RPM_GUIDE.md) - RPM from alternator
- [BME280_GUIDE.md](BME280_GUIDE.md) - Environmental sensor
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

