# preOBD Quick Reference

**Fast lookup guide for common tasks and configurations**

---

## ⚠️ Safety First

- **Beta software** - use at your own risk
- **Always maintain mechanical backup gauges**
- **Test thoroughly before trusting readings**
- **Monitor your engine actively during testing**

---

## Quick Start

New board? Follow [Getting Started](GETTING_STARTED.md) to install the tools and flash it.

### Configure Your Sensors

```
CONFIG                                    # Enter configuration mode
SET 7 EGT MAX31855                       # Pin 7: EGT with thermocouple
SET A0 OIL_TEMP VDO_150C_STEINHART        # Pin A0: VDO 150C using Steinhart Calibration
SET A2 COOLANT_TEMP VDO_120C_TABLE      # Pin A2: VDO coolant sensor using Lookup table
SET A3 OIL_PRESSURE GENERIC_PRESSURE_150PSI  # Pin A3: Generic 0.5-4.5V pressure
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER   # Pin A6: Battery voltage
SAVE                                      # Save configuration
RUN                                       # Start monitoring
```

Use `LIST SENSORS` to see sensor categories, then `LIST SENSORS <category>` for specific sensors.

### Operate

**MODE_BUTTON:**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Acknowledge the sounding alarm(s) (until that alarm clears by default; see `ALARM DURATION`)
- Hold for 2 seconds in RUN mode → Toggle the display on/off

---

## Common Sensor Configurations

### K-Type Thermocouple (CHT/EGT)

```
BUS SPI 0                           # once: bring up the SPI bus (then SAVE, SYSTEM REBOOT)
SET 7 EGT MAX31855                  # EGT, chip-select on pin 7
```

**Wiring:**
```
MAX31855 VCC → 3.3V (or 5V)
MAX31855 GND → GND
MAX31855 SCK → SPI clock (Teensy pin 13, Mega pin 52)
MAX31855 SO  → SPI MISO (Teensy pin 12, Mega pin 50)
MAX31855 CS  → Your configured pin
```

### VDO Temperature Sensor

```
SET A2 COOLANT_TEMP VDO_120C_TABLE   # Coolant (most accurate)
SET A4 OIL_TEMP VDO_150C_STEINHART    # Oil temp (faster processing)
SET A5 TCASE_TEMP VDO_150C_STEINHART  # Transfer case
```

**Wiring:**
```
VDO Sensor Signal → Analog pin (A2)
VDO Sensor Ground → Chassis ground
Add bias resistor: Pin → 100Ω → 3.3V/5V
```

### Generic NTC Thermistor

For non-VDO NTC thermistors (common 10K sensors):

```
SET A0 OIL_TEMP GENERIC_NTC_BETA                     # Beta equation (from the datasheet)
SET A0 CAL BETA 2490 3950 10000 25                   # bias, beta, R0, T0
SET A2 COOLANT_TEMP GENERIC_NTC_STEINHART            # Steinhart-Hart
SET A2 CAL STEINHART 2490 1.129e-3 2.341e-4 8.775e-8 # bias, A, B, C
```

**Wiring:**
```
NTC Thermistor → Between analog pin and GND
Bias resistor  → Between analog pin and 3.3V/5V
(Recommended: 2.49kΩ bias for high-impedance NTC sensors)
```

### VDO Pressure Sensor

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE          # Oil pressure
SET A7 BOOST_PRESSURE VDO_2BAR_CURVE        # Boost pressure
SET A8 FUEL_PRESSURE VDO_10BAR_TABLE  # Fuel pressure
```

**Wiring:** Same as temperature sensor (signal + ground + 100Ω bias to VCC)

### Generic Linear Pressure Sensor (0.5-4.5V)

For modern 3-wire pressure sensors with linear voltage output:

```
SET A3 OIL_PRESSURE GENERIC_PRESSURE_150PSI  # 150 PSI oil pressure
SET A4 BOOST_PRESSURE GENERIC_BOOST   # 0-5 bar boost
SET A5 BOOST_PRESSURE MPX4250AP       # Freescale MAP sensor
```

**Custom range (any 0.5-4.5V sensor):**
```
SET A3 OIL_PRESSURE GENERIC_PRESSURE_150PSI
SET A3 CAL PRESSURE_LINEAR 0.5 4.5 0.0 7.0   # 0.5-4.5V maps to 0-7 bar
```

**Wiring (5V boards — Mega):**
```
Sensor VCC    → 5V regulated
Sensor GND    → GND
Sensor Signal → Analog pin
```

**Wiring (3.3V boards — Teensy, ESP32):**

⚠️ These sensors output up to 4.5V. A voltage divider is required to protect the ADC.

```
Sensor VCC    → 5V regulated
Sensor GND    → GND
Sensor Signal → 18kΩ → Analog pin
                         │
                        33kΩ
                         │
                        GND
```

This scales 0.5–4.5V to ~0.32–2.91V — safely within the 3.3V ADC range.
Tell the firmware about the divider (33k / (18k + 33k) = 0.647) and keep the
calibration in sensor-side volts:
```
SET A3 DIVIDER 0.647
SET A3 CAL PRESSURE_LINEAR 0.5 4.5 0.0 7.0     # example: 0-7 bar sensor
```

### Battery Voltage

```
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A7 AUXILIARY_BATTERY VOLTAGE_DIVIDER
```

**Wiring (voltage divider):**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → 22kΩ → GND (for 3.3V boards)
Junction → 6.8kΩ → GND (for 5V boards)
```

### Engine RPM (Alternator W-Phase)

```
SET 3 ENGINE_RPM W_PHASE_RPM
```

**⚠️ CRITICAL:** Requires voltage protection circuit for 3.3V boards! See [W_PHASE_RPM_GUIDE.md](../guides/sensor-types/W_PHASE_RPM_GUIDE.md).

### Vehicle Speed (Hall Effect Sensor)

```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 CAL SPEED 100 2008 3.73 2000 300
```

**Parameters:** pulses_per_rev (100), tire_circumference_mm (2008), final_drive_ratio (3.73), timeout_ms (2000), max_speed_kph (300)

**⚠️ CRITICAL:** 12V sensors require voltage protection for 3.3V boards! See [HALL_SPEED_GUIDE.md](../guides/sensor-types/HALL_SPEED_GUIDE.md).

### Environmental (BME280)

```
BUS I2C 0                           # once: bring up the I2C bus (then SAVE, SYSTEM REBOOT)
SET I2C:0 AMBIENT_TEMP BME280_TEMP
SET I2C:1 BAROMETRIC_PRESSURE BME280_PRESSURE
SET I2C:2 HUMIDITY BME280_HUMIDITY
SET I2C:3 ELEVATION BME280_ELEVATION
```

### Float Switch (Coolant Level)

```
SET 4 COOLANT_LEVEL FLOAT_SWITCH
```

---

## Output Modules

### OBD-II Scanner Support (ELM327 / Torque)

```
BUS CAN OUTPUT CAN1 ENABLE 500000   # Bring up the CAN bus your transceiver is on
OUTPUT CAN ENABLE                   # Answer OBD-II requests on it
# Plug in ELM327 Bluetooth adapter to OBD-II port
# Open Torque app → Settings → OBD/ECU → Connection → Bluetooth
# Select your ELM327 adapter
# View real-time sensor data
```

**Supported apps**: Torque, OBD Fusion, Car Scanner, DashCommand

See [OBD-II Scanner Guide](../guides/outputs/OBD2_SCANNER_GUIDE.md) for details.

### RealDash Dashboard

```
OUTPUT RealDash ENABLE              # RealDash frames on the data channel (USB or BLE module)
OUTPUT Serial DISABLE               # CSV shares that channel
```

See [RealDash Setup Guide](../guides/outputs/REALDASH_SETUP_GUIDE.md) for details.

---

## Command Reference

### Getting Help

The help system is hierarchical and organized by category:

| Command | Description |
|---------|-------------|
| `HELP` | Show category overview |
| `HELP <category>` | Show detailed help (LIST, SET, CONTROL, OUTPUT, BUS, DISPLAY, TRANSPORT, SYSTEM, CONFIG, EXAMPLES) |
| `HELP QUICK` | Show compact command reference |
| `?` | Alias for HELP |

**Examples:**
```
HELP                    # Show all categories
HELP SET                # Show all SET commands
HELP EXAMPLES           # Show worked examples
```

### Configuration Commands (CONFIG mode only)

| Command | Description | Example |
|---------|-------------|---------|
| `SET <pin> <app> <sensor>` | Configure input (also seeds a default alarm Rule) | `SET A2 COOLANT_TEMP VDO_120C_TABLE` |
| `SET <pin> UNITS <unit>` | Set display units | `SET A2 UNITS FAHRENHEIT` |
| `SET <pin> NAME <name>` | Set full display label | `SET A2 NAME Coolant` |
| `SET <pin> ABBR <abbr>` | Set short (LCD) label | `SET A2 ABBR CLT` |
| `CLEAR <pin>` | Remove input (and its Rules) | `CLEAR A2` |
| `SET <pin> INPUT ENABLE` | Enable input | `SET A2 INPUT ENABLE` |
| `SET <pin> INPUT DISABLE` | Disable input | `SET A2 INPUT DISABLE` |
| `SAVE` | Save to EEPROM | `SAVE` |

### Query Commands (any mode)

| Command | Description |
|---------|-------------|
| `LIST INPUTS` | Show all configured inputs |
| `LIST APPLICATIONS` | Show available applications |
| `LIST SENSORS` | Show sensor categories |
| `LIST SENSORS <category>` | Show sensors in category |
| `INFO <pin>` | Show input details |
| `SYSTEM DUMP` | Show complete configuration |
| `VERSION` | Show firmware version |
| `HELP <category>` | Show help for category |

### Mode Commands

| Command | Description |
|---------|-------------|
| `CONFIG` | Enter configuration mode |
| `RUN` | Enter run mode |
| `LOAD` | Reload from EEPROM |

### Output Commands

| Command | Description |
|---------|-------------|
| `LIST OUTPUTS` | Show all output modules |
| `OUTPUT STATUS` | Show which outputs are enabled |
| `OUTPUT <n> ENABLE` | Enable output |
| `OUTPUT <n> DISABLE` | Disable output |
| `OUTPUT <n> INTERVAL <ms>` | Set update interval |

### Display Commands

| Command | Description |
|---------|-------------|
| `DISPLAY STATUS` | Show display config |
| `DISPLAY ENABLE` | Enable display |
| `DISPLAY DISABLE` | Disable display |
| `DISPLAY TYPE <LCD\|NONE>` | Set display type |
| `DISPLAY INTERVAL <ms>` | Set display refresh rate |

### System Commands

| Command | Description |
|---------|-------------|
| `SYSTEM STATUS` | Show global configuration |
| `SYSTEM DUMP` | Complete system dump |
| `SYSTEM DUMP JSON` | Export configuration as JSON |
| `SYSTEM UNITS TEMP <C\|F>` | Set default temperature units |
| `SYSTEM UNITS PRESSURE <BAR\|PSI\|KPA\|INHG>` | Set default pressure units |
| `SYSTEM UNITS ELEVATION <M\|FT>` | Set default elevation units |
| `SYSTEM UNITS SPEED <KPH\|MPH>` | Set default speed units |
| `SYSTEM SEA_LEVEL <hPa>` | Set sea level pressure |
| `SYSTEM INTERVAL SENSOR <ms>` | Set sensor read interval |
| `SYSTEM INTERVAL ALARM <ms>` | Set alarm check interval |
| `SYSTEM REBOOT` | Restart the device |
| `SYSTEM RESET CONFIRM` | Factory reset (erase config + reboot) |

### Rules & Actions (control engine)

Alarms and relays are unified Rules → Actions. `SET <pin> ALARM` is a shortcut
facade for a single per-input alarm Rule (band or one-sided); `RULE`/`ACTION` are
the full surface. (`RELAY` is deprecated and prints a pointer here.)

| Command | Description |
|---------|-------------|
| `SET <pin> ALARM <min> <max>` | Per-input alarm: band (alarm outside Min..Max) |
| `SET <pin> ALARM MIN <v>` / `MAX <v>` | Per-input alarm: one-sided floor / ceiling |
| `SET <pin> ALARM WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>]` | Set/clear the alarm Rule's warning threshold |
| `SET <pin> ALARM DISABLE` | Remove the input's alarm Rule |
| `RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>] [WARN <lo|NONE|AUTO>] [INFO|WARNING|ALARM|NONE]` | Threshold Rule |
| `RULE ADD <pin> OUTSIDE <lo> <hi> [HYST <w>] [WARN <lo|NONE|AUTO> <hi|NONE|AUTO>] [INFO|WARNING|ALARM|NONE]` | Both-sided band Rule |
| `RULE LIST` / `RULE <id>` / `RULE DEL <id>` | View / inspect / delete |
| `RULE <id> WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>]` | Edit an ALARM rule's warning threshold in place |
| `RULE <id> LIMIT ABOVE|BELOW <limit>` / `LIMIT OUTSIDE <lo> <hi>` | Move a Rule's limits in place |
| `RULE <id> SEVERITY INFO|WARNING|ALARM|NONE` | Change a Rule's severity in place |
| `RULE RESET <id>` | Restore the application's default Rule config |
| `RULE <id> ENABLE|DISABLE` | Re-arm / park a Rule without deleting it (`PARKED` never fires) |
| `ACTION ADD RELAY <pin> [FAILSAFE ON|OFF] [INVERT]` | Define an actuator |
| `ACTION <id> RULE <ruleId>` | Drive the Action from a Rule (replaces its membership) |
| `ACTION <id> RULE ADD|REMOVE <ruleId>` | Add / drop one member Rule — the Action is OR-driven |
| `ACTION <id> ON|OFF` | Manual override (RUN-mode allowed) |
| `ACTION <id> AUTO` | Release the override and return to Rule control |
| `ACTION <id> NAME [text]` | Set the operator label; no text clears it |
| `ACTION LIST` / `ACTION DEL <id>` | View / delete |

**Example - Cooling fan (coolant > 90°C, 5° hysteresis):**
```
RULE ADD A2 ABOVE 90 HYST 5 NONE     # control Rule (no annunciation), id e.g. 0
ACTION ADD RELAY 23                   # relay on pin 23, id e.g. 0
ACTION 0 RULE 0                       # fan follows the Rule
```

**...also demanded by trans temp** — an Action runs while **any** member Rule is
active:
```
RULE ADD A3 ABOVE 110 HYST 5 NONE    # second control Rule, id e.g. 1
ACTION 0 RULE ADD 1                   # fan now follows rule 0 OR rule 1
```

See [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md) and
[Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md) for details.

### Bus Commands

| Command | Description |
|---------|-------------|
| `BUS` | Show bus command help |
| `BUS I2C [0\|1\|2]` | Show or select I2C bus (Wire/Wire1/Wire2) |
| `BUS I2C CLOCK <kHz>` | Set I2C clock (100, 400, 1000) |
| `BUS SPI [0\|1\|2]` | Show or select SPI bus (SPI/SPI1/SPI2) |
| `BUS SPI CLOCK <Hz>` | Set SPI clock speed |
| `BUS CAN` | Show CAN bus status |
| `BUS CAN BAUDRATE <bps>` | Set CAN baudrate |
| `BUS CAN INPUT\|OUTPUT <bus> ENABLE\|DISABLE` | Choose which CAN bus carries input and output |

**Example - Switch to Wire1:**
```
BUS I2C 1                   # Select Wire1 (pins 17/16 on Teensy 4.x)
SAVE
SYSTEM REBOOT               # Reboot to apply
```

See [Serial Commands Reference](../reference/SERIAL_COMMANDS.md#bus-configuration) for details.

---

## Available Units

| Unit | Description |
|------|-------------|
| `CELSIUS` or `C` | Temperature in Celsius |
| `FAHRENHEIT` or `F` | Temperature in Fahrenheit |
| `PSI` | Pressure in PSI |
| `BAR` | Pressure in bar |
| `KPA` | Pressure in kilopascals |
| `INHG` | Pressure in inches of mercury |
| `VOLTS` or `V` | Voltage |
| `RPM` | Revolutions per minute |
| `KPH` | Speed in kilometers per hour |
| `MPH` | Speed in miles per hour |
| `PERCENT` or `%` | Percentage |
| `METERS` or `M` | Altitude in meters |
| `FEET` or `FT` | Altitude in feet |

---

## Sensors and Applications

`LIST SENSORS` and `LIST APPLICATIONS` show everything on the device; the
[Sensor Selection Guide](../guides/sensor-types/SENSOR_SELECTION_GUIDE.md) has the
complete tables.

### Custom Calibration
```
SET <pin> CAL RPM <poles> <ratio> [<mult>] <timeout> <min> <max>     # Custom RPM
SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> [<mult>] <timeout> <max>  # Custom speed
SET <pin> CAL STEINHART <bias> <a> <b> <c>           # Custom Steinhart-Hart
SET <pin> CAL BETA <bias> <beta> <r0> <t0>           # Custom Beta equation
SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>  # Custom linear range
SET <pin> CAL BIAS <resistor>                         # Override bias resistor
```

---

## Common Mistakes

1. **Wrong sensor type** - VDO 120C vs 150C makes huge difference
2. **Wrong bias resistor** - VDO/low-impedance senders need 100Ω; high-impedance NTC (10K) sensors need 2.49kΩ. See [Bias Resistor Guide](../guides/hardware/BIAS_RESISTOR_GUIDE.md).
3. **Wrong I2C address** - Try both 0x27 and 0x3F for LCD
4. **5V to 3.3V board** - Will destroy Teensy/ESP32!
5. **No CAN termination** - CAN bus needs 120Ω resistors at each end
6. **Loose connections** - Vibration in engine bay breaks wires
7. **Forgetting to SAVE** - Configuration must be saved to EEPROM

---

## Troubleshooting

See [Troubleshooting](../TROUBLESHOOTING.md).

---

## Getting Help

**Documentation:**
- [Full Documentation](../README.md)
- [Sensor Selection Guide](../guides/sensor-types/SENSOR_SELECTION_GUIDE.md)
- [Serial Commands Reference](../reference/SERIAL_COMMANDS.md)

**Support:**
- GitHub Issues - Bug reports
- GitHub Discussions - Questions

**When asking for help, include:**
- Board type (Mega, Teensy 4.0, etc.)
- Firmware version (`VERSION` command)
- Your configuration (`SYSTEM DUMP` command)
- What you expected vs. what happened

