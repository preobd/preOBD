# Alarm System Guide

**Comprehensive guide to the openEMS alarm state machine**

---

## Table of Contents

1. [Overview](#overview)
2. [State Machine Architecture](#state-machine-architecture)
3. [Alarm States Explained](#alarm-states-explained)
4. [Warmup Times by Application](#warmup-times-by-application)
5. [Fault Persistence](#fault-persistence)
6. [Configuration](#configuration)
7. [Serial Commands](#serial-commands)
8. [Practical Examples](#practical-examples)
9. [Troubleshooting](#troubleshooting)

---

## Overview

The openEMS alarm system uses a **state-machine architecture** to provide robust, false-alarm-resistant monitoring. This system addresses common problems with simple threshold alarms:

**Problems Solved:**
- False alarms during cold start (oil pressure, coolant temp rising)
- False alarms from transient sensor spikes/noise
- Unstable alarm state during sensor initialization
- Alarm triggers from invalid sensor readings (NaN values)

**Key Features:**
- Per-input alarm state tracking
- Sensor-specific warmup periods
- Configurable fault persistence timing
- NaN-safe threshold checking
- Runtime configuration via serial commands
- Alarm is now an output module (can be enabled/disabled)

---

## State Machine Architecture

The alarm system has **5 distinct states**:

```
DISABLED ──────────────────────────────────────┐
   │                                            │
   │ (alarm enabled)                            │
   ↓                                            │
INIT (1 second stabilization)                  │
   │                                            │
   ↓                                            │
WARMUP (sensor-specific duration)              │
   │                                            │
   │ (sensor valid + warmup expired)           │
   ↓                                            │
READY ←──────────────────────┐                 │
   │                          │                 │
   │ (violation + persist)    │ (value normal) │
   ↓                          │                 │
ACTIVE ────────────────────── ┘                │
   │                                            │
   │ (alarm disabled)                          │
   └────────────────────────────────────────────┘
```

### State Transitions

| From | To | Trigger | Duration |
|------|----|---------| ---------|
| DISABLED | INIT | Alarm enabled | Immediate |
| INIT | WARMUP | Init period complete | 1 second |
| WARMUP | READY | Sensor valid + warmup complete | Application-specific |
| READY | ACTIVE | Threshold violation + persistence | Persistence time |
| ACTIVE | READY | Value returns to normal | Immediate |
| ANY | DISABLED | Alarm disabled | Immediate |

---

## Alarm States Explained

### DISABLED
**Purpose:** Alarm feature is turned off for this input

**Characteristics:**
- No alarm checking occurs
- Sensor still reads normally
- No hardware alarm output (buzzer, LED)
- Alarm threshold violations are ignored

**Entry Conditions:**
- User disables alarm: `SET <pin> ALARM OFF`
- Input is disabled: `DISABLE <pin>`
- Alarm output module disabled: `OUTPUT Alarm DISABLE`

**Exit Conditions:**
- User enables alarm: `SET <pin> ALARM <min> <max>`
- Transitions to: **INIT**

---

### INIT
**Purpose:** Brief stabilization period after alarm activation

**Characteristics:**
- Duration: **1 second** (fixed)
- Sensor readings may be incomplete/invalid
- Prevents alarm triggers from uninitialized values
- Hardware alarm output: OFF
- `Input.flags.isInAlarm = false`

**Entry Conditions:**
- Alarm just enabled
- System boot with alarm configured
- Sensor reconfigured while alarm enabled

**Exit Conditions:**
- 1 second elapsed
- Transitions to: **WARMUP**

---

### WARMUP
**Purpose:** Sensor-specific warmup period to prevent false alarms

**Characteristics:**
- Duration: **Application-specific** (0-60 seconds)
- Sensor reads actively but alarm triggers blocked
- Waits for sensor to produce valid readings (not NaN)
- Hardware alarm output: OFF
- `Input.flags.isInAlarm = false`

**Entry Conditions:**
- INIT period complete (1 second elapsed)

**Exit Conditions:**
- Warmup time elapsed **AND** sensor produces valid reading (not NaN)
- Transitions to: **READY**

**Why Warmup?**
During engine cold start:
- **Oil pressure** starts low, rises as engine runs
- **Coolant temp** starts low, gradually increases
- **CHT** takes time to stabilize after ignition
- **EGT** requires exhaust flow to produce valid readings

Without warmup, these sensors would immediately trigger alarms during normal startup.

---

### READY
**Purpose:** Normal alarm monitoring active

**Characteristics:**
- Sensor readings are valid and stable
- Threshold checking active
- Hardware alarm output: OFF (waiting for persistence)
- `Input.flags.isInAlarm = false`

**Entry Conditions:**
- WARMUP period complete
- Sensor producing valid readings
- OR: Value returned to normal from ACTIVE state

**Exit Conditions:**
- **Threshold violation + persistence time elapsed** → Transitions to: **ACTIVE**
- **Alarm disabled** → Transitions to: **DISABLED**

**Threshold Checking:**
- Violation if: `value >= maxValue` OR `value <= minValue`
- NaN values are ignored (treated as non-violating)
- Fault timer starts when violation first detected
- Alarm triggers only after fault persists for `persistTime_ms`

---

### ACTIVE
**Purpose:** Alarm condition confirmed and active

**Characteristics:**
- Threshold violation has persisted long enough
- Hardware alarm output: **ON** (buzzer sounds)
- `Input.flags.isInAlarm = true`
- Alarm state visible in all outputs (LCD, CAN, Serial, SD)

**Entry Conditions:**
- Threshold violation detected in READY state
- Violation persisted for `persistTime_ms` duration

**Exit Conditions:**
- **Value returns to normal range** → Transitions to: **READY**
- **Alarm disabled** → Transitions to: **DISABLED**

**Hardware Output:**
When alarm becomes ACTIVE:
- Buzzer sounds (configurable interval)
- LCD shows alarm icon and flashing indicator
- CAN/RealDash outputs include alarm flag
- Serial CSV includes `isInAlarm` column
- SD log records alarm state

**Alarm Silencing:**
- Press MODE_BUTTON in RUN mode → Silences alarm for 30 seconds
- Alarm automatically reactivates if violation persists after silence period
- Silence duration configurable in `config.h`

---

## Warmup Times by Application

Each application type has a **preset warmup time** based on typical sensor behavior during engine cold start:

| Application | Warmup Time | Persistence | Rationale |
|-------------|-------------|-------------|-----------|
| **CHT** | 30 seconds | 2 seconds | Cylinder heads warm slowly after startup |
| **EGT** | 20 seconds | 2 seconds | Exhaust temps stabilize after combustion begins |
| **COOLANT_TEMP** | 60 seconds | 5 seconds | Coolant takes time to circulate and warm |
| **OIL_TEMP** | 60 seconds | 5 seconds | Oil warms gradually from engine heat |
| **OIL_PRESSURE** | 60 seconds | 5 seconds | Oil pressure builds as engine warms and oil thins |
| **BOOST_PRESSURE** | 0 seconds | 5 seconds | Boost responds immediately, no warmup needed |
| **FUEL_PRESSURE** | 5 seconds | 1 second | Fuel pump pressurizes quickly |
| **PRIMARY_BATTERY** | 1 second | 500ms | Voltage stabilizes quickly after start |
| **AUXILIARY_BATTERY** | 2 seconds | 1 second | Secondary battery stabilizes after initial load |
| **BAROMETRIC_PRESSURE** | 0 seconds | 5 seconds | Environmental, no engine dependency |
| **ENGINE_RPM** | 0 seconds | 1 second | RPM is immediate, no warmup |
| **COOLANT_LEVEL** | 0 seconds | 1 second | Digital sensor, immediate response |
| **HUMIDITY** | 5 seconds | 2 seconds | Environmental sensor stabilization |
| **TCASE_TEMP** | 60 seconds | 5 seconds | Transfer case warms slowly |
| **AMBIENT_TEMP** | 2 seconds | 0 seconds | Environmental, instant readout |

**Warmup Time = 0:**
Applications with no warmup requirement transition directly from INIT → WARMUP → READY in ~1 second total.

---

## Fault Persistence

**Fault persistence** prevents false alarms from transient sensor spikes or electrical noise.

### How It Works

1. **Violation Detected:** Sensor value exceeds threshold
2. **Fault Timer Starts:** `AlarmContext.faultStartTime = currentTime`
3. **Persistence Check:** System waits for `persistTime_ms`
4. **Alarm Triggers:** If violation still present after persistence time

**If value returns to normal before persistence time:**
- Fault timer resets to 0
- No alarm triggered
- Alarm remains in READY state

### Persistence Times by Application

| Application Type | Persistence Time | Rationale |
|------------------|------------------|-----------|
| **Temperature Sensors** | 2-5 seconds | Temps change slowly, longer persistence prevents false positives |
| **Pressure Sensors** | 1-5 seconds | Pressure fluctuates, moderate persistence filters noise |
| **Voltage Sensors** | 500ms-1s | Voltage responds quickly, short persistence acceptable |
| **RPM Sensors** | 1 second | RPM changes rapidly, brief persistence sufficient |
| **Digital Sensors** | 500ms-1s | Binary state changes, short persistence |

**Example: Oil Pressure**
```
Time 0s:    Oil pressure drops to 0.3 bar (below 0.5 bar min threshold)
Time 0s:    Fault timer starts
Time 1s:    Violation still present (persistence = 5s)
Time 2s:    Violation still present
Time 3s:    Violation still present
Time 4s:    Violation still present
Time 5s:    Violation still present → ALARM TRIGGERS (ACTIVE state)
```

**Example: Transient Spike (No Alarm)**
```
Time 0s:    CHT spikes to 450°C (electrical noise, above 400°C max threshold)
Time 0s:    Fault timer starts
Time 0.1s:  Value returns to normal 350°C
Time 0.1s:  Fault timer RESETS to 0 → NO ALARM
```

---

## Configuration

### Compile-Time Configuration

Set alarm thresholds in `config.h`:

```cpp
// Alarm enable (applies to all inputs in static config mode)
#define ENABLE_ALARMS

// Alarm silence duration (MODE_BUTTON press in RUN mode)
#define ALARM_SILENCE_DURATION 30000  // 30 seconds

// Per-input alarm thresholds (in STANDARD UNITS)
// Temperature: Celsius, Pressure: bar, Voltage: volts

#define INPUT_0_ALARM_MIN 50.0    // CHT minimum: 50°C
#define INPUT_0_ALARM_MAX 400.0   // CHT maximum: 400°C

#define INPUT_1_ALARM_MIN 0.5     // Oil pressure min: 0.5 bar
#define INPUT_1_ALARM_MAX 10.0    // Oil pressure max: 10 bar
```

**Note:** Warmup and persistence times are preset per application and cannot be overridden in compile-time mode. Use runtime mode for custom warmup/persistence.

### Runtime Configuration

Set alarm thresholds via serial commands (CONFIG mode):

```
CONFIG
SET A0 ALARM 50 400        # CHT: 50-400°C
SET A1 ALARM 0.5 10        # Oil pressure: 0.5-10 bar
SET A2 ALARM -40 150       # Coolant: -40-150°C
SAVE
RUN
```

### Custom Warmup/Persistence (Runtime Only)

Override application defaults:

```
CONFIG
SET A1 OIL_PRESSURE VDO_5BAR_CURVE
ALARM A1 WARMUP 45000      # Custom 45-second warmup
ALARM A1 PERSIST 3000      # Custom 3-second persistence
SAVE
RUN
```

**Use cases for custom values:**
- Older engines that warm slower (increase warmup)
- Noisy sensors (increase persistence)
- Racing applications (decrease warmup for faster response)

---

## Serial Commands

### Query Alarm Status

```
INFO <pin>
```

Shows alarm state, warmup, and persistence:

```
======================================
  Input A1 (OIL_PRESSURE)
======================================
Sensor: VDO_5BAR_CURVE
Value: 2.35 bar
Alarm Thresholds: 0.50 - 10.00 bar
Alarm State: READY
Warmup Time: 60000ms
Persistence Time: 5000ms
```

### Enable/Disable Alarm Output Module

The alarm system is now an **output module**:

```
OUTPUT Alarm ENABLE        # Enable alarm output (buzzer, LEDs)
OUTPUT Alarm DISABLE       # Disable alarm output (silent mode)
OUTPUT Alarm INTERVAL 500  # Set alarm check interval (10-10000ms)
OUTPUT LIST                # Show alarm status with other outputs
```

**Example:**
```
OUTPUT LIST
```
Output:
```
=== Output Modules ===
CAN: Enabled, Interval: 100ms
RealDash: Disabled
Serial: Enabled, Interval: 1000ms
SD_Log: Disabled
Alarm: Enabled, Interval: 500ms
```

### Per-Input Alarm Configuration

```
ALARM <pin> WARMUP <ms>    # Set custom warmup time
ALARM <pin> PERSIST <ms>   # Set custom persistence time
```

Examples:
```
ALARM A0 WARMUP 20000      # CHT: 20-second warmup (faster than default 30s)
ALARM A1 PERSIST 1000      # Oil pressure: 1-second persistence (faster response)
```

**Note:** These commands are available in runtime mode only and override application defaults.

---

## Practical Examples

### Example 1: Cold Start - Oil Pressure

**Scenario:** Engine starts, oil pressure low initially

```
Time 0s:     Engine start, oil pressure = 0.2 bar
             Alarm state: INIT (1s stabilization)

Time 1s:     Alarm state: WARMUP (60s warmup begins)
             Oil pressure still low: 0.3 bar
             No alarm (warmup blocks trigger)

Time 30s:    Oil pressure rising: 1.5 bar
             Alarm state: WARMUP (30s remaining)

Time 61s:    Warmup complete, oil pressure = 3.0 bar
             Alarm state: READY
             Normal operation
```

**Without warmup:** Alarm would trigger immediately at 0s (pressure below 0.5 bar minimum).

### Example 2: Transient Sensor Spike - CHT

**Scenario:** Electrical noise causes brief CHT spike

```
Time 0s:     CHT reading: 350°C (normal)
             Alarm state: READY

Time 0.05s:  Electrical spike: CHT = 500°C (above 400°C max)
             Fault timer starts
             Alarm state: READY (waiting for persistence)

Time 0.15s:  Spike clears: CHT = 355°C (normal)
             Fault timer RESETS to 0
             Alarm state: READY
             No alarm triggered
```

**Without persistence:** Alarm would trigger immediately from brief 50ms spike.

### Example 3: Real Overheat - CHT

**Scenario:** Actual cylinder head overheating

```
Time 0s:     CHT reading: 380°C (normal)
             Alarm state: READY

Time 5s:     CHT rising: 405°C (above 400°C max)
             Fault timer starts
             Alarm state: READY (persistence = 2s)

Time 6s:     CHT still high: 410°C
             Fault timer: 1s elapsed (1s remaining)

Time 7s:     CHT still high: 415°C
             Fault timer: 2s elapsed → PERSISTENCE MET
             Alarm state: ACTIVE
             Buzzer sounds, LCD shows alarm

Time 10s:    User presses MODE_BUTTON
             Buzzer silenced for 30 seconds
             Alarm state: still ACTIVE (alarm condition persists)

Time 15s:    CHT drops: 395°C (below threshold)
             Alarm state: READY
             Buzzer remains silent (silence period active)
```

### Example 4: Runtime Alarm Configuration

**Scenario:** Add alarm to existing sensor

```
# Check current state
INFO A2

# Add alarm thresholds
CONFIG
SET A2 ALARM 60 120       # Coolant: 60-120°C
SAVE

# Check alarm state
INFO A2
# Shows: Alarm State: INIT (just configured)

# Wait 1 second
# Alarm state transitions: INIT → WARMUP

# Wait 60 seconds (coolant warmup)
# Alarm state transitions: WARMUP → READY

RUN
```

---

## Troubleshooting

### Problem: Alarm triggers immediately on cold start

**Symptom:**
```
Alarm active at startup despite normal readings
```

**Diagnosis:**
Check warmup time for application:
```
INFO <pin>
```

Look for:
```
Warmup Time: 0ms
```

**Solution:**
Increase warmup time:
```
CONFIG
ALARM <pin> WARMUP 30000    # 30-second warmup
SAVE
RUN
```

---

### Problem: Alarm triggers from brief sensor spikes

**Symptom:**
```
Alarm active for <1 second, then clears
Sensor readings show occasional spikes
```

**Diagnosis:**
Check persistence time:
```
INFO <pin>
```

Look for:
```
Persistence Time: 0ms
```

**Solution:**
Increase persistence:
```
CONFIG
ALARM <pin> PERSIST 2000    # 2-second persistence
SAVE
RUN
```

---

### Problem: Alarm doesn't trigger when it should

**Symptom:**
```
Sensor reading violates threshold but no alarm
```

**Diagnosis:**
1. Check if alarm is enabled:
```
INFO <pin>
```

Look for:
```
Alarm Thresholds: OFF
```

2. Check alarm state:
```
Alarm State: WARMUP
```

3. Check if Alarm output module is enabled:
```
OUTPUT LIST
```

Look for:
```
Alarm: Disabled
```

**Solutions:**

**If alarm thresholds not set:**
```
CONFIG
SET <pin> ALARM <min> <max>
SAVE
RUN
```

**If stuck in WARMUP:**
- Wait for warmup period to complete
- Check if sensor is producing valid readings (not NaN)

**If Alarm output disabled:**
```
CONFIG
OUTPUT Alarm ENABLE
SAVE
RUN
```

---

### Problem: Alarm stays active after value returns to normal

**Symptom:**
```
Sensor reading back in normal range but alarm still active
```

**Diagnosis:**
This should not happen. Check for:
1. Software bug (report to GitHub)
2. Sensor reading oscillating around threshold

**Temporary Solution:**
Manually disable and re-enable alarm:
```
CONFIG
SET <pin> ALARM OFF
SET <pin> ALARM <min> <max>
SAVE
RUN
```

---

### Problem: Can't configure warmup/persistence times

**Symptom:**
```
ALARM <pin> WARMUP 30000
ERROR: Unknown command
```

**Diagnosis:**
You're in compile-time configuration mode (`USE_STATIC_CONFIG` is defined).

**Solution:**
Custom warmup/persistence is only available in runtime mode. To use runtime mode:

1. Edit `src/config.h`
2. Comment out: `// #define USE_STATIC_CONFIG`
3. Recompile and upload
4. Configure via serial commands

---

## Implementation Details

### AlarmContext Structure

Each input has a 12-byte alarm context:

```cpp
struct AlarmContext {
    AlarmState state;           // Current alarm state (1 byte)
    uint32_t stateEntryTime;    // When current state was entered (4 bytes)
    uint32_t faultStartTime;    // When threshold violation started (4 bytes, 0 = no violation)
    uint16_t warmupTime_ms;     // Warmup duration in milliseconds (2 bytes)
    uint16_t persistTime_ms;    // Fault persistence time in milliseconds (2 bytes)
};
```

### State Machine Code

See `src/inputs/alarm_logic.cpp` for complete implementation.

**Key functions:**
- `initInputAlarmContext()` - Initialize alarm context for input
- `updateInputAlarmState()` - Update single input alarm state
- `updateAllInputAlarms()` - Update all inputs (called from main loop)

**Hardware control:**
- Alarm logic sets `Input.flags.isInAlarm = true/false`
- Hardware output handled by `src/outputs/output_alarm.cpp`
- Separation allows flexible alarm output (buzzer, LED, relay, etc.)

---

## See Also

- [Serial Command Reference](../../reference/SERIAL_COMMANDS.md) - Complete command list
- [Config/Run Mode Guide](CONFIG_RUN_MODE_GUIDE.md) - Configuration workflow
- [Adding Sensors Guide](ADDING_SENSORS.md) - How to add sensors
- [CHANGELOG](../../../CHANGELOG.md) - Alarm system refactor details

---

**Last Updated:** 2025-01-28
**Firmware Version:** 0.5.0-alpha (Unreleased)
**EEPROM Version:** 3
