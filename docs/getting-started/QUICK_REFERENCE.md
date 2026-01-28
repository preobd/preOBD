# openEMS Quick Reference

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
pio run -e mega2560      # Arduino Mega 2560
pio run -e uno_static    # Arduino Uno (static config)
```

### Platform Comparison

| Platform | Flash | RAM | Features | Notes |
|----------|-------|-----|----------|-------|
| **Teensy 4.1** | 8MB | 512KB | All | **Recommended** - Built-in SD |
| Teensy 4.0 | 2MB | 512KB | All | External SD module |
| Teensy 3.6 | 1MB | 256KB | All | Older platform |
| Mega 2560 | 256KB | 8KB | All | Good for prototyping |
| Uno | 32KB | 2KB | Minimal | Static config only |

**Recommended**: Teensy 4.1 provides best value with built-in SD card, ample resources, and native CAN support.

See [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md) for details.

---

## Quick Start

### 1. Connect and Build

```bash
git clone https://github.com/preobd/openEMS.git
cd openEMS
pio run -e teensy41 -t upload    # Build and upload for Teensy 4.1
pio device monitor               # 115200 baud
```

### 2. Configure Your Sensors

```
CONFIG                                    # Enter configuration mode
SET 7 EGT MAX31855                       # Pin 7: EGT with thermocouple
SET A0 OIL_TEMP VDO_150C_STEINHART        # Pin A0: VDO 150C using Steinhart Calibration
SET A2 COOLANT_TEMP VDO_120C_TABLE      # Pin A2: VDO coolant sensor using Lookup table
SET A3 OIL_PRESSURE GENERIC_150PSI       # Pin A3: Generic 0.5-4.5V pressure
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER   # Pin A6: Battery voltage
SAVE                                      # Save configuration
RUN                                       # Start monitoring
```

Use `LIST SENSORS` to see sensor categories, then `LIST SENSORS <category>` for specific sensors.

### 3. Operate

**MODE_BUTTON:**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Silence alarms for 30 seconds

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
MAX31855 SCK → Pin 13 (SPI)
MAX31855 SO  → Pin 12 (SPI MISO)
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
Add bias resistor: Pin → 1kΩ → 3.3V/5V
```

### Generic NTC Thermistor

For non-VDO NTC thermistors (common 10K sensors):

```
SET A0 OIL_TEMP NTC_10K_BETA_3950       # Most common generic NTC
SET A1 COOLANT_TEMP NTC_10K_STEINHART   # Alternative method
SET A2 COOLANT_TEMP NTC_STEINHART       # Then set coefficients
SET A2 STEINHART 10000 1.129e-3 2.341e-4 8.775e-8
```

**Wiring:**
```
NTC Thermistor → Between analog pin and GND
Bias resistor  → Between analog pin and 3.3V/5V
(Typically 10kΩ bias for 10K NTC)
```

### VDO Pressure Sensor

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE          # Oil pressure
SET A7 BOOST_PRESSURE VDO_2BAR_CURVE        # Boost pressure
SET A8 FUEL_PRESSURE VDO_10BAR        # Fuel pressure
```

**Wiring:** Same as temperature sensor (signal + ground + 1kΩ bias)

### Generic Linear Pressure Sensor (0.5-4.5V)

For modern 3-wire pressure sensors with linear voltage output:

```
SET A3 OIL_PRESSURE GENERIC_150PSI    # 150 PSI oil pressure
SET A4 BOOST_PRESSURE GENERIC_BOOST   # 0-5 bar boost
SET A5 BOOST_PRESSURE MPX4250AP       # Freescale MAP sensor
```

**Custom range (any 0.5-4.5V sensor):**
```
SET A3 OIL_PRESSURE GENERIC_LINEAR
SET A3 PRESSURE_LINEAR 0.5 4.5 0.0 7.0   # 0.5-4.5V maps to 0-7 bar
```

**Wiring:**
```
Sensor VCC    → 5V regulated
Sensor GND    → GND
Sensor Signal → Analog pin
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
SET 2 SPEED 100 2008 3.73 2000 300
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
| `HELP <category>` | Show detailed help (LIST, SET, CALIBRATION, CONTROL, OUTPUT, RELAY, DISPLAY, TRANSPORT, SYSTEM, CONFIG) |
| `HELP QUICK` | Show compact command reference |
| `?` | Alias for HELP |

**Examples:**
```
HELP                    # Show all categories
HELP SET                # Show all SET commands
HELP CALIBRATION        # Show calibration commands
```

### Configuration Commands (CONFIG mode only)

| Command | Description | Example |
|---------|-------------|---------|
| `SET <pin> <app> <sensor>` | Configure input | `SET A2 COOLANT_TEMP VDO_120C_TABLE` |
| `SET <pin> ALARM <min> <max>` | Set alarm thresholds | `SET A2 ALARM 60 120` |
| `SET <pin> UNITS <unit>` | Set display units | `SET A2 UNITS FAHRENHEIT` |
| `SET <pin> NAME <n>` | Set short name | `SET A2 NAME CLT` |
| `CLEAR <pin>` | Remove input | `CLEAR A2` |
| `ENABLE <pin>` | Enable input | `ENABLE A2` |
| `DISABLE <pin>` | Disable input | `DISABLE A2` |
| `SAVE` | Save to EEPROM | `SAVE` |

### Query Commands (any mode)

| Command | Description |
|---------|-------------|
| `LIST INPUTS` | Show all configured inputs |
| `LIST APPLICATIONS` | Show available applications |
| `LIST SENSORS` | Show sensor categories |
| `LIST SENSORS <category>` | Show sensors in category |
| `INFO <pin>` | Show input details |
| `DUMP` | Show complete configuration |
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

### Relay Commands (requires ENABLE_RELAY_OUTPUT)

| Command | Description |
|---------|-------------|
| `RELAY LIST` | Show all relay status |
| `RELAY <0-1> PIN <pin>` | Set relay output pin |
| `RELAY <0-1> INPUT <pin>` | Link to sensor input |
| `RELAY <0-1> THRESHOLD <on> <off>` | Set ON/OFF thresholds |
| `RELAY <0-1> MODE <mode>` | AUTO_HIGH, AUTO_LOW, ON, OFF |

**Example - Cooling fan:**
```
RELAY 0 PIN 23              # Relay on pin 23
RELAY 0 INPUT A2            # Monitor coolant temp
RELAY 0 THRESHOLD 90 85     # ON at 90°C, OFF at 85°C
RELAY 0 MODE AUTO_HIGH      # Activate on high temp
```

See [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md) for details.

### Bus Commands

| Command | Description |
|---------|-------------|
| `BUS` | Show all bus configurations |
| `BUS I2C [0\|1\|2]` | Show or select I2C bus (Wire/Wire1/Wire2) |
| `BUS I2C CLOCK <kHz>` | Set I2C clock (100, 400, 1000) |
| `BUS SPI [0\|1\|2]` | Show or select SPI bus (SPI/SPI1/SPI2) |
| `BUS SPI CLOCK <Hz>` | Set SPI clock speed |
| `BUS CAN [0\|1\|2]` | Show or select CAN bus (CAN1/CAN2/CAN3) |
| `BUS CAN BAUDRATE <bps>` | Set CAN baudrate |

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
| `COOLANT_TEMP` | Engine Coolant | VDO_120C_*, NTC_10K_* |
| `OIL_TEMP` | Engine Oil | VDO_150C_*, NTC_10K_* |
| `TCASE_TEMP` | Transfer Case | VDO_150C_*, NTC_10K_* |
| `OIL_PRESSURE` | Engine Oil Pressure | VDO_5BAR_CURVE, GENERIC_150PSI |
| `BOOST_PRESSURE` | Boost/Manifold Pressure | GENERIC_BOOST, VDO_2BAR_CURVE |
| `FUEL_PRESSURE` | Fuel Pressure | VDO_5BAR_CURVE, GENERIC_150PSI |
| `PRIMARY_BATTERY` | Main Battery | VOLTAGE_DIVIDER |
| `AUXILIARY_BATTERY` | Secondary Battery | VOLTAGE_DIVIDER |
| `COOLANT_LEVEL` | Coolant Level Switch | FLOAT_SWITCH |
| `AMBIENT_TEMP` | Ambient Temperature | BME280_TEMP |
| `BAROMETRIC_PRESSURE` | Barometric Pressure | BME280_PRESSURE |
| `HUMIDITY` | Relative Humidity | BME280_HUMIDITY |
| `ELEVATION` | Estimated Altitude | BME280_ELEVATION |
| `ENGINE_RPM` | Engine RPM | W_PHASE_RPM |
| `VEHICLE_SPEED` | Vehicle Speed | HALL_SPEED |

---

## Sensor Types

### Thermocouples
- `MAX6675` - K-Type thermocouple (0-1024°C)
- `MAX31855` - K-Type thermocouple (high range, -200 to 1350°C)

### VDO Thermistors
- `VDO_120C_TABLE` - VDO 120°C (lookup table, most accurate)
- `VDO_120C_STEINHART` - VDO 120°C (Steinhart-Hart, faster)
- `VDO_150C_TABLE` - VDO 150°C (table)
- `VDO_150C_STEINHART` - VDO 150°C (Steinhart-Hart)

### Generic NTC Thermistors
- `NTC_10K_BETA_3950` - 10K NTC, β=3950 (most common)
- `NTC_10K_BETA_3435` - 10K NTC, β=3435
- `NTC_10K_STEINHART` - 10K NTC, Steinhart-Hart
- `NTC_STEINHART` - Generic, set coefficients with `SET <pin> STEINHART`
- `NTC_BETA` - Generic, set coefficients with `SET <pin> BETA`

### Linear Temperature Sensors
- `LINEAR_TEMP_40_150` - Linear 0.5-4.5V, -40 to 150°C

### VDO Pressure Sensors (Resistive)
- `VDO_2BAR_CURVE` - VDO 0-2 bar
- `VDO_5BAR_CURVE` - VDO 0-5 bar
- `VDO_10BAR` - VDO 0-10 bar

### Linear Pressure Sensors (0.5-4.5V)
- `GENERIC_BOOST` - Generic 0-5 bar boost
- `GENERIC_150PSI` - Generic 0-150 PSI
- `MPX4250AP` - Freescale/NXP MAP sensor (20-250 kPa)
- `MPX5700AP` - Freescale/NXP MAP sensor (15-700 kPa)
- `GENERIC_LINEAR` - Custom, set range with `SET <pin> PRESSURE_LINEAR`

### Other
- `VOLTAGE_DIVIDER` - 12V battery monitoring
- `W_PHASE_RPM` - Alternator RPM
- `HALL_SPEED` - Hall effect speed sensor (VDO, OEM, generic 3-wire)
- `FLOAT_SWITCH` - Digital level switch
- `BME280_TEMP`, `BME280_PRESSURE`, `BME280_HUMIDITY`, `BME280_ELEVATION`

### Custom Calibration Commands
```
SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>     # Custom RPM
SET <pin> SPEED <ppr> <tire_circ> <ratio> [<mult>] <timeout> <max>  # Custom speed
SET <pin> STEINHART <bias> <a> <b> <c>           # Custom Steinhart-Hart
SET <pin> BETA <bias> <beta> <r0> <t0>           # Custom Beta equation
SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>  # Custom linear range
SET <pin> BIAS <resistor>                         # Override bias resistor
```

---

## Platform Quick Reference

| Board | ADC | Voltage | Max Inputs | Notes |
|-------|-----|---------|------------|-------|
| Teensy 4.0/4.1 | 12-bit | 3.3V | 16 | Best performance, native CAN |
| Arduino Mega | 10-bit | 5V | 16 | Good all-rounder |
| Arduino Due | 12-bit | 3.3V | 16 | High resolution ADC |
| ESP32 | 12-bit | 3.3V | 16 | WiFi capable |
| Arduino Uno | 10-bit | 5V | 8 | Limited RAM, use [static builds](../advanced/STATIC_BUILDS_GUIDE.md) |

---

## Common Mistakes

1. **Wrong sensor type** - VDO 120C vs 150C makes huge difference
2. **Missing pull-down resistor** - VDO thermistors need pull-down (typically 1kΩ)
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

- Normal behavior - sensors need warmup time
- Increase warmup if needed: `ALARM <pin> WARMUP 60000`

### Can't change configuration

- Type `CONFIG` to enter configuration mode
- Configuration commands only work in CONFIG mode

### LCD doesn't display

- Check I2C address (try 0x27 and 0x3F)
- Verify I2C wiring (SDA, SCL)
- Check `OUTPUT LIST` to ensure LCD is enabled

### CAN not working

- Verify CAN_CS and CAN_INT pins in config.h
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
- Your configuration (`DUMP` command)
- What you expected vs. what happened

