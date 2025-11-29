# Config/Run Mode Guide

**Applies to:** Runtime configuration mode only (EEPROM-based configuration)
**Not applicable to:** Compile-time configuration mode (USE_STATIC_CONFIG)

---

## Overview

The Config/Run mode system provides safe separation between configuration and operation:

- **CONFIG Mode**: Sensors paused, configuration unlocked - modify settings safely
- **RUN Mode**: Sensors active, configuration locked - normal operation

This prevents race conditions when modifying configuration during runtime and ensures stable operation.

---

## MODE_BUTTON (Pin 4)

Pin 4 serves as a multi-function button:

| Context | Behavior |
|---------|----------|
| **Boot time** | Hold LOW during boot to enter CONFIG mode |
| **RUN mode** | Press to silence alarms for 30 seconds |
| **CONFIG mode** | No effect (alarm silence only works in RUN mode) |

**Wiring:**
- Connect momentary push button between Pin 4 and GND
- External pull-up resistor required (e.g., 10kΩ to VCC)
- Button press = LOW signal
- Button released = HIGH signal

---

## System Modes

### CONFIG Mode

**Purpose:** Safe configuration without sensor interference

**Characteristics:**
- Sensors paused (no readings taken)
- Configuration commands unlocked (SET, CLEAR, SAVE, etc.)
- Serial CSV output disabled
- LCD display shows last values or config status
- Read-only commands available (LIST, INFO, VERSION, DUMP)

**When entered:**
- Automatically if EEPROM is empty/invalid on boot
- Hold MODE_BUTTON during boot
- Send `CONFIG` command via serial

**Use for:**
- Initial setup
- Adding/removing sensors
- Changing sensor types or calibrations
- Setting alarm thresholds
- Testing configurations without live data

### RUN Mode

**Purpose:** Normal operation with configuration protection

**Characteristics:**
- Sensors actively reading
- Configuration commands locked (write commands rejected)
- Serial CSV output enabled
- LCD display updating in real-time
- Read-only commands available (LIST, INFO, VERSION, DUMP)
- Alarm system active
- MODE_BUTTON silences alarms (30 seconds)

**When entered:**
- Automatically on boot if EEPROM config is valid and MODE_BUTTON not held
- Send `RUN` command via serial

**Use for:**
- Normal driving/operation
- Data logging
- Monitoring sensors
- Testing alarms

---

## Entering CONFIG Mode

### Method 1: Boot-Time Button (Hardware)

1. Power off the system
2. Press and hold MODE_BUTTON (Pin 4)
3. Power on the system
4. Wait for boot message: "CONFIG BUTTON DETECTED"
5. Release button after 1 second
6. System enters CONFIG mode

**Typical output:**
```
========================================
  CONFIG BUTTON DETECTED
  Entering CONFIG mode
========================================
```

### Method 2: Automatic (Empty EEPROM)

On first boot or after EEPROM clear:

```
========================================
  NO CONFIGURATION FOUND
  Automatically entering CONFIG mode
========================================
```

### Method 3: Serial Command (Software)

From RUN mode, send:
```
CONFIG
```

**Response:**
```
========================================
  ENTERED CONFIG MODE
  Sensors paused, configuration unlocked
  Type RUN to resume normal operation
========================================
```

---

## Entering RUN Mode

### Method 1: Automatic Boot (Default)

If EEPROM contains valid configuration and MODE_BUTTON is not pressed:
```
Starting in RUN mode (config locked)
```

### Method 2: Serial Command (Software)

From CONFIG mode, send:
```
RUN
```

**Response:**
```
========================================
  ENTERED RUN MODE
  Sensors active, configuration locked
  Type CONFIG to modify configuration
========================================
```

---

## Command Gating

### Commands Available in BOTH Modes

These commands are **always available** to prevent system deadlock:

| Command | Purpose |
|---------|---------|
| `CONFIG` | Switch to CONFIG mode |
| `RUN` | Switch to RUN mode |
| `HELP` or `?` | Show command list |
| `VERSION` | Show firmware version |
| `DUMP` | Dump current configuration |
| `INFO <pin>` | Show detailed sensor info |
| `LIST INPUTS` | List active inputs |
| `LIST APPLICATIONS` | List application presets |
| `LIST SENSORS` | List sensor types |

### Commands Available ONLY in CONFIG Mode

**Write/Modify Commands** (blocked in RUN mode):

| Command | Purpose |
|---------|---------|
| `SET <pin> APPLICATION <app> <sensor>` | Configure sensor |
| `SET <pin> SENSOR <sensor>` | Change sensor type |
| `SET <pin> NAME <name>` | Set abbreviated name |
| `SET <pin> DISPLAY_NAME <name>` | Set display name |
| `SET <pin> UNITS <units>` | Override display units |
| `SET <pin> ALARM <min> <max>` | Set alarm thresholds |
| `ENABLE <pin>` | Enable sensor |
| `DISABLE <pin>` | Disable sensor |
| `CLEAR <pin>` | Remove sensor |
| `SAVE` | Save config to EEPROM |
| `LOAD` | Reload from EEPROM |
| `RELOAD` | Alias for LOAD |
| `RESET` | Clear all configuration |

**Attempting write commands in RUN mode:**
```
========================================
  ERROR: Configuration locked in RUN mode
  Type CONFIG to enter configuration mode
========================================
```

---

## Typical Workflows

### Initial Setup (First Boot)

1. System boots to CONFIG mode (no EEPROM config)
2. Configure sensors via serial commands:
   ```
   SET A0 APPLICATION CHT K_TYPE_THERMOCOUPLE_MAX6675
   SET A1 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
   SET A2 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
   SET A0 ALARM 50 400
   SET A2 ALARM 0.5 5.0
   SAVE
   ```
3. Verify configuration:
   ```
   LIST INPUTS
   DUMP
   ```
4. Enter RUN mode:
   ```
   RUN
   ```
5. Sensors start reading, outputs active

### Modifying Existing Configuration

**From RUN mode:**
1. Enter CONFIG mode:
   ```
   CONFIG
   ```
2. Sensors pause, configuration unlocked
3. Make changes:
   ```
   SET A3 APPLICATION BOOST_PRESSURE GENERIC_BOOST_LINEAR
   SET A3 ALARM -0.5 2.0
   SAVE
   ```
4. Return to RUN mode:
   ```
   RUN
   ```

**From Boot:**
1. Hold MODE_BUTTON during power-on
2. System enters CONFIG mode
3. Make changes via serial
4. Send `RUN` to resume operation

### Testing New Sensor

**Safe testing in CONFIG mode:**
1. Enter CONFIG mode: `CONFIG`
2. Add sensor: `SET A5 APPLICATION OIL_TEMP VDO_150C_STEINHART`
3. Check info: `INFO A5`
4. Save: `SAVE`
5. Enter RUN mode: `RUN`
6. Monitor sensor on LCD/serial
7. If incorrect, return to CONFIG and adjust

### Emergency Reconfiguration

**In the field without serial access:**
1. Power off
2. Hold MODE_BUTTON
3. Power on → CONFIG mode
4. Connect serial terminal
5. Modify configuration
6. Send `RUN` to resume

---

## Behavior Differences

| Feature | CONFIG Mode | RUN Mode |
|---------|-------------|----------|
| **Sensor readings** | Paused | Active |
| **LCD display** | Static/config info | Real-time updates |
| **Serial CSV output** | Disabled | Enabled |
| **CAN bus output** | Disabled | Enabled |
| **SD card logging** | Disabled | Enabled |
| **Write commands** | Allowed | Blocked |
| **Read commands** | Allowed | Allowed |
| **Alarm system** | Inactive | Active |
| **MODE_BUTTON** | No effect | Silences alarm |
| **Main loop** | Early return | Full execution |

---

## Safety Features

### 1. Always-Available Mode Commands

`CONFIG` and `RUN` commands are **always available** in both modes to prevent deadlock scenarios.

### 2. Boot Mode Detection

System intelligently determines boot mode:
- Empty EEPROM → CONFIG mode (prevent blank operation)
- Button held → CONFIG mode (user override)
- Valid EEPROM, no button → RUN mode (normal operation)

### 3. Configuration Protection

Write commands are blocked in RUN mode to prevent:
- Accidental configuration changes during operation
- Race conditions between sensor reads and config modifications
- Unstable sensor behavior mid-operation

### 4. Sensor Pause in CONFIG

Sensors are completely paused in CONFIG mode:
- No ADC reads
- No I2C/SPI communication
- No value updates
- Prevents interference with configuration changes

---

## Troubleshooting

### Problem: Can't modify configuration

**Symptom:**
```
ERROR: Configuration locked in RUN mode
```

**Solution:**
Enter CONFIG mode first:
```
CONFIG
```

### Problem: No sensor readings

**Check current mode:**
```
# CONFIG mode shows:
ENTERED CONFIG MODE
Sensors paused, configuration unlocked

# RUN mode shows:
ENTERED RUN MODE
Sensors active, configuration locked
```

**Solution:**
If in CONFIG mode, enter RUN mode:
```
RUN
```

### Problem: System boots to CONFIG every time

**Cause:** EEPROM not saving or invalid

**Solution:**
1. Configure sensors
2. Verify: `LIST INPUTS`
3. Save: `SAVE`
4. Confirm: "Saved N inputs to EEPROM"
5. Restart system

### Problem: Can't enter CONFIG mode at boot

**Check wiring:**
- MODE_BUTTON on Pin 4
- Button connects Pin 4 to GND when pressed
- External pull-up resistor (10kΩ) to VCC
- Button must be held during entire boot sequence

**Alternative:**
Use serial command from RUN mode:
```
CONFIG
```

### Problem: MODE_BUTTON doesn't silence alarm

**Check mode:**
- Alarm silence only works in RUN mode
- In CONFIG mode, alarms are inactive

**Check wiring:**
- Verify button connected to Pin 4
- Test with multimeter: LOW when pressed, HIGH when released

---

## Technical Details

### State Management

**Current mode stored in:**
```cpp
static SystemMode currentMode;  // MODE_RUN or MODE_CONFIG
```

**Query functions:**
```cpp
bool isInConfigMode();  // Returns true if CONFIG mode
bool isInRunMode();     // Returns true if RUN mode
```

### Boot Detection

**Timing:**
```cpp
#define BOOT_DETECT_DELAY_MS 10  // Pin stabilization delay
```

**Detection logic:**
1. Check EEPROM validity
2. If invalid → CONFIG mode
3. If valid, wait 10ms for pin stabilization
4. If MODE_BUTTON LOW → CONFIG mode
5. Otherwise → RUN mode

### Main Loop Gating

**In CONFIG mode:**
```cpp
if (isInConfigMode()) {
    delay(LOOP_DELAY_MS);
    return;  // Skip sensor reads and outputs
}
```

**Result:**
- Sensors not read
- Outputs not sent
- Only serial command processing and display updates

---

## Best Practices

### 1. Always SAVE After Changes

Configuration is volatile until saved:
```
SET A0 APPLICATION CHT MAX6675
SAVE                              # Persist to EEPROM
```

### 2. Verify Before RUN

Check configuration before entering RUN mode:
```
LIST INPUTS                       # Quick overview
DUMP                              # Full config details
VERSION                           # Confirm firmware version
RUN                               # Enter operation
```

### 3. Use CONFIG for All Changes

Don't attempt to modify configuration in RUN mode. Always:
```
CONFIG                            # Enter CONFIG
# Make changes
SAVE                              # Persist
RUN                               # Resume operation
```

### 4. Test New Sensors Safely

Test in RUN mode first, but stay connected:
```
CONFIG
SET A5 APPLICATION OIL_TEMP VDO_150C_STEINHART
SAVE
RUN
# Monitor A5 on LCD/serial
# If wrong, immediately: CONFIG → adjust → SAVE → RUN
```

---

## Related Documentation

- [Adding Sensors Guide](ADDING_SENSORS.md) - How to configure sensors
- [Serial Command Reference](../../reference/SERIAL_COMMANDS.md) - Complete command list (if available)
- [EEPROM Persistence](../../reference/EEPROM_FORMAT.md) - EEPROM format details (if available)
- [Testing Plan](../../../TESTING_PLAN.md) - Mode system test cases (section 6.4)

---

## Summary

**CONFIG Mode:**
- Safe configuration environment
- Sensors paused
- All commands available
- Use for setup and changes

**RUN Mode:**
- Normal operation
- Sensors active
- Configuration locked
- Use for driving/monitoring

**MODE_BUTTON (Pin 4):**
- Boot: Hold to enter CONFIG
- RUN: Press to silence alarm
- CONFIG: No effect

**Key Commands:**
- `CONFIG` - Enter configuration mode
- `RUN` - Enter operation mode
- `SAVE` - Persist changes to EEPROM

---

**Last Updated:** 2025-01-28
**Firmware Version:** 0.3.0-alpha+
