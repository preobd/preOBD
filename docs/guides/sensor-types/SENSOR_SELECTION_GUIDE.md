# Sensor Selection Guide

**How to choose and configure sensors for preOBD**

---

## Quick Start

Each input is configured with an **application** (what you measure) and a
**sensor** (the hardware):

```
SET <pin> <application> <sensor>
```

```
SET 6 CHT MAX6675                         # Thermocouple CHT
SET A2 COOLANT_TEMP VDO_120C_TABLE        # VDO coolant sender
SET A3 OIL_PRESSURE VDO_5BAR_CURVE        # VDO oil pressure sender
SAVE
```

The sensor's calibration, conversion and display formatting are set up automatically.

### Browsing the library on the device

```
LIST SENSORS                    # categories and counts
LIST SENSORS THERMISTOR         # sensors in one category
LIST SENSORS TEMPERATURE        # every sensor that measures temperature
LIST APPLICATIONS               # every application
```

You can also set the sensor on its own, optionally naming its category:

```
SET A2 SENSOR VDO_120C_TABLE
SET A2 SENSOR THERMISTOR VDO_120C_TABLE
```

---

## Sensor Categories

| Category | Aliases | Contents |
|----------|---------|----------|
| `THERMOCOUPLE` | `TC` | K-type thermocouple amplifiers |
| `THERMISTOR` | `NTC` | Resistive temperature senders, NTC thermistors, linear temperature sensors |
| `PRESSURE` | - | Resistive senders and 0.5-4.5V linear / MAP sensors |
| `VOLTAGE` | - | Battery voltage |
| `FREQUENCY` | `RPM`, `SPEED` | Alternator RPM and hall-effect speed |
| `ENVIRONMENTAL` | - | BME280 (I2C) |
| `DIGITAL` | - | Float switches |
| `LEVEL` | - | Fuel level senders and throttle position |

`LIST SENSORS` also accepts a measurement type in place of a category:
`TEMPERATURE`, `PRESSURE`, `VOLTAGE`, `RPM`, `SPEED`, `HUMIDITY`, `ELEVATION`, `DIGITAL`.

---

## Sensor Catalog (by Category)

### Temperature Sensors - Thermocouples

| Sensor ID | Description | Range | Interface |
|-----------|-------------|-------|-----------|
| `MAX6675` | K-Type thermocouple (MAX6675 amplifier) | 0-1024°C | SPI |
| `MAX31855` | K-Type thermocouple (MAX31855 amplifier) | -270-1372°C | SPI |

**Best for:** CHT (Cylinder Head Temperature), EGT (Exhaust Gas Temperature)

**Wiring:**
```
MAX6675/MAX31855 VCC → 5V (or 3.3V)
MAX6675/MAX31855 GND → GND
MAX6675/MAX31855 SCK → SPI clock, shared (Teensy 13, Mega 52)
MAX6675/MAX31855 SO  → SPI MISO, shared (Teensy 12, Mega 50)
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
| `VDO_200C_TABLE` | VDO 200°C cylinder-head sender (table) | up to 200°C | Table |

**Best for:** Coolant temperature, oil temperature, transfer case temperature

**Wiring:**
```
VDO Sensor Signal wire → Analog pin
VDO Sensor Ground → Chassis ground (sensor body grounds through engine block)
Add bias resistor: VCC → 100Ω resistor → Analog pin
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
| `GENERIC_NTC_BETA` | Generic NTC (Beta equation) | Set coefficients with `SET <pin> CAL BETA` |
| `GENERIC_NTC_STEINHART` | Generic NTC (Steinhart-Hart) | Set coefficients with `SET <pin> CAL STEINHART` |
| `GENERIC_NTC_TABLE` | Generic NTC (custom lookup table) | Table via JSON import |

**Best for:** Custom NTC thermistors, non-VDO sensors

**Example with custom Steinhart-Hart coefficients:**
```
SET A2 COOLANT_TEMP GENERIC_NTC_STEINHART
SET A2 CAL STEINHART 2490 1.129e-3 2.341e-4 8.775e-8
```

See [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for custom calibration.

### Pressure Sensors

| Sensor ID | Description | Range |
|-----------|-------------|-------|
| `VDO_2BAR_CURVE` | VDO 0-2 bar pressure sender | 0-29 PSI |
| `VDO_5BAR_CURVE` | VDO 0-5 bar pressure sender | 0-73 PSI |
| `GENERIC_BOOST` | Generic 0.5-4.5V boost sensor | 0-5 bar |
| `GENERIC_PRESSURE_150PSI` | Generic 0.5-4.5V sensor | 0-150 PSI |
| `AEM_30_2130_150` | AEM 30-2130-150 | 0-150 PSI |
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
Add bias resistor: VCC → 100Ω resistor → Analog pin
```

See [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) for detailed setup.

### Fluid Level Sensors

| Sensor ID | Description | Range | Bias Position |
|-----------|-------------|-------|---------------|
| `VDO_FUEL_LEVEL_180` | VDO 3–180Ω (European, ascending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_240` | VDO 240–34Ω (European, descending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_75` | VDO 75–3Ω (tubular, descending) | 0–100% | 100Ω |
| `VDO_FUEL_LEVEL_90` | VDO 0–90Ω (US standard, ascending) | 0–100% | 100Ω |
| `SW_FUEL_LEVEL_240` | Stewart-Warner 240–33Ω (US, descending) | 0–100% | 100Ω |
| `SW_FORD_FUEL_LEVEL` | Ford/Chrysler 73–10Ω (US, descending) | 0–100% | 100Ω |
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
| `VOLTAGE_DIVIDER` | Standard 12V battery monitoring | Assumes the standard divider below |

**Wiring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → Lower resistor → GND
  - 22kΩ for 3.3V boards (Teensy)
  - 6.8kΩ for 5V boards (Arduino)
```

**Note:** The firmware assumes these resistor values; see the [Voltage Sensor Guide](VOLTAGE_SENSOR_GUIDE.md) for other values.

### RPM Sensors

| Sensor ID | Description | Notes |
|-----------|-------------|-------|
| `W_PHASE_RPM` | W-phase alternator RPM | Requires voltage protection circuit |
| `HALL_SPEED` | Hall-effect vehicle speed sensor | See [HALL_SPEED_GUIDE.md](HALL_SPEED_GUIDE.md) |

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

### `_TABLE` Method
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

Each input needs both an **application** (what you're measuring) and a **sensor** (the hardware). The warmup is how long after start-up its alarm waits before it can trigger (see the [Alarm System Guide](../configuration/ALARM_SYSTEM_GUIDE.md)).

| Application | Description | Warmup | Default Sensor |
|-------------|-------------|--------|----------------|
| `CHT` | Cylinder Head Temperature | 30s | `MAX31855` |
| `EGT` | Exhaust Gas Temperature | 20s | `MAX31855` |
| `COOLANT_TEMP` | Engine Coolant Temperature | 60s | `VDO_120C_STEINHART` |
| `OIL_TEMP` | Engine Oil Temperature | 60s | `VDO_150C_STEINHART` |
| `TCASE_TEMP` | Transfer Case Fluid Temperature | 60s | `VDO_120C_STEINHART` |
| `AMBIENT_TEMP` | Ambient Air Temperature | 0s | `BME280_TEMP` |
| `OIL_PRESSURE` | Engine Oil Pressure | 5s | `VDO_5BAR_CURVE` |
| `BOOST_PRESSURE` | Boost Pressure | 1s | `VDO_2BAR_CURVE` |
| `FUEL_PRESSURE` | Fuel Pressure | 2s | — |
| `BAROMETRIC_PRESSURE` | Barometric Pressure | 0s | `BME280_PRESSURE` |
| `PRIMARY_BATTERY` | Primary Battery Voltage | 0s | `VOLTAGE_DIVIDER` |
| `AUXILIARY_BATTERY` | Auxiliary Battery Voltage | 0s | `VOLTAGE_DIVIDER` |
| `COOLANT_LEVEL` | Engine Coolant Level | 5s | `FLOAT_SWITCH` |
| `HUMIDITY` | Relative Humidity | 0s | `BME280_HUMIDITY` |
| `ELEVATION` | Elevation | 0s | `BME280_ELEVATION` |
| `ENGINE_RPM` | Engine RPM | 2s | `W_PHASE_RPM` |
| `HYDRAULIC_PRESSURE` | Hydraulic Pressure | 2s | `VDO_10BAR_TABLE` |
| `BRAKE_PRESSURE` | Brake Pressure | 1s | `VDO_10BAR_TABLE` |
| `FUEL_LEVEL` | Fuel Tank Level | 2s | `VDO_FUEL_LEVEL_180` |
| `VEHICLE_SPEED` | Vehicle Speed | 0s | `HALL_SPEED` |
| `TRANS_TEMP` | Transmission Fluid Temperature | 60s | `VDO_150C_STEINHART` |
| `INTAKE_AIR_TEMP` | Intake Air Temperature | 10s | `VDO_120C_STEINHART` |
| `MAP_PRESSURE` | Manifold Absolute Pressure | 1s | `MPX4250AP` |
| `THROTTLE_POSITION` | Throttle Position | 0s | `GENERIC_TPS` |
| `DIFF_TEMP` | Differential Fluid Temperature | 60s | `VDO_150C_STEINHART` |
| `GEAR_OIL_TEMP` | Gearbox Fluid Temperature | 60s | `VDO_150C_STEINHART` |
| `OIL_LEVEL` | Engine Oil Level | 10s | `FLOAT_SWITCH` |
| `INTERCOOLER_TEMP` | Intercooler Temperature | 10s | `GENERIC_TEMP_LINEAR` |

`LIST APPLICATIONS` prints the same list on the device.

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
RULE ADD 6 OUTSIDE 50 260
RULE ADD A2 OUTSIDE 60 105
RULE ADD A0 OUTSIDE 60 130
RULE ADD A3 OUTSIDE 0.5 6

# Save to EEPROM
SAVE
```

---

## Common Questions

**Q: Can I use multiple of the same sensor type?**
A: Yes! Each input is independent. You can have several `VDO_120C_TABLE` sensors on different pins.

**Q: What's the difference between TABLE and STEINHART?**
A: Same physical sensor, different math. Lookup is more accurate (±0.5°C), Steinhart is faster (±1°C).

**Q: Do I need to specify the bias resistor value?**
A: For most presets, no — each preset has its correct default baked in. **VDO and other
low-impedance gauge senders default to 100Ω**; **high-impedance NTC sensors (Jeep XJ temp,
Renix CTS) default to 2.49kΩ**. These defaults match the required PCB bias position and take
effect automatically when you assign the sensor — no `SET <pin> CAL BIAS` command needed unless
you've wired a non-standard value. See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md).

**Q: What if I used a different bias resistor?**
A: Use `SET <pin> CAL BIAS <ohms>` command. Example: `SET A0 CAL BIAS 2200`

**Q: How do I see what sensors are available?**
A: Use `LIST SENSORS` command.

**Q: How do I see what applications are available?**
A: Use `LIST APPLICATIONS` command.

## Complete Sensor List

Every sensor in the firmware library, by category (`LIST SENSORS <category>` shows the same on the device):

**THERMOCOUPLE**

| Sensor | Description | Measures |
|---|---|---|
| `MAX6675` | K-Type Thermocouple (0-1024°C) | Temperature |
| `MAX31855` | K-Type Thermocouple (-270-1372°C) | Temperature |

**THERMISTOR**

| Sensor | Description | Measures |
|---|---|---|
| `VDO_120C_TABLE` | VDO 120C (table) | Temperature |
| `VDO_150C_TABLE` | VDO 150C (table) | Temperature |
| `VDO_200C_TABLE` | VDO 200C Cylinder Temp (table) | Temperature |
| `VDO_120C_STEINHART` | VDO 120C (Steinhart-Hart) | Temperature |
| `VDO_150C_STEINHART` | VDO 150C (Steinhart-Hart) | Temperature |
| `GENERIC_NTC_TABLE` | Generic NTC (custom table) | Temperature |
| `GENERIC_NTC_STEINHART` | Generic NTC (custom Steinhart-Hart) | Temperature |
| `GENERIC_NTC_BETA` | Generic NTC (custom Beta equation) | Temperature |
| `SMITHS_TT6811_TEMP` | Smiths TT6811 Coolant Temp (MG/Triumph/Austin-Healey) | Temperature |
| `SMITHS_TT4201_TEMP` | Smiths TT4201 Coolant Temp (Jaguar E-Type) | Temperature |
| `SMITHS_GTR104_TEMP` | Smiths GTR104 Coolant Temp (generic British gauge, approx) | Temperature |
| `ACDELCO_TEMP_PRE79` | AC Delco GM Coolant Temp Sender, pre-1979 (1967-1978 trucks) | Temperature |
| `ACDELCO_TEMP_POST79` | AC Delco GM Coolant Temp Sender, 1979+ (1979-1990 trucks) | Temperature |
| `BOSCH_NTC_M12` | Bosch NTC M12 EFI Temp Sensor (-40 to 130°C) | Temperature |
| `JEEP_4_0_TEMP_GAUGE` | Jeep 4.0L Coolant Temp Sender (Gauge) (0-120°C) | Temperature |
| `JEEP_RENIX_CTS` | Jeep Renix ECU Coolant Temp Sensor (-20-180°C) | Temperature |
| `JEEP_CJ_TEMP_GAUGE` | Jeep CJ Coolant Temp Sender (Gauge) (49-127°C, approx) | Temperature |
| `GENERIC_TEMP_LINEAR` | 0.5-4.5V linear (-40 to 150°C) | Temperature |

**PRESSURE**

| Sensor | Description | Measures |
|---|---|---|
| `GENERIC_BOOST` | 0.5-4.5V linear (0-5 bar) | Pressure |
| `GENERIC_PRESSURE_150PSI` | 0.5-4.5V linear (0-150 PSI / 10 bar) | Pressure |
| `AEM_30_2130_150` | AEM 150 PSI (0-150 PSI / 10 bar) | Pressure |
| `MPX4250AP` | Freescale/NXP (20-250 kPa) | Pressure |
| `MPX5700AP` | Freescale/NXP (15-700 kPa) | Pressure |
| `VDO_2BAR_CURVE` | VDO 2 Bar (curve fit) | Pressure |
| `VDO_5BAR_CURVE` | VDO 5 Bar (curve fit) | Pressure |
| `VDO_2BAR_TABLE` | VDO 2 Bar (table) | Pressure |
| `VDO_5BAR_TABLE` | VDO 5 Bar (table) | Pressure |
| `VDO_10BAR_TABLE` | VDO 10 Bar (table) | Pressure |
| `VDO_10BAR_CURVE` | VDO 10 Bar (curve fit) | Pressure |
| `VDO_25BAR_TABLE` | VDO 25 Bar Gear Oil (table) | Pressure |
| `VDO_80PSI_TABLE` | VDO 80 PSI Oil (US, descending, table) | Pressure |
| `SMITHS_OIL_PRESSURE_BP` | Smiths BP/ACP Oil Pressure Sender (0-80 PSI) | Pressure |
| `SW_OIL_PRESSURE` | Stewart-Warner Oil Pressure Sender (0-80 PSI) | Pressure |
| `JEEP_4_0_OIL_GAUGE` | Jeep 4.0L Oil Pressure Sender (Gauge) (0-80 PSI) | Pressure |
| `JEEP_CJ_OIL_THIN` | Jeep CJ Oil Pressure Sender, 1" Thin Gauge (0-80 PSI) | Pressure |
| `JEEP_CJ_OIL_DEEP` | Jeep CJ Oil Pressure Sender, 2" Deep Gauge (0-80 PSI) | Pressure |

**VOLTAGE**

| Sensor | Description | Measures |
|---|---|---|
| `VOLTAGE_DIVIDER` | Battery voltage (12V divider) | Voltage |

**FREQUENCY**

| Sensor | Description | Measures |
|---|---|---|
| `W_PHASE_RPM` | W-phase alternator RPM | RPM |
| `HALL_SPEED` | Hall Effect Speed Sensor | Speed |

**ENVIRONMENTAL**

| Sensor | Description | Measures |
|---|---|---|
| `BME280_TEMP` | BME280 temperature (I2C) | Temperature |
| `BME280_PRESSURE` | BME280 barometric pressure (I2C) | Pressure |
| `BME280_HUMIDITY` | BME280 relative humidity (I2C) | Humidity |
| `BME280_ELEVATION` | BME280 altitude (I2C) | Elevation |

**DIGITAL**

| Sensor | Description | Measures |
|---|---|---|
| `FLOAT_SWITCH` | Float/level switch (digital) | Digital |

**LEVEL**

| Sensor | Description | Measures |
|---|---|---|
| `GENERIC_TPS` | 0.5-4.5V linear (0-100% throttle) | Percent |
| `VDO_FUEL_LEVEL_180` | VDO Fuel Level 3-180Ω (European, ascending) | Percent |
| `VDO_FUEL_LEVEL_240` | VDO Fuel Level 240-34Ω (European, descending) | Percent |
| `VDO_FUEL_LEVEL_75` | VDO Fuel Level 75-3Ω (tubular, descending) | Percent |
| `VDO_FUEL_LEVEL_90` | VDO Fuel Level 0-90Ω (US standard, ascending) | Percent |
| `SW_FUEL_LEVEL_240` | Stewart-Warner Fuel Level 240-33Ω (US, descending) | Percent |
| `SW_FORD_FUEL_LEVEL` | Ford/Chrysler Fuel Level 73-10Ω (US, descending) | Percent |
| `JEEP_CJ_FUEL_LEVEL` | Jeep CJ Fuel Level Sender (0-100%, 10-73Ω) | Percent |

CAN-imported inputs use `CAN_IMPORT` automatically (see the [CAN Sensor Import Guide](CAN_SENSOR_IMPORT_GUIDE.md)).

---

## See Also

- [THERMOCOUPLE_GUIDE.md](THERMOCOUPLE_GUIDE.md) - MAX6675/MAX31855 setup
- [THERMISTOR_GUIDE.md](THERMISTOR_GUIDE.md) - Resistive temperature sensors (NTC, VDO senders)
- [PRESSURE_SENSOR_GUIDE.md](PRESSURE_SENSOR_GUIDE.md) - Pressure sensor setup
- [W_PHASE_RPM_GUIDE.md](W_PHASE_RPM_GUIDE.md) - RPM from alternator
- [BME280_GUIDE.md](BME280_GUIDE.md) - Environmental sensor
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations

