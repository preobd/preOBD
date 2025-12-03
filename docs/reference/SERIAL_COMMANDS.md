# Serial Command Reference

Complete reference for all serial commands available in openEMS runtime configuration mode.

## Table of Contents
- [Sensor Configuration](#sensor-configuration)
- [Output Configuration](#output-configuration)
- [Display Configuration](#display-configuration)
- [System Configuration](#system-configuration)
- [Persistence Commands](#persistence-commands)
- [Information Commands](#information-commands)
- [Mode Commands](#mode-commands)

---

## Sensor Configuration

### Combined Syntax (Recommended)

Configure application and sensor in one command:

```
SET <pin> <application> <sensor>
```

**Examples:**
```
SET 6 CHT MAX6675
SET A0 OIL_TEMP VDO_150C
SET 7 EGT MAX31855
SET A2 COOLANT_TEMP VDO_120C
SET 18 RPM W_PHASE
```

**Benefits:**
- Faster configuration - one command instead of two
- Less typing
- Automatic validation of application/sensor compatibility

### Individual Field Commands (Legacy)

Still supported for backward compatibility and advanced configuration:

```
SET <pin> APPLICATION <application>   # Set measurement type
SET <pin> SENSOR <sensor>              # Set hardware sensor
SET <pin> NAME <name>                  # Set short name (8 chars max)
SET <pin> DISPLAY_NAME <name>          # Set full name (24 chars max)
SET <pin> UNITS <units>                # Override display units
SET <pin> ALARM <min> <max>            # Set alarm thresholds
```

**Examples:**
```
SET A0 APPLICATION OIL_PRESSURE
SET A0 SENSOR VDO_10BAR
SET A0 NAME "OilPres"
SET A0 DISPLAY_NAME "Engine Oil Pressure"
SET A0 UNITS PSI
SET A0 ALARM 10 80
```

### Input Control

```
ENABLE <pin>   # Enable input reading
DISABLE <pin>  # Disable input reading (stops reading, keeps config)
CLEAR <pin>    # Clear input configuration completely
```

### List Available Options

```
LIST INPUTS        # Show all configured inputs
LIST APPLICATIONS  # Show available application presets
LIST SENSORS       # Show available sensor types
INFO <pin>         # Show detailed configuration for a pin
```

---

## Output Configuration

Control output modules at runtime without recompiling.

### Commands

```
OUTPUT LIST                    # Show all output modules with status
OUTPUT <name> ENABLE          # Enable output module
OUTPUT <name> DISABLE         # Disable output module
OUTPUT <name> INTERVAL <ms>   # Set send interval (10-60000ms)
```

### Available Outputs

- `CAN` - CAN bus output
- `RealDash` - RealDash CAN frames
- `Serial` - CSV serial output
- `SD_Log` - SD card data logging

### Examples

```
# Enable CAN output at 100ms interval
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100

# Enable RealDash at 50ms for smooth gauges
OUTPUT RealDash ENABLE
OUTPUT RealDash INTERVAL 50

# Disable SD logging to save battery
OUTPUT SD_Log DISABLE

# Enable CSV serial output for debugging
OUTPUT Serial ENABLE
OUTPUT Serial INTERVAL 1000

# Check current status
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
```

---

## Display Configuration

Configure display settings at runtime.

### Commands

```
DISPLAY STATUS                           # Show current configuration
DISPLAY TYPE <LCD|OLED|NONE>            # Set display type
DISPLAY LCD ADDRESS <hex>                # Set I2C address
DISPLAY UNITS TEMP <C|F>                 # Default temperature units
DISPLAY UNITS PRESSURE <BAR|PSI|KPA>    # Default pressure units
DISPLAY UNITS ELEVATION <M|FT>          # Default elevation units
```

### Examples

```
# Switch to OLED display
DISPLAY TYPE OLED

# Change LCD I2C address
DISPLAY LCD ADDRESS 0x3F

# Use Fahrenheit by default
DISPLAY UNITS TEMP F

# Use PSI for pressure
DISPLAY UNITS PRESSURE PSI

# Use feet for elevation
DISPLAY UNITS ELEVATION FT

# Disable display
DISPLAY TYPE NONE

# Check current settings
DISPLAY STATUS
```

### Display Status Output

```
DISPLAY STATUS
```
Shows:
```
=== Display Configuration ===
Type: LCD
LCD I2C Address: 0x27
Temperature Units: Celsius
Pressure Units: Bar
Elevation Units: Meters
```

---

## System Configuration

Advanced system parameters and timing intervals.

### Commands

```
SYSTEM STATUS                    # Show all system configuration
SYSTEM VDO_BIAS <ohms>          # VDO pull-down resistor (100-10000)
SYSTEM SEA_LEVEL <hPa>          # Sea level pressure (800-1200)
SYSTEM INTERVAL SENSOR <ms>     # Sensor read interval (10-10000)
SYSTEM INTERVAL ALARM <ms>      # Alarm check interval (10-10000)
SYSTEM INTERVAL LCD <ms>        # LCD update interval (10-10000)
```

### Examples

```
# Configure VDO bias resistor for 2.2k sensors
SYSTEM VDO_BIAS 2200

# Set sea level pressure for accurate altitude
SYSTEM SEA_LEVEL 1013.25

# Read sensors faster (50ms)
SYSTEM INTERVAL SENSOR 50

# Check alarms more frequently
SYSTEM INTERVAL ALARM 100

# Update LCD at 250ms (smoother)
SYSTEM INTERVAL LCD 250

# View all settings
SYSTEM STATUS
```

### System Status Output

```
SYSTEM STATUS
```
Shows:
```
=== System Configuration ===
VDO Bias Resistor: 1000.00 ohms
Sea Level Pressure: 1013.25 hPa

Hardware Pins:
  Mode Button: 2
  Buzzer: 3
  CAN CS: 10
  CAN INT: 9
  SD CS: 4

Timing Intervals:
  Sensor Read: 100ms
  Alarm Check: 500ms
  LCD Update: 500ms
```

---

## Persistence Commands

Save and load configuration to/from EEPROM.

### Commands

```
SAVE            # Save all configuration to EEPROM
LOAD            # Load configuration from EEPROM
RESET           # Show reset warning
RESET CONFIRM   # Reset all configuration to compile-time defaults
```

### What Gets Saved

When you run `SAVE`, the following is persisted to EEPROM:
- All input configurations (pins, applications, sensors, names, alarms)
- Output module enable/disable states and intervals
- Display type and unit preferences
- System parameters (VDO bias, sea level, intervals)

### Examples

```
# Save current configuration
SAVE

# Load saved configuration
LOAD

# Reset to factory defaults (WARNING: destructive!)
RESET
RESET CONFIRM
```

### EEPROM Layout

```
Address Range    | Content                    | Size
-----------------|----------------------------|------
0x0000 - 0x0007  | Input header               | 8 bytes
0x0008 - 0x03EF  | Input configs (10 slots)   | 1000 bytes
0x03F0 - 0x041F  | System config              | 48 bytes
-----------------|----------------------------|------
Total: 1056 bytes (fits in Teensy 4.x 1080-byte EEPROM)
```

---

## Information Commands

Query system status and configuration.

### Commands

```
VERSION   # Show firmware and EEPROM version
DUMP      # Show complete configuration (inputs, outputs, display, system)
HELP      # Show command help
?         # Alias for HELP
```

### Examples

```
# Check firmware version
VERSION
```
Output:
```
========================================
  Firmware: v0.4.0-alpha
  EEPROM Version: 2
  Active Inputs: 5/10
========================================
```

```
# Show complete system state
DUMP
```
Output:
```
========================================
  Full Configuration Dump
========================================

[Lists all inputs with details]

=== Output Modules ===
CAN: Enabled, Interval: 100ms
RealDash: Disabled
Serial: Disabled
SD_Log: Disabled

=== Display Configuration ===
Type: LCD
LCD I2C Address: 0x27
Default Units: Temp=C, Press=Bar, Elev=M

=== System Configuration ===
VDO Bias: 1000.00 ohms, Sea Level: 1013.25 hPa
Intervals: Sensor=100ms, Alarm=500ms, LCD=500ms

To save this configuration to EEPROM, type: SAVE
```

---

## Mode Commands

Switch between CONFIG and RUN modes (EEPROM config mode only).

### Commands

```
CONFIG   # Enter configuration mode (unlock serial commands)
RUN      # Enter run mode (lock config, resume normal operation)
RELOAD   # Trigger watchdog reset (system reboot)
```

### Mode Behavior

**CONFIG Mode:**
- All configuration commands enabled
- Sensor reading continues
- Output modules continue sending data
- Used for runtime adjustments

**RUN Mode:**
- Configuration commands locked (read-only commands still work)
- Normal operation
- Type `CONFIG` to unlock

### Examples

```
# Enter configuration mode
CONFIG

# Make changes...
OUTPUT CAN ENABLE
DISPLAY UNITS TEMP F
SAVE

# Return to run mode
RUN

# Reboot system
RELOAD
```

---

## Quick Reference Examples

### Complete Setup Workflow

```
# 1. Enter config mode (if in RUN mode)
CONFIG

# 2. Configure sensors
SET 6 CHT MAX6675
SET 7 EGT MAX31855
SET A0 OIL_TEMP VDO_150C
SET A2 OIL_PRESSURE VDO_10BAR

# 3. Enable all sensors
ENABLE 6
ENABLE 7
ENABLE A0
ENABLE A2

# 4. Configure outputs
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100
OUTPUT RealDash ENABLE
OUTPUT RealDash INTERVAL 50

# 5. Set display preferences
DISPLAY UNITS TEMP F
DISPLAY UNITS PRESSURE PSI

# 6. Save configuration
SAVE

# 7. Return to run mode
RUN
```

### Change Output Intervals On-The-Fly

```
# Slow down CAN for debugging
OUTPUT CAN INTERVAL 500

# Speed up RealDash for smoother gauges
OUTPUT RealDash INTERVAL 20

# Save changes
SAVE
```

### Switch Display Type

```
# Switch from LCD to OLED
DISPLAY TYPE OLED
SAVE
RELOAD  # Reboot to reinitialize display
```

### Adjust for Different VDO Sensors

```
# Switch to 2.2k VDO sensors
SYSTEM VDO_BIAS 2200
SAVE

# Switch back to 1k VDO sensors
SYSTEM VDO_BIAS 1000
SAVE
```

---

## Command Tips

### Pin Notation
- Digital pins: `0`, `1`, `2`, etc.
- Analog pins: `A0`, `A1`, `A2`, etc.
- SPI pins: `10`, `11`, `12`, `13` (standard SPI bus)
- Special: `18`, `19` (interrupt-capable for RPM)

### Case Sensitivity
- Commands are **case-insensitive**: `OUTPUT`, `output`, `Output` all work
- Pin names are case-insensitive: `A0`, `a0` both work
- Application/Sensor names are case-insensitive

### Command Shortcuts
- `?` = `HELP`
- `OUTPUT STATUS` = `OUTPUT LIST`

### Error Messages
If a command fails, you'll see helpful error messages:
```
ERROR: Unknown output 'CANBUS'
  Hint: Use 'OUTPUT LIST' to see available outputs
```

### Read-Only in RUN Mode
Some commands work in RUN mode without entering CONFIG:
- `HELP`, `?`
- `VERSION`
- `DUMP`
- `INFO <pin>`
- `LIST *`
- `OUTPUT LIST`
- `DISPLAY STATUS`
- `SYSTEM STATUS`

---

## Troubleshooting

### Configuration Not Saving
```
# Check if SAVE succeeded
SAVE
# Should see: "✓ Configuration saved to EEPROM"

# If it fails, check EEPROM space
VERSION
# Shows EEPROM version - if mismatch, need RESET CONFIRM
```

### Can't Modify Configuration
```
# You might be in RUN mode
CONFIG
# Now try your commands again
```

### Wrong Sensor Readings After VDO Change
```
# Did you update the bias resistor?
SYSTEM VDO_BIAS 2200
SAVE
```

### Display Not Working After Type Change
```
# Need to reboot to reinitialize display hardware
RELOAD
```

---

## See Also

- [Configuration Mode Guide](../guides/configuration/CONFIG_RUN_MODE_GUIDE.md)
- [Adding Sensors Guide](../guides/configuration/ADDING_SENSORS.md)
- [VDO Bias Resistor Guide](../guides/hardware/BIAS_RESISTOR_GUIDE.md)
- [RealDash Setup](../guides/outputs/REALDASH_SETUP_GUIDE.md)
