# preOBD Quick Reference

**Fast lookup guide for common tasks and configurations**

---

## ⚠️ Safety First

- **Beta software** - use at your own risk
- **Always maintain mechanical backup gauges**
- **Test thoroughly before trusting readings**
- **Monitor your engine actively during testing**

---

## Build Platforms

### Platform Selection

```bash
pio run -e teensy41      # Teensy 4.1 with built-in SD (recommended)
pio run -e teensy40      # Teensy 4.0
pio run -e teensy36      # Teensy 3.6
pio run -e mega2560      # Arduino Mega 2560
pio run -e esp32s3dev    # ESP32-S3
```

### Platform Comparison

| Platform | Flash | RAM | Features | Notes |
|----------|-------|-----|----------|-------|
| **Teensy 4.1** | 8MB | 512KB | All | **Recommended** - Built-in SD |
| Teensy 4.0 | 2MB | 512KB | All | External SD module |
| Teensy 3.6 | 1MB | 256KB | All | Older platform, 3.3V |
| Mega 2560 | 256KB | 8KB | Most | 5V, no native CAN |
| ESP32-S3 | 4MB | 512KB | Most | WiFi/BT capable, 3.3V |

**Recommended**: Teensy 4.1 provides best value with built-in SD card, ample resources, and native CAN support.

See [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md) for details.

---

## Quick Start

### 1. Connect and Build

```bash
git clone https://github.com/preobd/preOBD.git
cd preOBD
pio run -e teensy41 -t upload    # Build and upload for Teensy 4.1
pio device monitor               # 115200 baud
```

### 2. Configure Your Sensors

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

### 3. Operate

**MODE_BUTTON:**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Acknowledge the sounding alarm(s) (until that alarm clears by default; see `ALARM DURATION`)
- Hold for 2 seconds in RUN mode → Toggle the display on/off

---

## Common Sensor Configurations

### K-Type Thermocouple (CHT/EGT)

```
SET 7 EGT MAX31855                  # EGT on digital pin 7
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
OUTPUT CAN ENABLE                   # Enable CAN output
# Plug in ELM327 Bluetooth adapter to OBD-II port
# Open Torque app → Settings → OBD/ECU → Connection → Bluetooth
# Select your ELM327 adapter
# View real-time sensor data
```

**Supported apps**: Torque, OBD Fusion, Car Scanner, DashCommand

See [OBD-II Scanner Guide](../guides/outputs/OBD2_SCANNER_GUIDE.md) for details.

### RealDash Dashboard

```
OUTPUT RealDash ENABLE              # Enable RealDash CAN output
# Works simultaneously with OBD-II scanners (hybrid mode)
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

### System Commands

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
| `DISPLAY TYPE <LCD\|OLED\|NONE>` | Set display type |
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

## Application Types

| Application | Description | Suggested Sensors |
|-------------|-------------|-------------------|
| `CHT` | Cylinder Head Temperature | MAX6675, MAX31855 |
| `EGT` | Exhaust Gas Temperature | MAX31855 (high range) |
| `COOLANT_TEMP` | Engine Coolant | VDO_120C_*, GENERIC_NTC_* |
| `OIL_TEMP` | Engine Oil | VDO_150C_*, GENERIC_NTC_* |
| `TCASE_TEMP` | Transfer Case | VDO_150C_*, GENERIC_NTC_* |
| `OIL_PRESSURE` | Engine Oil Pressure | VDO_5BAR_CURVE, GENERIC_PRESSURE_150PSI |
| `BOOST_PRESSURE` | Boost/Manifold Pressure | GENERIC_BOOST, VDO_2BAR_CURVE |
| `FUEL_PRESSURE` | Fuel Pressure | VDO_5BAR_CURVE, GENERIC_PRESSURE_150PSI |
| `PRIMARY_BATTERY` | Main Battery | VOLTAGE_DIVIDER |
| `AUXILIARY_BATTERY` | Secondary Battery | VOLTAGE_DIVIDER |
| `COOLANT_LEVEL` | Coolant Level Switch | FLOAT_SWITCH |
| `AMBIENT_TEMP` | Ambient Temperature | BME280_TEMP |
| `BAROMETRIC_PRESSURE` | Barometric Pressure | BME280_PRESSURE |
| `HUMIDITY` | Relative Humidity | BME280_HUMIDITY |
| `ELEVATION` | Estimated Altitude | BME280_ELEVATION |
| `ENGINE_RPM` | Engine RPM | W_PHASE_RPM |
| `VEHICLE_SPEED` | Vehicle Speed | HALL_SPEED |

This is the common subset — `LIST APPLICATIONS` shows all of them (fuel level, throttle position, transmission and differential temps, and more).

---

## Sensor Types

### Thermocouples
- `MAX6675` - K-Type thermocouple (0-1024°C)
- `MAX31855` - K-Type thermocouple (high range, -270 to 1372°C)

### VDO Thermistors
- `VDO_120C_TABLE` - VDO 120°C (lookup table, most accurate)
- `VDO_120C_STEINHART` - VDO 120°C (Steinhart-Hart, faster)
- `VDO_150C_TABLE` - VDO 150°C (table)
- `VDO_150C_STEINHART` - VDO 150°C (Steinhart-Hart)
- `VDO_200C_TABLE` - VDO 200°C cylinder-head sender

### Other Resistive Temperature Senders
- `SMITHS_TT6811_TEMP`, `SMITHS_TT4201_TEMP`, `SMITHS_GTR104_TEMP` - Smiths coolant senders
- `ACDELCO_TEMP_PRE79`, `ACDELCO_TEMP_POST79` - AC Delco GM coolant senders
- `BOSCH_NTC_M12` - Bosch M12 EFI NTC sensor
- `JEEP_4_0_TEMP_GAUGE`, `JEEP_CJ_TEMP_GAUGE`, `JEEP_RENIX_CTS` - Jeep senders

### Generic NTC Thermistors
- `GENERIC_NTC_BETA` - Any NTC; set coefficients with `SET <pin> CAL BETA`
- `GENERIC_NTC_STEINHART` - Any NTC; set coefficients with `SET <pin> CAL STEINHART`
- `GENERIC_NTC_TABLE` - Any NTC with a custom lookup table (JSON import)

### Linear Temperature Sensors
- `GENERIC_TEMP_LINEAR` - Linear 0.5-4.5V, -40 to 150°C

### VDO Pressure Sensors (Resistive)
- `VDO_2BAR_CURVE` - VDO 0-2 bar
- `VDO_5BAR_CURVE` - VDO 0-5 bar
- `VDO_10BAR_TABLE` / `VDO_10BAR_CURVE` - VDO 0-10 bar
- `VDO_25BAR_TABLE` - VDO 0-25 bar
- `VDO_80PSI_TABLE` - VDO 0-80 PSI (US)
- `SMITHS_OIL_PRESSURE_BP`, `SW_OIL_PRESSURE`, `JEEP_4_0_OIL_GAUGE`, `JEEP_CJ_OIL_THIN`, `JEEP_CJ_OIL_DEEP` - Classic gauge senders (0-80 PSI)

### Linear Pressure Sensors (0.5-4.5V)
- `GENERIC_BOOST` - Generic 0-5 bar boost
- `GENERIC_PRESSURE_150PSI` - Generic 0-150 PSI
- `AEM_30_2130_150` - AEM 0-150 PSI
- `MPX4250AP` - Freescale/NXP MAP sensor (20-250 kPa)
- `MPX5700AP` - Freescale/NXP MAP sensor (15-700 kPa)
- Any other range: pick a linear preset and set it with `SET <pin> CAL PRESSURE_LINEAR`

### Other
- `VOLTAGE_DIVIDER` - 12V battery monitoring
- `W_PHASE_RPM` - Alternator RPM
- `HALL_SPEED` - Hall effect speed sensor (VDO, OEM, generic 3-wire)
- `FLOAT_SWITCH` - Digital level switch
- `VDO_FUEL_LEVEL_*`, `SW_FUEL_LEVEL_240`, `SW_FORD_FUEL_LEVEL`, `JEEP_CJ_FUEL_LEVEL` - Fuel level senders
- `GENERIC_TPS` - 0.5-4.5V throttle position
- `BME280_TEMP`, `BME280_PRESSURE`, `BME280_HUMIDITY`, `BME280_ELEVATION`

### Custom Calibration Commands
```
SET <pin> CAL RPM <poles> <ratio> [<mult>] <timeout> <min> <max>     # Custom RPM
SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> [<mult>] <timeout> <max>  # Custom speed
SET <pin> CAL STEINHART <bias> <a> <b> <c>           # Custom Steinhart-Hart
SET <pin> CAL BETA <bias> <beta> <r0> <t0>           # Custom Beta equation
SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>  # Custom linear range
SET <pin> CAL BIAS <resistor>                         # Override bias resistor
```

---

## Platform Quick Reference

| Board | ADC | Voltage | Max Inputs | Notes |
|-------|-----|---------|------------|-------|
| Teensy 4.0/4.1 | 12-bit | 3.3V | 40 | Best performance, native CAN |
| Teensy 3.6 | 12-bit | 3.3V | 32 | Older platform, native CAN |
| Arduino Mega | 10-bit | 5V | 8 | Tight on flash and RAM; MCP2515 for CAN |
| ESP32-S3 | 12-bit | 3.3V | 32 | Native TWAI CAN |
| Arduino Uno | 10-bit | 5V | 8 | Not supported — too little RAM for runtime CLI |

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

### Sensor reads NaN or ERR

- Check wiring
- Verify bias resistor is installed (VDO sensors)
- Confirm correct sensor type is configured

### Alarm triggers at startup

- Normal behavior - each application has a warmup time before its alarm arms (see `RULE <id>`)
- Warmup can be changed per Rule through a JSON import (`warmupMs`)

### Can't change configuration

- Type `CONFIG` to enter configuration mode
- Configuration commands only work in CONFIG mode

### LCD doesn't display

- Check I2C address (try 0x27 and 0x3F)
- Verify I2C wiring (SDA, SCL)
- Check `DISPLAY STATUS` to ensure the display is enabled and the type/address are right

### CAN not working

- Verify CAN pin assignments in your board definition (`boards/<board>/board.h` — SPI-CAN `CAN_CS_*`/`CAN_INT_*`)
- Check 120Ω termination resistors
- Ensure baud rate matches receiver

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

