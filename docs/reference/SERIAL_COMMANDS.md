# Serial Commands Reference

**Complete reference for openEMS serial configuration commands**

---

## Overview

openEMS is configured via serial commands at 115200 baud using an interactive command-line interface.

**Terminal Mode (Default):**
- Full-featured CLI with tab completion, command history, and arrow key navigation
- Requires a terminal emulator (screen, minicom, picocom, PuTTY, etc.)
- Build environments: `teensy41`, `mega2560`, `esp32dev` (with `ENABLE_TERMINAL_EMULATOR` flag)

**Serial Monitor Mode (Alternative):**
- Basic CLI compatible with Arduino/PlatformIO serial monitors
- No tab completion or command history
- Build environment: `teensy41_serial` (without `ENABLE_TERMINAL_EMULATOR` flag)

Commands are case-insensitive. Pin names can be `A0`, `a0`, `6`, etc.

### Quick Connection Guide

**macOS/Linux (Terminal Mode):**
```bash
screen /dev/tty.usbmodem* 115200   # macOS
screen /dev/ttyACM0 115200         # Linux
# Exit: Ctrl+A then K then Y
```

**Windows (Terminal Mode):**
- Use PuTTY, Tera Term, or similar terminal emulator
- Set to Serial, 115200 baud, with VT100 emulation

**Serial Monitor Mode:**
```bash
pio device monitor                  # PlatformIO
# Or use Arduino IDE Serial Monitor
```

---

## Quick Start

```
CONFIG                           # Enter configuration mode
SET 6 CHT MAX6675               # Configure CHT with thermocouple
SET A2 COOLANT_TEMP VDO_120C_TABLE  # Configure coolant sensor
SET A3 OIL_PRESSURE VDO_5BAR_CURVE    # Configure oil pressure
SAVE                             # Save to EEPROM
RUN                              # Start monitoring
```

---

## Command Syntax Patterns

openEMS commands follow consistent patterns based on what you're configuring:

### Discovery Commands
Pattern: `LIST <type>`

```
LIST INPUTS              # Show all configured inputs
LIST APPLICATIONS        # Show available application presets
LIST SENSORS             # Show sensor categories
LIST SENSORS <category>  # Show sensors in category (e.g., NTC_THERMISTOR)
LIST SENSORS TEMPERATURE # Show all temperature sensors
LIST OUTPUTS             # Show available output modules
LIST TRANSPORTS          # Show available transports
```

### Configuration Commands
Pattern: `SET <pin> <field> <value>`

```
SET A0 CHT MAX6675                       # Combined syntax (recommended)
SET A0 APPLICATION CHT                   # Set application type
SET A0 SENSOR NTC VDO_120C_TABLE        # Two-layer: category + preset
SET A0 SENSOR VDO_120C_TABLE            # Legacy: direct sensor name
SET A0 ALARM 100 900                     # Set alarm thresholds
SET A0 ALARM WARMUP 30000                # Set alarm warmup time
```

### Control Commands
Pattern: `<ACTION> <pin>`

```
ENABLE A0            # Enable input reading
DISABLE A0           # Disable input (keeps config)
CLEAR A0             # Remove input configuration
```

### Query Commands
Pattern: `INFO <pin> [subcommand]`

```
INFO A0              # Show complete configuration
INFO A0 ALARM        # Show alarm status
INFO A0 CALIBRATION  # Show calibration details
```

### Output Module Commands
Pattern: `OUTPUT <name> <action> [parameters]`

```
OUTPUT STATUS                # Show all outputs
OUTPUT CAN ENABLE            # Enable CAN output
OUTPUT CAN INTERVAL 100      # Set interval to 100ms
```

### System Commands
Pattern: `SYSTEM <action> [parameters]`

```
SYSTEM STATUS                      # Show global config
SYSTEM DUMP                        # Complete dump
SYSTEM UNITS TEMP F                # Set default units
SYSTEM INTERVAL SENSOR 100         # Set sensor interval
SYSTEM REBOOT                      # Restart the device
```

### Persistence Commands

```
SAVE                        # Save to EEPROM
SAVE SD:config.json         # Save to SD card file
LOAD                        # Load from EEPROM
LOAD SD:backup.json         # Load from SD card file
```

---

## Understanding Settings Scope

openEMS has three levels of configuration:

### Per-Input Settings (use SET command)
Apply to individual sensor inputs:
- Application type, sensor hardware
- Display names and units override
- Alarm thresholds, warmup, persistence
- Custom calibration parameters

**Example:**
```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A2 ALARM 60 120
SET A2 ALARM WARMUP 30000
```

### Global System Settings (use SYSTEM command)
Apply to all inputs as defaults:
- Default units (temperature, pressure, elevation, speed)
- System timing intervals (sensor read, alarm check)
- Calibration constants (sea level pressure)

**Example:**
```
SYSTEM UNITS TEMP F              # All temps default to Fahrenheit
SYSTEM UNITS PRESSURE PSI        # All pressures default to PSI
SYSTEM INTERVAL SENSOR 100       # Read sensors every 100ms
```

### Output Module Settings (use OUTPUT command)
Control output modules independently:
- Enable/disable individual outputs
- Output-specific intervals
- Output-specific parameters

**Example:**
```
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100          # CAN at 100ms
OUTPUT RealDash INTERVAL 50      # RealDash at 50ms (faster)
```

**Note:** Individual inputs can override global defaults:
```
SYSTEM UNITS TEMP C              # Global default: Celsius
SET A2 UNITS F                   # A2 overrides to Fahrenheit
```

---

## Table of Contents

1. [Command Syntax Patterns](#command-syntax-patterns)
2. [Understanding Settings Scope](#understanding-settings-scope)
3. [Input Configuration](#input-configuration)
4. [Custom Calibration](#custom-calibration)
5. [Alarm Configuration](#alarm-configuration)
6. [Output Configuration](#output-configuration)
7. [Relay Control](#relay-control)
8. [Bus Configuration](#bus-configuration)
9. [Display Configuration](#display-configuration)
10. [System Configuration](#system-configuration)
11. [Mode Commands](#mode-commands)
12. [Persistence Commands](#persistence-commands)
13. [Query Commands](#query-commands)
14. [Quick Reference Examples](#quick-reference-examples)

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
SET 2 VEHICLE_SPEED HALL_SPEED       # Vehicle speed with Hall sensor
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
| `KPH` | | Speed in kilometers per hour |
| `MPH` | | Speed in miles per hour |
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
SET <pin> SPEED <ppr> <tire_circ> <ratio> <timeout> <max>  # Speed (5 params)
SET <pin> SPEED <ppr> <tire_circ> <ratio> <mult> <timeout> <max>  # Speed (6 params)
```

Use `INFO <pin> CALIBRATION` to view active calibration parameters (see [INFO Command](#info-command)).

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
SET A2 OIL_PRESSURE VDO_5BAR_CURVE
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

### Speed Calibration Example

**Hall effect sensor, 100 pulses/rev, 2008mm tire circumference, 3.73 final drive:**
```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 SPEED 100 2008 3.73 2000 300
SAVE
```

**Parameters:**
- `ppr` (100) - Pulses per revolution (gear tooth count)
- `tire_circ` (2008) - Tire rolling circumference in millimeters
- `ratio` (3.73) - Final drive ratio
- `mult` (optional) - Calibration multiplier (default: 1.0)
- `timeout` (2000) - Zero speed timeout in milliseconds
- `max` (300) - Maximum valid speed in kph

See **[Hall Speed Guide](../guides/sensor-types/HALL_SPEED_GUIDE.md)** and **[Advanced Calibration Guide](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** for detailed calibration instructions.

---

## Alarm Configuration

Configure per-input alarm thresholds and timing.

### Understanding Alarm Timing

**Warmup Time** - Delay after system startup before alarm can trigger
- **Purpose:** Prevents nuisance alarms during sensor stabilization
- **Common use case:** Oil pressure during engine start
- **Range:** 0-300000ms (5 minutes maximum)
- **Example:** Oil pressure needs 45 seconds to build up after engine starts

**Persistence Time** - How long a threshold violation must persist before triggering
- **Purpose:** Filters out transient spikes or noise
- **Common use case:** Temperature spikes from exhaust gas turbulence
- **Range:** 0-60000ms (60 seconds maximum)
- **Example:** Ignore brief CHT spikes lasting less than 2 seconds

### Alarm Commands

```
SET <pin> ALARM <min> <max>          # Set alarm thresholds
SET <pin> ALARM WARMUP <ms>          # Alarm warmup time (0-300000ms)
SET <pin> ALARM PERSIST <ms>         # Alarm persistence time (0-60000ms)
SET <pin> ALARM ENABLE               # Enable alarm checking
SET <pin> ALARM DISABLE              # Disable alarm (keeps thresholds)
```

Use `INFO <pin> ALARM` to view alarm status and configuration (see [INFO Command](#info-command)).

### Output Module Control (Global Alarm Hardware)

```
OUTPUT Alarm ENABLE              # Enable alarm hardware (buzzer, LED)
OUTPUT Alarm DISABLE             # Disable alarm hardware (silent mode)
OUTPUT Alarm INTERVAL <ms>       # Set alarm check interval (10-10000ms)
```

### Practical Examples

**Oil Pressure Alarm (with warmup):**
```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 ALARM 10 80                   # Alarm if <10 or >80 PSI
SET A3 ALARM WARMUP 45000            # Wait 45 seconds after startup
SET A3 ALARM PERSIST 3000            # Must persist 3 seconds to trigger
```
*Why:* Oil pressure needs time to build during startup. Brief drops during gear changes are normal.

**Coolant Temperature Alarm (with persistence):**
```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A2 ALARM 60 120                  # Alarm if <60°C or >120°C
SET A2 ALARM PERSIST 5000            # Must persist 5 seconds
```
*Why:* Brief temperature spikes can occur from exhaust gas turbulence. Only trigger if sustained.

**CHT Alarm (critical, fast response):**
```
SET 6 CHT MAX6675
SET 6 ALARM 100 900                  # Alarm if <100°F or >900°F
SET 6 ALARM PERSIST 1000             # Quick 1 second response
```
*Why:* Cylinder head temperature is critical - respond quickly to overtemp.

**Disable Alarm Temporarily:**
```
SET A3 ALARM DISABLE                 # Turn off oil pressure alarm
# Later...
SET A3 ALARM ENABLE                  # Re-enable with same thresholds
```

**Global Alarm Hardware Control:**
```
OUTPUT Alarm DISABLE                 # Silent mode (no buzzer)
# Alarms still check and log, but buzzer won't sound
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

### Output Module Availability

Output modules must be compiled into firmware to be available. Standard builds (`teensy41`, `teensy40`, `mega2560`) include all outputs by default.

**Check which outputs are available**:
```
LIST OUTPUTS           # List available output modules
OUTPUT STATUS          # Show current configuration (enabled/disabled + intervals)
```

**Runtime control** (for compiled-in outputs):
```
OUTPUT <name> ENABLE             # Enable output module
OUTPUT <name> DISABLE            # Disable output module
OUTPUT <name> INTERVAL <ms>      # Set send interval (10-60000ms)
```

**Note**: If an output is not listed by `LIST OUTPUTS`, it wasn't compiled into your build. See [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md) to create a custom environment with the outputs you need.

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
OUTPUT STATUS
```

### Output Status Display

```
OUTPUT STATUS
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

## Relay Control

**Note**: Relay functionality requires `ENABLE_RELAY_OUTPUT` to be defined in `config.h`.

Control automatic relay outputs based on sensor thresholds. Useful for cooling fans, warning lights, pumps, and other switched loads.

### Commands

```
RELAY LIST                           # Show all relay status
RELAY <0-1> STATUS                   # Show specific relay configuration
RELAY <0-1> PIN <pin>                # Set relay output pin
RELAY <0-1> INPUT <pin>              # Link relay to sensor input
RELAY <0-1> THRESHOLD <on> <off>     # Set ON/OFF thresholds with hysteresis
RELAY <0-1> MODE <mode>              # Set relay mode (see modes below)
RELAY <0-1> DISABLE                  # Disable relay
```

### Relay Modes

| Mode | Description |
|------|-------------|
| `AUTO_HIGH` | Relay ON when sensor rises above ON threshold, OFF when falls below OFF threshold |
| `AUTO_LOW` | Relay ON when sensor falls below ON threshold, OFF when rises above OFF threshold |
| `MANUAL_ON` | Manual override - always ON |
| `MANUAL_OFF` | Manual override - always OFF |
| `DISABLED` | Relay disabled (same as `RELAY <0-1> DISABLE` command) |

### Examples

**Cooling fan (AUTO_HIGH mode)**:
```
# Configure relay 0 for cooling fan control
RELAY 0 PIN 23                       # Relay connected to digital pin 23
RELAY 0 INPUT A2                     # Monitor coolant temperature on A2
RELAY 0 THRESHOLD 90 85              # Fan ON at 90°C, OFF at 85°C
RELAY 0 MODE AUTO_HIGH               # Activate on high temperature
```

**Low pressure warning (AUTO_LOW mode)**:
```
# Configure relay 1 for low oil pressure warning light
RELAY 1 PIN 22                       # Warning light on pin 22
RELAY 1 INPUT A3                     # Monitor oil pressure on A3
RELAY 1 THRESHOLD 15 20              # Light ON below 15 PSI, OFF above 20 PSI
RELAY 1 MODE AUTO_LOW                # Activate on low pressure
```

**Manual override**:
```
RELAY 0 MODE MANUAL_ON               # Force fan ON for testing
RELAY 0 MODE AUTO_HIGH               # Resume automatic control
```

### Relay Status Display

```
RELAY LIST
```
Shows:
```
=== Relay Status ===

Relay 0:
  Mode: AUTO_HIGH
  Output Pin: 23
  Input: A2 (Coolant Temp)
  Thresholds: ON=90.0°C, OFF=85.0°C
  Current Value: 82.3°C
  State: OFF

Relay 1:
  Mode: DISABLED
```

**For detailed relay configuration guide, see [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md)**.

---

## Bus Configuration

Select which I2C, SPI, CAN bus, or serial ports to use for sensors and outputs. This is useful when you want to use alternate bus pins (e.g., Wire1 instead of Wire on Teensy) or enable additional serial ports for communication.

### Commands

```
BUS                              # Show all bus configurations
BUS I2C                          # Show current I2C bus configuration
BUS I2C <0|1|2>                  # Select I2C bus (0=Wire, 1=Wire1, 2=Wire2)
BUS I2C CLOCK <kHz>              # Set I2C clock speed (100, 400, or 1000 kHz)
BUS SPI                          # Show current SPI bus configuration
BUS SPI <0|1|2>                  # Select SPI bus (0=SPI, 1=SPI1, 2=SPI2)
BUS SPI CLOCK <Hz>               # Set SPI clock speed in Hz
BUS CAN                          # Show current CAN bus configuration
BUS CAN <0|1|2>                  # Select CAN bus (0=CAN1, 1=CAN2, 2=CAN3)
BUS CAN BAUDRATE <bps>           # Set CAN baudrate (125000, 250000, 500000, 1000000)
BUS SERIAL                       # Show all serial port status
BUS SERIAL <1-8>                 # Show specific port status
BUS SERIAL <1-8> ENABLE [baud]   # Enable serial port with optional baud rate
BUS SERIAL <1-8> DISABLE         # Disable serial port
BUS SERIAL <1-8> BAUDRATE <rate> # Set serial port baud rate
```

### Platform Availability

| Platform | I2C Buses | SPI Buses | CAN Buses | Serial Ports |
|----------|-----------|-----------|-----------|--------------|
| Teensy 4.1 | Wire, Wire1, Wire2 | SPI, SPI1, SPI2 | CAN1, CAN2, CAN3 | Serial1-Serial8 |
| Teensy 4.0 | Wire, Wire1, Wire2 | SPI, SPI1, SPI2 | CAN1, CAN2, CAN3 | Serial1-Serial7 |
| Teensy 3.6 | Wire, Wire1, Wire2 | SPI, SPI1 | CAN1, CAN2 | Serial1-Serial6 |
| Teensy 3.5 | Wire, Wire1, Wire2 | SPI, SPI1 | CAN1 | Serial1-Serial6 |
| Teensy 3.1/3.2 | Wire, Wire1 | SPI | CAN1 | Serial1-Serial3 |
| ESP32 | Wire, Wire1 | SPI | CAN1 (TWAI) | Serial1-Serial2 |
| Arduino Mega | Wire | SPI | None | Serial1-Serial3 |

### Serial Port Baud Rates

Supported baud rates: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600

### Examples

**Switch to Wire1 for I2C sensors (Teensy 4.x):**
```
BUS I2C 1                        # Select Wire1 (pins 17/16 on Teensy 4.x)
SAVE                             # Save configuration
SYSTEM REBOOT                    # Reboot to apply
```

**Configure CAN bus and baudrate:**
```
BUS CAN 0                        # Use CAN1
BUS CAN BAUDRATE 500000          # 500 kbps
SAVE
```

**Set I2C to fast mode:**
```
BUS I2C CLOCK 400                # 400 kHz (Fast mode)
SAVE
```

**Enable Serial5 for Bluetooth module:**
```
BUS SERIAL 5 ENABLE 115200       # Enable Serial5 at 115200 baud
SAVE
```

**Change Serial1 baud rate:**
```
BUS SERIAL 1 BAUDRATE 9600       # Set Serial1 to 9600 baud (for HC-05 module)
SAVE
```

**Disable unused serial port:**
```
BUS SERIAL 3 DISABLE             # Disable Serial3 to free pins
SAVE
```

### Bus Status Display

```
BUS
```
Shows:
```
=== I2C Bus Configuration ===
Active: Wire (SDA=18, SCL=19) @ 400kHz
Available buses: 0=Wire, 1=Wire1, 2=Wire2

=== SPI Bus Configuration ===
Active: SPI (MOSI=11, MISO=12, SCK=13) @ 4.0MHz
Available buses: 0=SPI, 1=SPI1, 2=SPI2

=== CAN Bus Configuration ===
Active: CAN1 (TX=22, RX=23) @ 500kbps
Available buses: 0=CAN1, 1=CAN2, 2=CAN3
```

### Serial Port Status Display

```
BUS SERIAL
```
Shows:
```
=== Serial Port Configuration ===
Platform supports Serial1-Serial8

  Serial1: ENABLED @ 115200 baud (RX=0, TX=1)
  Serial2: disabled (RX=7, TX=8)
  Serial3: disabled (RX=15, TX=14)
  Serial4: disabled (RX=16, TX=17)
  Serial5: ENABLED @ 9600 baud (RX=21, TX=20)
  Serial6: disabled (RX=25, TX=24)
  Serial7: disabled (RX=28, TX=29)
  Serial8: disabled (RX=34, TX=35)

Use BUS SERIAL <port> ENABLE [baudrate] to enable a port
Use BUS SERIAL <port> DISABLE to disable a port
```

### Notes

- Bus changes take effect on next reboot (use `SYSTEM REBOOT` after `SAVE`)
- All sensors of a type use the selected bus (no per-sensor bus assignment)
- The system will fall back to bus 0 if the selected bus fails to initialize
- Bus configuration is saved to EEPROM and included in JSON exports
- Serial ports can be assigned to TRANSPORT for message routing (see `TRANSPORT` command)
- Enabled serial ports register their pins with the pin registry to prevent conflicts

---

## Display Configuration

Configure display hardware and refresh rate.

**Note:** Unit preferences have moved to `SYSTEM UNITS`.

### Commands

```
DISPLAY STATUS                   # Show display hardware status
DISPLAY ENABLE                   # Enable display
DISPLAY DISABLE                  # Disable display
DISPLAY TYPE <LCD|OLED|NONE>     # Set display type
DISPLAY ADDRESS <hex>            # Set I2C address (LCD only)
DISPLAY INTERVAL <ms>            # Set display refresh rate
```

### DISPLAY STATUS Output

```
=== Display Configuration ===
Status: Enabled
Type: LCD
LCD I2C Address: 0x27
Update Interval: 1000ms
```

For unit configuration, use `SYSTEM UNITS` commands.

### Examples

```
# Switch to OLED display
DISPLAY TYPE OLED
SAVE
SYSTEM REBOOT  # Reboot to reinitialize display

# Configure display hardware
DISPLAY TYPE LCD
DISPLAY ADDRESS 0x27
DISPLAY INTERVAL 500    # Update display every 500ms

# View configuration
DISPLAY STATUS

# Change LCD I2C address
DISPLAY ADDRESS 0x3F
SAVE
```

---

## System Configuration

Global configuration affecting all subsystems.

### Query System

```bash
SYSTEM STATUS            # Show all global configuration
SYSTEM DUMP              # Show complete system dump (all subsystems)
SYSTEM DUMP JSON         # Export configuration as JSON (copy/paste)
```

**SYSTEM STATUS** output:
```
=== System Configuration ===
Sea Level Pressure: 1013.25 hPa
Global Intervals: Sensor=100ms, Alarm=100ms
Default Units: Temp=°C, Pressure=bar, Elevation=m, Speed=kph
```

**SYSTEM DUMP** shows complete configuration including all inputs, outputs, display, and system parameters.

**SYSTEM DUMP JSON** exports the complete configuration as JSON to the terminal for easy copy/paste.

### Configure Global Defaults

**Default Units** (inherited by all inputs unless overridden):
```bash
SYSTEM UNITS TEMP <C|F>                    # Temperature units
SYSTEM UNITS PRESSURE <BAR|PSI|KPA|INHG>   # Pressure units
SYSTEM UNITS ELEVATION <M|FT>              # Elevation units
SYSTEM UNITS SPEED <KPH|MPH>               # Speed units
```

**Calibration Constants:**
```bash
SYSTEM SEA_LEVEL <hPa>                     # Sea level pressure for altitude calculations
```

**Timing Intervals:**
```bash
SYSTEM INTERVAL <type> <ms>                # Set timing intervals
```

Interval types:
- `SENSOR` - How often to read sensor values (default: 100ms)
- `ALARM` - How often to check alarm thresholds (default: 100ms)

**Note:** Display refresh rate has moved to `DISPLAY INTERVAL`. Per-output intervals are configured with `OUTPUT <name> INTERVAL`.

### Examples

```bash
# Set global defaults
SYSTEM UNITS TEMP F
SYSTEM UNITS PRESSURE PSI
SYSTEM SEA_LEVEL 1013.25
SYSTEM INTERVAL SENSOR 50

# View configuration
SYSTEM STATUS
SYSTEM DUMP

# Export for sharing
SYSTEM DUMP JSON

# Read sensors faster
SYSTEM INTERVAL SENSOR 50

# Check alarms more frequently
SYSTEM INTERVAL ALARM 100
```

**System Control:**
```bash
SYSTEM REBOOT                                  # Restart the device
SYSTEM RESET CONFIRM                           # Factory reset (erase config + reboot)
```

**System Control Examples:**
```bash
# Simple reboot
SYSTEM REBOOT

# Factory reset (requires CONFIRM for safety)
SYSTEM RESET CONFIRM                           # Erases ALL config and reboots

```

**Comparison of Reset Commands:**

| Command | Clears Config? | Reboots? | Use Case |
|---------|---------------|----------|----------|
| `SYSTEM REBOOT` | No | Yes | Clean restart |
| `SYSTEM RESET CONFIRM` | Yes | Yes | Factory reset to defaults |

---

## Mode Commands

Switch between configuration and run modes.

### Commands

```
CONFIG                           # Enter configuration mode
RUN                              # Enter run mode
```

### Mode Behavior

| Mode | Description |
|------|-------------|
| **CONFIG** | All commands enabled. Sensors continue reading but outputs may be paused. Use for setup. |
| **RUN** | Configuration locked. Normal operation. Only read-only commands work. |

### Read-Only Commands (work in RUN mode)

- `HELP`, `?`
- `VERSION`
- `INFO <pin>`
- `LIST *`
- `OUTPUT STATUS`
- `TRANSPORT STATUS`
- `DISPLAY STATUS`
- `SYSTEM STATUS`
- `SYSTEM DUMP`
- `SYSTEM DUMP JSON`

---

## Persistence Commands

Save and load configuration to/from EEPROM or file storage.

### EEPROM Persistence

Internal persistent storage that survives reboots. Limited write cycles (~100,000).

```
SAVE                             # Save configuration to EEPROM
SAVE EEPROM                      # Save configuration to EEPROM (explicit)
LOAD                             # Load configuration from EEPROM
LOAD EEPROM                      # Load configuration from EEPROM (explicit)
```

**Note:** Use `SYSTEM RESET CONFIRM` to perform a factory reset (clears all configuration and reboots).

### File Storage (SD Card, USB, etc.)

The file storage system uses URI-style paths with optional destination prefixes:

```
SAVE [destination:]filename     # Save to file storage
LOAD [destination:]filename     # Load from file storage
```

**Destination Prefixes:**
- `SD:` - SD card storage (default if no prefix specified)
- `USB:` - USB drive storage (if `ENABLE_USB_STORAGE` defined)
- Additional destinations may be added in future releases

**Common File Storage Examples:**

```bash
# Quick save/load (defaults to SD card)
SAVE config.json                    # Saves to SD:/config/config.json
LOAD config.json                    # Loads from SD:/config/config.json

# Named configuration backups
SAVE SD:backup_20260111.json        # Timestamped backup
SAVE SD:summer_setup.json           # Summer configuration
SAVE SD:winter_setup.json           # Winter configuration

# Absolute paths for organization
SAVE SD:/backups/backup_20260111.json
LOAD SD:/backups/backup_20260111.json

# USB storage (if available)
SAVE USB:backup.json
LOAD USB:restore.json
```

**Workflow Example - Configuration Backup and Restore:**
```bash
# Save current configuration before making changes
SAVE SD:backup_before_changes.json

# Make configuration changes
SET A2 ALARM 65 115

# Test the changes
# If something goes wrong, restore from backup
LOAD SD:backup_before_changes.json
SAVE                                # Persist restored config to EEPROM
```

**Path Handling:**
- Relative paths (e.g., `config.json`) are auto-prefixed with `/config/`
- Absolute paths (e.g., `/data/config.json`) are used as-is
- URI prefix + path: `SD:/path/file.json` uses absolute path
- Maximum path length: 32 characters (including destination prefix)

**Error Handling:**
If a file operation fails, you'll see:
```
ERROR: Failed to save configuration
  Check: SD card inserted and formatted
  Check: Filename length < 32 characters
  Check: Destination prefix valid (SD, USB)
```

### What Gets Saved

- All input configurations (pins, applications, sensors, names, alarms, calibrations)
- Output module enable/disable states and intervals
- Display type and unit preferences
- System parameters (sea level pressure, intervals)

### Best Practices

- Save after completing all configuration changes (not after each command)
- EEPROM has ~100,000 write cycles - avoid excessive saves
- Use file storage (SD/USB) for archival and sharing configurations
- Create timestamped backups: `SAVE SD:backup_20260109.json`
- Test loaded configurations before persisting to EEPROM

---

## Query Commands

View system status and configuration.

### Help System

The help system is hierarchical and organized by category:

```
HELP                             # Show category overview
HELP <category>                  # Show detailed help for category
HELP QUICK                       # Show compact command reference
?                                # Alias for HELP
```

**Available Categories:**
- `HELP LIST` - Discovery commands (list inputs, applications, sensors)
- `HELP SET` - Basic configuration commands (application, sensor, names, units, alarms)
- `HELP CALIBRATION` - Advanced sensor calibration (RPM, speed, pressure, temperature)
- `HELP CONTROL` - Input control (enable, disable, clear, info)
- `HELP OUTPUT` - Output module configuration (CAN, RealDash, Serial, SD logging)
- `HELP DISPLAY` - Display settings (LCD/OLED, unit preferences)
- `HELP TRANSPORT` - Message routing (control, data, debug messages)
- `HELP SYSTEM` - System configuration (sea level, intervals)
- `HELP CONFIG` - Persistence and modes (save, load, reset, version, dump)

**Examples:**
```
HELP                            # Show all categories
HELP SET                        # Show all SET commands
HELP CALIBRATION                # Show calibration commands
HELP QUICK                      # Show compact reference
```

### INFO Command

Query detailed information about configured inputs.

```
INFO <pin>                       # Show complete input configuration and current value
INFO <pin> CALIBRATION           # Show calibration parameters and equations
INFO <pin> ALARM                 # Show alarm configuration and current status
```

**INFO <pin>** displays:
- Application type and sensor type
- Current sensor value (raw and converted)
- Display name and units
- Alarm thresholds (if configured)
- Pin assignment

**INFO <pin> CALIBRATION** displays:
- Active calibration method (Steinhart-Hart, Beta, lookup table, linear, etc.)
- Calibration coefficients or table data
- Conversion equations

**INFO <pin> ALARM** displays:
- Alarm thresholds (min/max)
- Current alarm state (OK, LOW, HIGH)
- Warmup time remaining (if in warmup period)
- Persistence settings

**Examples:**
```
INFO A2                          # Show complete coolant temp sensor info
INFO A2 CALIBRATION              # Show calibration coefficients
INFO A2 ALARM                    # Check alarm status
```

### Other Query Commands

```
VERSION                          # Show firmware version
LIST INPUTS                      # Show all configured inputs
LIST APPLICATIONS                # Show available application types
LIST SENSORS                     # Show sensor categories
LIST SENSORS NTC_THERMISTOR      # Show NTC thermistor sensors
LIST SENSORS TEMPERATURE         # Show all temperature sensors
```

**Note:** For complete system dumps, use `SYSTEM DUMP` or `SYSTEM DUMP JSON` (see [System Configuration](#system-configuration)).

---

## Quick Reference Examples

### Complete Setup Workflow

```
CONFIG
SET 6 CHT MAX6675
SET A0 OIL_TEMP NTC_10K_BETA_3950
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A3 OIL_PRESSURE GENERIC_150PSI
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET A2 ALARM 60 120
SET A3 ALARM 0.5 7.0
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100
SYSTEM UNITS TEMP F
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

### Configure Hall Speed Sensor

```
CONFIG
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 SPEED 100 2008 3.73 2000 300
SET 2 NAME SPEED
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
  Hint: Use 'LIST OUTPUTS' to see available outputs

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

