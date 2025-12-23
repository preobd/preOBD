# Serial Commands Reference

**Complete reference for openEMS serial configuration commands**

---

## Overview

openEMS is configured via serial commands at 115200 baud. Connect to your board's serial port using the Arduino Serial Monitor, PuTTY, or `pio device monitor`.

Commands are case-insensitive. Pin names can be `A0`, `a0`, `6`, etc.

---

## Quick Start

```
CONFIG                           # Enter configuration mode
SET 6 CHT MAX6675               # Configure CHT with thermocouple
SET A2 COOLANT_TEMP VDO_120C_LOOKUP  # Configure coolant sensor
SET A3 OIL_PRESSURE VDO_5BAR    # Configure oil pressure
SAVE                             # Save to EEPROM
RUN                              # Start monitoring
```

---

## Table of Contents

1. [Input Configuration](#input-configuration)
2. [Custom Calibration](#custom-calibration)
3. [Alarm Configuration](#alarm-configuration)
4. [Output Configuration](#output-configuration)
5. [Display Configuration](#display-configuration)
6. [System Configuration](#system-configuration)
7. [Mode Commands](#mode-commands)
8. [Persistence Commands](#persistence-commands)
9. [Query Commands](#query-commands)
10. [Quick Reference Examples](#quick-reference-examples)

---

## Input Configuration

### Combined Sensor Command (Recommended)

Configure application and sensor in one command:

```
SET <pin> <application> <sensor>
```

**Examples:**
```
SET 6 CHT MAX6675                    # CHT with K-type thermocouple
SET A0 OIL_TEMP VDO_150C_STEINHART   # Oil temp with VDO sensor
SET A2 COOLANT_TEMP NTC_10K_BETA_3950  # Coolant with generic NTC
SET A3 OIL_PRESSURE GENERIC_150PSI   # Oil pressure with linear sensor
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER  # Battery voltage
```

### Individual Commands

```
SET <pin> APPLICATION <app>      # Set measurement type
SET <pin> SENSOR <sensor>        # Set hardware sensor
SET <pin> NAME <name>            # Set short display name (8 chars max)
SET <pin> DISPLAY_NAME <name>    # Set full display name (32 chars max)
SET <pin> UNITS <units>          # Override display units
SET <pin> ALARM <min> <max>      # Set alarm thresholds
SET <pin> ALARM OFF              # Disable alarm
```

### Input Control

```
ENABLE <pin>                     # Enable input reading
DISABLE <pin>                    # Disable input (keeps config)
CLEAR <pin>                      # Remove input configuration completely
```

### Available Units

| Unit | Aliases | Description |
|------|---------|-------------|
| `CELSIUS` | `C` | Temperature in Celsius |
| `FAHRENHEIT` | `F` | Temperature in Fahrenheit |
| `BAR` | | Pressure in bar |
| `PSI` | | Pressure in PSI |
| `KPA` | | Pressure in kilopascals |
| `VOLTS` | `V` | Voltage |
| `RPM` | | Revolutions per minute |
| `PERCENT` | `%` | Percentage |
| `METERS` | `M` | Altitude in meters |
| `FEET` | `FT` | Altitude in feet |

---

## Custom Calibration

Override sensor preset calibration with custom values.

### Calibration Commands

```
SET <pin> CALIBRATION PRESET     # Revert to preset calibration
SET <pin> BIAS <resistor>        # Set bias resistor (Ω)
SET <pin> STEINHART <bias> <a> <b> <c>  # Steinhart-Hart thermistor
SET <pin> BETA <bias> <beta> <r0> <t0>  # Beta equation thermistor
SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>  # Linear pressure
SET <pin> PRESSURE_POLY <bias> <a> <b> <c>  # Polynomial pressure (VDO)
SET <pin> RPM <poles> <ratio> <timeout> <min> <max>  # RPM (5 params)
SET <pin> RPM <poles> <ratio> <mult> <timeout> <min> <max>  # RPM (6 params)
INFO <pin> CALIBRATION           # View active calibration
```

### Thermistor Calibration Examples

**Steinhart-Hart (when you have A, B, C coefficients):**
```
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 STEINHART 10000 1.129e-3 2.341e-4 8.775e-8
SAVE
```

**Beta equation (when you have β value from datasheet):**
```
SET A0 OIL_TEMP THERMISTOR_BETA
SET A0 BETA 10000 3950 10000 25
SAVE
```

### Pressure Calibration Examples

**Linear 0.5-4.5V sensor, 0-7 bar:**
```
SET A1 BOOST_PRESSURE GENERIC_LINEAR
SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 7.0
SAVE
```

**VDO polynomial:**
```
SET A2 OIL_PRESSURE VDO_5BAR
SET A2 PRESSURE_POLY 1000 -0.3682 36.465 10.648
SAVE
```

### RPM Calibration Example

**12-pole alternator, 3:1 pulley ratio, fine-tuned +2%:**
```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 RPM 12 3.0 1.02 2000 100 8000
SAVE
```

See **[Advanced Calibration Guide](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** for detailed calibration instructions.

---

## Alarm Configuration

Configure per-input alarm thresholds and timing.

### Alarm Commands

```
SET <pin> ALARM <min> <max>      # Set alarm thresholds
SET <pin> ALARM OFF              # Disable alarm for input
ALARM <pin> WARMUP <ms>          # Override warmup time
ALARM <pin> PERSIST <ms>         # Override persistence time
INFO <pin> ALARM                 # Show alarm status
```

### Output Module Control

```
OUTPUT Alarm ENABLE              # Enable alarm hardware (buzzer, LED)
OUTPUT Alarm DISABLE             # Disable alarm hardware (silent mode)
OUTPUT Alarm INTERVAL <ms>       # Set alarm check interval (10-10000ms)
```

### Examples

```
# Set coolant alarm: trigger if <60°C or >120°C
SET A2 ALARM 60 120

# Custom warmup for oil pressure (45 seconds)
ALARM A1 WARMUP 45000

# Faster alarm response (1 second persistence)
ALARM 6 PERSIST 1000

# Check alarm status
INFO A2 ALARM
```

### Alarm State Machine

Each input tracks alarm state: `DISABLED → INIT → WARMUP → READY ↔ ACTIVE`

| State | Description |
|-------|-------------|
| DISABLED | Alarm off for this input |
| INIT | 1-second initialization after alarm enabled |
| WARMUP | Sensor-specific warmup (0-60s depending on application) |
| READY | Normal monitoring, alarm checking active |
| ACTIVE | Alarm condition met, buzzer sounding |

See **[Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md)** for complete documentation.

---

## Output Configuration

Control output modules at runtime.

### Commands

```
OUTPUT LIST                      # Show all output modules with status
OUTPUT <name> ENABLE             # Enable output module
OUTPUT <name> DISABLE            # Disable output module
OUTPUT <name> INTERVAL <ms>      # Set send interval (10-60000ms)
```

### Available Outputs

| Name | Description |
|------|-------------|
| `CAN` | CAN bus output (OBDII PIDs) |
| `RealDash` | RealDash CAN frames |
| `Serial` | CSV serial output |
| `SD_Log` | SD card data logging |
| `Alarm` | Alarm system (buzzer, LED) |

### Examples

```
# Enable CAN at 100ms interval
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100

# Enable RealDash at 50ms for smooth gauges
OUTPUT RealDash ENABLE
OUTPUT RealDash INTERVAL 50

# Disable SD logging
OUTPUT SD_Log DISABLE

# Check status
OUTPUT LIST
```

### Output Status Display

```
OUTPUT LIST
```
Shows:
```
=== Output Modules ===
CAN: Enabled, Interval: 100ms
RealDash: Enabled, Interval: 50ms
Serial: Disabled
SD_Log: Disabled
Alarm: Enabled, Interval: 500ms
```

---

## Display Configuration

Configure display settings.

### Commands

```
DISPLAY STATUS                   # Show current configuration
DISPLAY ENABLE                   # Enable display
DISPLAY DISABLE                  # Disable display
DISPLAY TYPE <LCD|OLED|NONE>     # Set display type
DISPLAY LCD ADDRESS <hex>        # Set I2C address (e.g., 0x27, 0x3F)
DISPLAY UNITS TEMP <C|F>         # Default temperature units
DISPLAY UNITS PRESSURE <BAR|PSI|KPA>  # Default pressure units
DISPLAY UNITS ELEVATION <M|FT>   # Default elevation units
```

### Examples

```
# Switch to OLED display
DISPLAY TYPE OLED
SAVE
RELOAD  # Reboot to reinitialize display

# Use Fahrenheit and PSI
DISPLAY UNITS TEMP F
DISPLAY UNITS PRESSURE PSI
SAVE

# Change LCD I2C address
DISPLAY LCD ADDRESS 0x3F
SAVE
```

---

## System Configuration

Advanced system parameters.

### Commands

```
SYSTEM STATUS                    # Show all system configuration
SYSTEM SEA_LEVEL <hPa>           # Sea level pressure (800-1200)
SYSTEM INTERVAL SENSOR <ms>      # Sensor read interval (10-10000)
SYSTEM INTERVAL ALARM <ms>       # Alarm check interval (10-10000)
SYSTEM INTERVAL LCD <ms>         # LCD update interval (10-10000)
```

### Examples

```
# Set sea level pressure for accurate altitude
SYSTEM SEA_LEVEL 1013.25

# Read sensors faster
SYSTEM INTERVAL SENSOR 50

# Check alarms more frequently
SYSTEM INTERVAL ALARM 100
```

---

## Mode Commands

Switch between configuration and run modes.

### Commands

```
CONFIG                           # Enter configuration mode
RUN                              # Enter run mode
RELOAD                           # Trigger system reboot
```

### Mode Behavior

| Mode | Description |
|------|-------------|
| **CONFIG** | All commands enabled. Sensors continue reading but outputs may be paused. Use for setup. |
| **RUN** | Configuration locked. Normal operation. Only read-only commands work. |

### Read-Only Commands (work in RUN mode)

- `HELP`, `?`
- `VERSION`
- `DUMP`
- `INFO <pin>`
- `LIST *`
- `OUTPUT LIST`
- `DISPLAY STATUS`
- `SYSTEM STATUS`

---

## Persistence Commands

Save and load configuration.

### Commands

```
SAVE                             # Save configuration to EEPROM
LOAD                             # Load configuration from EEPROM
RESET                            # Show reset warning
RESET CONFIRM                    # Reset all configuration to defaults
```

### SD Card Backup/Restore

```
CONFIG SAVE [filename]           # Save to SD card (optional filename)
CONFIG LOAD <filename>           # Load from SD card
```

### What Gets Saved

- All input configurations (pins, applications, sensors, names, alarms, calibrations)
- Output module enable/disable states and intervals
- Display type and unit preferences
- System parameters

### Best Practices

- Save after completing all configuration changes (not after each command)
- EEPROM has ~100,000 write cycles - avoid excessive saves
- Use SD card backup for archival

---

## Query Commands

View system status and configuration.

### Commands

```
HELP                             # Show command list
VERSION                          # Show firmware version
DUMP                             # Show complete system state
DUMP JSON                        # Export configuration as JSON
LIST INPUTS                      # Show all configured inputs
LIST APPLICATIONS                # Show available application types
LIST SENSORS                     # Show available sensor types
INFO <pin>                       # Show detailed input info
INFO <pin> CALIBRATION           # Show calibration details
INFO <pin> ALARM                 # Show alarm status
```

---

## Quick Reference Examples

### Complete Setup Workflow

```
CONFIG
SET 6 CHT MAX6675
SET A0 OIL_TEMP NTC_10K_BETA_3950
SET A2 COOLANT_TEMP VDO_120C_LOOKUP
SET A3 OIL_PRESSURE GENERIC_150PSI
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A2 ALARM 60 120
SET A3 ALARM 0.5 7.0
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100
DISPLAY UNITS TEMP F
SAVE
RUN
```

### Change Output Intervals

```
CONFIG
OUTPUT CAN INTERVAL 500          # Slow down CAN
OUTPUT RealDash INTERVAL 20      # Speed up RealDash
SAVE
RUN
```

### Add Custom Calibration

```
CONFIG
SET A0 OIL_TEMP THERMISTOR_STEINHART
SET A0 STEINHART 4700 1.129e-3 2.341e-4 8.775e-8
SAVE
RUN
```

---

## Pin Notation

| Type | Format | Examples |
|------|--------|----------|
| Digital | Number | `0`, `1`, `6`, `13` |
| Analog | A + Number | `A0`, `A1`, `A6`, `A15` |
| I2C | `I2C` | For BME280 and other I2C sensors |
| SPI CS | Number | Use any digital pin for chip select |

---

## Error Messages

If a command fails, you'll see helpful error messages:

```
ERROR: Unknown output 'CANBUS'
  Hint: Use 'OUTPUT LIST' to see available outputs

ERROR: Configuration locked in RUN mode
  Type CONFIG to enter configuration mode

ERROR: Pin A20 out of range
  Hint: Valid pins: 0-53, A0-A15
```

---

## Static Builds

These serial commands are available by default. For memory-constrained boards (Arduino Uno), you may use static builds where configuration is done at compile time via `tools/configure.py`.

In static builds:
- Serial configuration commands are disabled
- CONFIG/RUN mode switching is disabled
- Configuration is defined at compile time

See **[Static Builds Guide](../advanced/STATIC_BUILDS_GUIDE.md)** for details.

---

## See Also

- **[Quick Reference](../getting-started/QUICK_REFERENCE.md)** - Command cheat sheet
- **[Advanced Calibration Guide](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensor calibration
- **[Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md)** - Alarm state machine
- **[CONFIG/RUN Mode Guide](../guides/configuration/CONFIG_RUN_MODE_GUIDE.md)** - Configuration workflow

---

**For the classic car community.**
