# Relay Output Control

The relay output module enables automatic control of 12V electrical relays based on sensor thresholds with hysteresis and manual override capability.

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [Hardware Setup](#hardware-setup)
- [Configuration](#configuration)
- [Command Reference](#command-reference)
- [Use Cases](#use-cases)
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)

---

## Overview

The relay control feature allows openEMS to automatically switch 12V relays ON/OFF based on sensor readings. This is useful for controlling:
- Electric cooling fans (turn on when temperature exceeds threshold)
- Water pumps (activate based on temperature or pressure)
- Warning lights or indicators
- Electric heaters (turn on when cold)
- Bypass valves or solenoids

### Key Capabilities
- **2 independent relays** with separate configurations
- **Automatic control** with hysteresis to prevent relay chattering
- **Manual override** for testing or emergency operation
- **EEPROM persistence** - configuration survives power cycles
- **Safety features** - relays default OFF, warmup protection

---

## Features

### Automatic Control Modes

#### AUTO_HIGH Mode
Relay turns **ON** when sensor value rises above threshold, **OFF** when it falls below.

**Use case**: Cooling fan
- Temperature rises → fan turns ON at 90°C
- Temperature falls → fan turns OFF at 85°C
- Hysteresis band (85-90°C) prevents rapid ON/OFF cycling

```
Temperature (°C)
    ↑
100 |
 95 |                    ┌────────────
 90 |            ┌───────┘            ← Relay ON threshold
 85 |════════════╝                    ← Relay OFF threshold
 80 |
    └──────────────────────────────→ Time
         OFF    |    ON    |   OFF
```

#### AUTO_LOW Mode
Relay turns **ON** when sensor value falls below threshold, **OFF** when it rises above.

**Use case**: Low oil pressure warning light
- Pressure drops → light ON at 0.5 bar
- Pressure rises → light OFF at 1.0 bar

### Manual Override Modes

- **MANUAL_ON**: Force relay ON (ignores sensor)
- **MANUAL_OFF**: Force relay OFF (ignores sensor)
- **DISABLED**: Relay completely disabled

### Safety Features

1. **Safe Startup**: All relays initialize to OFF state
2. **Warmup Protection**: Relays stay OFF during sensor warmup period
3. **NaN Handling**: Relay turns OFF if sensor value is invalid
4. **Disabled Sensor**: Relay turns OFF if linked sensor is disabled
5. **EEPROM Persistence**: Configuration survives power cycles and resets

---

## Hardware Setup

### Relay Module Connection

**Typical relay module** (like common 5V/12V relay boards):

```
┌─────────────────────┐
│   Relay Module      │
├─────────────────────┤
│ VCC  ──→ 5V/12V    │  Power supply (check your module)
│ GND  ──→ GND       │  Ground
│ IN   ──→ GPIO Pin  │  Control signal (e.g., pin 23)
│                     │
│ COM  ──┐            │  Common (to +12V battery)
│ NO   ──┼──→ Load   │  Normally Open (to fan, pump, etc.)
│ NC   ──┘            │  Normally Closed (not used typically)
└─────────────────────┘
```

### Example: Cooling Fan Control

**Components needed:**
- 12V electric cooling fan
- 5V relay module (with optocoupler isolation)
- openEMS running on Teensy/Arduino

**Wiring:**
1. **Relay module power**: VCC → 5V, GND → GND
2. **Control signal**: IN → Teensy GPIO pin 23
3. **Fan power**:
   - Connect +12V battery → Relay COM terminal
   - Connect Relay NO → Fan positive wire
   - Connect Fan negative → Battery ground

**Result**: When relay activates, it completes the circuit powering the fan.

### GPIO Pin Selection

**Available pins** (platform-dependent):
- **Teensy 4.1**: Most digital pins (2-41)
- **Arduino Mega**: Digital pins (22-53 recommended to avoid conflicts)
- **Avoid**: Pins already used for sensors, CAN, I2C, SPI

**Recommended pins** for relays:
- Teensy 4.1: 23, 24, 25, 26 (not used by other peripherals)
- Arduino Mega: 22, 24, 26, 28 (avoid 50-53 for SPI)

---

## Configuration

### Quick Start Example: Cooling Fan

This example configures a cooling fan to turn ON at 90°C and OFF at 85°C.

```bash
# 1. Configure coolant temperature sensor on pin A2
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_STEINHART
ENABLE A2

# 2. Configure relay 0 for the fan
RELAY 0 PIN 23                    # Control signal on GPIO 23
RELAY 0 INPUT A2                  # Link to coolant temp sensor
RELAY 0 THRESHOLD 90 85           # ON at 90°C, OFF at 85°C
RELAY 0 MODE AUTO_HIGH            # Automatic mode (high temp triggers)

# 3. Save configuration to EEPROM
SAVE
```

**Done!** The fan will now automatically turn on when coolant reaches 90°C.

### Step-by-Step Configuration

#### Step 1: Configure Your Sensor

Before linking a relay, ensure the sensor is configured and enabled:

```bash
# Check available sensors
LIST SENSORS

# Configure sensor
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_STEINHART

# Set alarm thresholds (optional, separate from relay thresholds)
SET A2 ALARM 50 110

# Enable sensor
ENABLE A2

# Verify sensor is reading
INFO A2
```

#### Step 2: Configure Relay Output Pin

```bash
# Set which GPIO pin controls the relay
RELAY 0 PIN 23
```

**Response**: `Relay 0 pin set to 23`

#### Step 3: Link Relay to Sensor

```bash
# Connect relay to sensor input
RELAY 0 INPUT A2
```

**Response**: `Relay 0 linked to input on pin 54` (A2 = pin 54 on Teensy)

#### Step 4: Set Thresholds

```bash
# Set ON and OFF thresholds
# Format: RELAY <index> THRESHOLD <on_value> <off_value>
RELAY 0 THRESHOLD 90 85
```

**For AUTO_HIGH mode** (like cooling fan):
- First value (90) = Turn ON threshold
- Second value (85) = Turn OFF threshold
- OFF value should be < ON value

**For AUTO_LOW mode** (like low pressure warning):
- First value (0.5) = Turn ON threshold
- Second value (1.0) = Turn OFF threshold
- ON value should be < OFF value

#### Step 5: Set Mode

```bash
# Enable automatic control
RELAY 0 MODE AUTO_HIGH
```

**Available modes**:
- `AUTO_HIGH` - Turn ON when value ≥ ON threshold
- `AUTO_LOW` - Turn ON when value ≤ ON threshold
- `ON` - Force ON (manual override)
- `OFF` - Force OFF (manual override)

#### Step 6: Save Configuration

```bash
# Save to EEPROM (survives power cycles)
SAVE
```

**Response**: `✓ Configuration saved`

---

## Command Reference

### RELAY LIST
Show status of all relays.

```bash
RELAY LIST
```

**Output**:
```
=================================
Relay 0
=================================
Output Pin: 23
Input: Pin 54 (COOL)
  Current Value: 85.3 °C
Mode: AUTO_HIGH
Threshold ON: 90.00
Threshold OFF: 85.00
Current State: OFF
State Changes: 3
Last Change: 45 seconds ago
```

### RELAY \<index\> STATUS
Show detailed status of a specific relay.

```bash
RELAY 0 STATUS
RELAY 1 STATUS
```

**Alias**: Just typing `RELAY 0` also shows status.

### RELAY \<index\> PIN \<pin\>
Set the GPIO pin for relay control signal.

```bash
RELAY 0 PIN 23
RELAY 1 PIN 24
```

**Parameters**:
- `index`: 0 or 1
- `pin`: GPIO pin number (e.g., 23, 24, 25)

### RELAY \<index\> INPUT \<pin\>
Link relay to a sensor input.

```bash
RELAY 0 INPUT A2
RELAY 1 INPUT A3
```

**Parameters**:
- `index`: 0 or 1
- `pin`: Sensor pin (A0-A15, or digital pin)

**Note**: Sensor must be enabled first (`ENABLE A2`)

### RELAY \<index\> THRESHOLD \<on\> \<off\>
Set activation thresholds.

```bash
RELAY 0 THRESHOLD 90 85        # Cooling fan
RELAY 1 THRESHOLD 0.5 1.0      # Low pressure warning
```

**Parameters**:
- `index`: 0 or 1
- `on`: Activation threshold (in sensor's current units)
- `off`: Deactivation threshold

**Units**: Thresholds use the sensor's configured units (°C, bar, PSI, etc.)

### RELAY \<index\> MODE \<mode\>
Set relay operating mode.

```bash
RELAY 0 MODE AUTO_HIGH         # Automatic: ON when hot
RELAY 0 MODE AUTO_LOW          # Automatic: ON when cold
RELAY 0 MODE ON                # Manual: Force ON
RELAY 0 MODE OFF               # Manual: Force OFF
```

**Modes**:
- `AUTO_HIGH`: Automatic control, activates on high values
- `AUTO_LOW`: Automatic control, activates on low values
- `ON`: Manual override, force relay ON
- `OFF`: Manual override, force relay OFF

### RELAY \<index\> DISABLE
Disable a relay completely.

```bash
RELAY 0 DISABLE
```

**Effect**: Relay turns OFF and stops evaluating rules.

---

## Use Cases

### 1. Electric Cooling Fan

**Scenario**: Turn on cooling fan when coolant temperature exceeds 90°C.

**Configuration**:
```bash
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_STEINHART
ENABLE A2
RELAY 0 PIN 23
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 90 85
RELAY 0 MODE AUTO_HIGH
SAVE
```

**Behavior**:
- Temperature < 85°C: Fan OFF
- Temperature 85-90°C: Fan maintains current state (hysteresis)
- Temperature ≥ 90°C: Fan ON
- When temperature drops ≤ 85°C: Fan OFF

**Hysteresis benefit**: Prevents fan from rapidly cycling ON/OFF if temperature hovers around 90°C.

---

### 2. Low Oil Pressure Warning Light

**Scenario**: Turn on warning light when oil pressure drops below 0.5 bar.

**Configuration**:
```bash
SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR_CURVE
ENABLE A3
RELAY 1 PIN 24
RELAY 1 INPUT A3
RELAY 1 THRESHOLD 0.5 1.0
RELAY 1 MODE AUTO_LOW
SAVE
```

**Behavior**:
- Pressure > 1.0 bar: Light OFF
- Pressure 0.5-1.0 bar: Light maintains current state
- Pressure ≤ 0.5 bar: Light ON
- When pressure rises ≥ 1.0 bar: Light OFF

---

### 3. Electric Water Pump

**Scenario**: Turn on electric water pump when coolant temperature exceeds 80°C.

**Configuration**:
```bash
SET A2 APPLICATION COOLANT_TEMP
ENABLE A2
RELAY 0 PIN 23
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 80 75
RELAY 0 MODE AUTO_HIGH
SAVE
```

**Behavior**:
- Pump turns ON at 80°C
- Pump turns OFF at 75°C
- 5°C hysteresis prevents pump cycling

---

### 4. Manual Testing

**Scenario**: Test relay operation without automatic control.

**Commands**:
```bash
# Force relay ON
RELAY 0 MODE ON

# Wait and observe...
# (Check that fan/pump/light activates)

# Force relay OFF
RELAY 0 MODE OFF

# Return to automatic control
RELAY 0 MODE AUTO_HIGH
```

**Use case**: Verify wiring, test relay module, check fan operation.

---

### 5. Two-Speed Fan Control

**Scenario**: Low speed fan at 85°C, high speed fan at 95°C.

**Configuration**:
```bash
# Low speed fan
RELAY 0 PIN 23
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 85 80
RELAY 0 MODE AUTO_HIGH

# High speed fan
RELAY 1 PIN 24
RELAY 1 INPUT A2
RELAY 1 THRESHOLD 95 90
RELAY 1 MODE AUTO_HIGH

SAVE
```

**Behavior**:
- < 80°C: Both fans OFF
- 80-85°C: Hysteresis for low speed
- ≥ 85°C: Low speed fan ON
- ≥ 95°C: Both fans ON (high speed)

---

## Troubleshooting

### Relay doesn't activate

**Check 1: Verify relay configuration**
```bash
RELAY 0 STATUS
```
- Confirm `Output Pin` is correct
- Confirm `Input` is linked to correct sensor
- Confirm `Mode` is AUTO_HIGH or AUTO_LOW

**Check 2: Verify sensor is reading**
```bash
INFO A2
```
- Confirm sensor is `Enabled`
- Confirm `Value` is not NaN
- Confirm sensor is past warmup period

**Check 3: Check threshold logic**
```bash
RELAY 0 STATUS
```
- For AUTO_HIGH: `Current Value` must exceed `Threshold ON`
- For AUTO_LOW: `Current Value` must drop below `Threshold ON`

**Check 4: Test with manual override**
```bash
RELAY 0 MODE ON
```
- If relay clicks/activates → wiring is good, automatic logic issue
- If relay doesn't activate → wiring or hardware problem

---

### Relay chatters (rapid ON/OFF)

**Cause**: Insufficient hysteresis (ON and OFF thresholds too close).

**Solution**: Increase hysteresis gap.

**Example**:
```bash
# Bad: 1°C hysteresis (may chatter)
RELAY 0 THRESHOLD 90 89

# Good: 5°C hysteresis (stable)
RELAY 0 THRESHOLD 90 85
```

**Recommended hysteresis**:
- Temperature: 5-10°C
- Pressure: 0.2-0.5 bar
- Voltage: 0.5-1.0V

---

### Relay stays ON after condition clears

**Check 1: Verify thresholds are correct**
```bash
RELAY 0 STATUS
```
- For AUTO_HIGH: `Threshold OFF` should be < `Threshold ON`
- For AUTO_LOW: `Threshold OFF` should be > `Threshold ON`

**Check 2: Verify sensor value**
```bash
INFO A2
```
- Confirm sensor value has actually dropped below OFF threshold

**Check 3: Check if in manual mode**
```bash
RELAY 0 STATUS
```
- If `Mode: MANUAL_ON`, return to auto: `RELAY 0 MODE AUTO_HIGH`

---

### Configuration lost after power cycle

**Cause**: Configuration not saved to EEPROM.

**Solution**: Always run `SAVE` after configuration changes.

```bash
# After making changes:
SAVE
```

**Verification**:
```bash
# Reboot system, then check:
RELAY 0 STATUS
```

---

### Relay activates during startup

**Expected behavior**: Relays should stay OFF during sensor warmup.

**If relay activates immediately**:
1. Check sensor warmup time: `INFO A2 ALARM`
2. Increase warmup if needed: `SET A2 WARMUP 60000` (60 seconds)
3. Save configuration: `SAVE`

---

## Technical Details

### Hysteresis Implementation

The relay module implements **Schmitt trigger hysteresis** to prevent chattering:

```cpp
// AUTO_HIGH mode logic
if (!currentState && value >= thresholdOn) {
    return true;   // Turn ON
}
if (currentState && value <= thresholdOff) {
    return false;  // Turn OFF
}
return currentState;  // Maintain state in hysteresis band
```

**Key feature**: Different ON and OFF thresholds create a "dead band" where the relay maintains its current state.

### Memory Usage

**Per relay** (16 bytes in EEPROM):
- Output pin: 1 byte
- Input index: 1 byte
- Mode: 1 byte
- Reserved: 1 byte
- Threshold ON: 4 bytes (float)
- Threshold OFF: 4 bytes (float)
- Reserved: 4 bytes

**Total for 2 relays**: 32 bytes

**Runtime RAM** (~24 bytes per relay):
- Current state: 1 byte (bool)
- Last state change: 4 bytes (uint32_t)
- State change count: 4 bytes (uint32_t)

### Integration with Alarm System

The relay module is **independent** from the alarm system:
- Relays can have different thresholds than alarms
- Relay activation doesn't trigger alarms
- Alarms can trigger without relay activation

**Example**:
- Alarm threshold: 110°C (critical temperature)
- Relay threshold: 90°C (turn on cooling)
- Fan activates at 90°C, alarm triggers at 110°C

### Update Rate

Relays are evaluated every **100ms** (10 Hz) by default.

**To change**:
```bash
OUTPUT RELAY INTERVAL 50    # 50ms = 20 Hz (faster response)
OUTPUT RELAY INTERVAL 200   # 200ms = 5 Hz (slower, saves CPU)
SAVE
```

**Recommendation**: Keep default 100ms for most applications.

---

## Advanced Topics

### Multiple Relays on Same Sensor

You can link multiple relays to the same sensor for staged control:

```bash
# Stage 1: Low speed fan at 85°C
RELAY 0 PIN 23
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 85 80
RELAY 0 MODE AUTO_HIGH

# Stage 2: High speed fan at 95°C
RELAY 1 PIN 24
RELAY 1 INPUT A2
RELAY 1 THRESHOLD 95 90
RELAY 1 MODE AUTO_HIGH
```

### Relay with Different Unit Systems

If sensor is configured in Fahrenheit, thresholds use Fahrenheit:

```bash
SET A2 UNITS FAHRENHEIT
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 190 180    # °F, not °C
```

**Important**: Thresholds always use the sensor's current unit setting.

### Combining with Alarms

You can use both alarms and relays:

```bash
# Alarm at critical temperature
SET A2 ALARM 50 110
SET A2 ALARM ENABLE

# Relay activates earlier for cooling
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 90 85
RELAY 0 MODE AUTO_HIGH
```

**Result**:
- 90°C: Fan turns ON (relay)
- 110°C: Alarm buzzer sounds (alarm system)

---

## Safety Considerations

### Electrical Safety
1. **Use isolated relay modules** with optocouplers
2. **Fuse the load circuit** appropriately for the fan/pump
3. **Check relay ratings** (10A relays for high-current loads)
4. **Avoid back-EMF** from inductive loads (use flyback diodes)

### Software Safety
1. **Safe startup**: Relays default to OFF on boot
2. **Warmup protection**: Relays stay OFF during sensor warmup
3. **Fail-safe**: Relay turns OFF if sensor fails (NaN)
4. **Manual override**: Can force relay state for emergencies

### Best Practices
1. **Test in manual mode first**: Verify wiring with `RELAY 0 MODE ON`
2. **Use appropriate hysteresis**: Prevent relay cycling (5-10°C recommended)
3. **Monitor state changes**: Check `State Changes` count for excessive switching
4. **Save configuration**: Always `SAVE` after changes
5. **Document your setup**: Note which relay controls which device

---

## Quick Reference

| Command | Purpose | Example |
|---------|---------|---------|
| `RELAY LIST` | Show all relays | `RELAY LIST` |
| `RELAY <n> STATUS` | Show relay details | `RELAY 0 STATUS` |
| `RELAY <n> PIN <p>` | Set output pin | `RELAY 0 PIN 23` |
| `RELAY <n> INPUT <p>` | Link to sensor | `RELAY 0 INPUT A2` |
| `RELAY <n> THRESHOLD <on> <off>` | Set thresholds | `RELAY 0 THRESHOLD 90 85` |
| `RELAY <n> MODE <m>` | Set mode | `RELAY 0 MODE AUTO_HIGH` |
| `RELAY <n> DISABLE` | Disable relay | `RELAY 0 DISABLE` |
| `SAVE` | Save to EEPROM | `SAVE` |

### Typical Workflow

```bash
# 1. Configure sensor
SET A2 APPLICATION COOLANT_TEMP
ENABLE A2

# 2. Configure relay
RELAY 0 PIN 23
RELAY 0 INPUT A2
RELAY 0 THRESHOLD 90 85
RELAY 0 MODE AUTO_HIGH

# 3. Save
SAVE

# 4. Monitor
RELAY 0 STATUS
```

---

## Additional Resources

- **Main documentation**: [README.md](../README.md)
- **Command reference**: Type `HELP` in serial console
- **Examples**: Type `HELP` and see Examples section
- **Source code**: [src/outputs/output_relay.cpp](../src/outputs/output_relay.cpp)

---

**Questions or issues?** Report at https://github.com/preobd/openEMS/issues
