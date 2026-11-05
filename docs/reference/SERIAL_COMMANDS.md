# Serial Commands Reference

**Complete reference for preOBD serial configuration commands**

---

## Overview

preOBD is configured via serial commands at 115200 baud using an interactive command-line interface.

Any serial monitor works for typing commands. A terminal emulator (screen,
minicom, picocom, PuTTY, etc.) additionally gives you tab completion, command
history and arrow-key editing.

Commands are case-insensitive. Pin names can be `A0`, `a0`, `6`, etc.

### Quick Connection Guide

**macOS/Linux (terminal emulator):**
```bash
screen /dev/tty.usbmodem* 115200   # macOS
screen /dev/ttyACM0 115200         # Linux
# Exit: Ctrl+A then K then Y
```

**Windows (terminal emulator):**
- Use PuTTY, Tera Term, or similar terminal emulator
- Set to Serial, 115200 baud, with VT100 emulation

**Serial monitor:**
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

preOBD commands follow consistent patterns based on what you're configuring:

### Discovery Commands
Pattern: `LIST <type>`

```
LIST INPUTS              # Show all configured inputs
LIST APPLICATIONS        # Show available application presets
LIST SENSORS             # Show sensor categories
LIST SENSORS <category>  # Show sensors in category (e.g., THERMISTOR)
LIST SENSORS TEMPERATURE # Show all temperature sensors
LIST OUTPUTS             # Show available output modules
LIST TRANSPORTS          # Show available transports
```

**JSON catalog export** (Teensy 3.x/4.x and ESP32 only):

```
LIST APPLICATIONS JSON         # Export application catalog as JSON
LIST SENSORS JSON              # Export full sensor catalog as JSON
LIST SENSORS <category> JSON   # Export sensors in one category as JSON
LIST OUTPUTS JSON              # Export output module list as JSON
LIST UNITS JSON                # Export units registry as JSON
LIST CATEGORIES JSON           # Export sensor categories as JSON
LIST PIDS JSON                 # Export standard OBD-II PID table as JSON
LIST MEASUREMENT_TYPES JSON    # Export measurement type enum as JSON
LIST CALIBRATION_TYPES JSON    # Export calibration type enum as JSON
SYSTEM DUMP REGISTRY JSON      # Export all catalogs in one JSON object
```

These commands export the firmware's compile-time registries for use by external tooling (e.g., a web UI populating dropdowns). This is distinct from `SYSTEM DUMP JSON`, which exports the user's active configuration.

### Configuration Commands
Pattern: `SET <pin> <field> <value>`

```
SET A0 CHT MAX6675                       # Combined syntax (recommended)
SET A0 APPLICATION CHT                   # Set application type
SET A0 SENSOR THERMISTOR VDO_120C_TABLE # Two-layer: category + preset
SET A0 SENSOR VDO_120C_TABLE            # Direct sensor name
RULE ADD A0 OUTSIDE 50 260              # Add an alarm Rule (both-sided band, °C)
```

### Control Commands
Pattern: `SET <pin> INPUT <ENABLE|DISABLE>` / `CLEAR <pin>`

```
SET A0 INPUT ENABLE            # Enable input reading
SET A0 INPUT DISABLE           # Disable input (keeps config)
CLEAR A0             # Remove input configuration
CLEAR A0 SAVE        # Remove it and persist to EEPROM in one command
```

`CLEAR` changes RAM only until you `SAVE`. A trailing `SAVE` (also accepted
after `CLEAR <pin> CALIBRATION`) does both in one command. It saves the
**whole** RAM config, not just the clear — exactly like typing `SAVE` next.

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
SYSTEM PINS                        # Show pin allocations
INFO A0                            # Query specific pin
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

preOBD has three levels of configuration:

### Per-Input Settings (use SET command)
Apply to individual sensor inputs:
- Application type, sensor hardware
- Display names and units override
- Alarm thresholds
- Custom calibration parameters

**Example:**
```
SET A2 COOLANT_TEMP VDO_120C_TABLE
RULE ADD A2 OUTSIDE 60 120
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
7. [Rules & Actions](#rules--actions-control-engine)
8. [Relay Control (deprecated)](#relay-control)
9. [Bus Configuration](#bus-configuration)
10. [AT Command Passthrough](#at-command-passthrough)
11. [Transport Configuration](#transport-configuration)
12. [Display Configuration](#display-configuration)
13. [System Configuration](#system-configuration)
14. [Mode Commands](#mode-commands)
15. [Persistence Commands](#persistence-commands)
16. [Query Commands](#query-commands)
17. [Diagnostics](#diagnostics)
18. [Quick Reference Examples](#quick-reference-examples)

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
SET A2 COOLANT_TEMP GENERIC_NTC_BETA  # Coolant with generic NTC (then SET A2 CAL BETA ...)
SET A3 OIL_PRESSURE GENERIC_PRESSURE_150PSI  # Oil pressure with linear sensor
SET 2 VEHICLE_SPEED HALL_SPEED       # Vehicle speed with Hall sensor
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER  # Battery voltage
```

### Individual Commands

```
SET <pin> APPLICATION <app>      # Set measurement type
SET <pin> SENSOR <sensor>        # Set hardware sensor
SET <pin> NAME <name>            # Set full display label (32 chars max)
SET <pin> ABBR <abbreviation>    # Set short LCD label (8 chars max)
SET <pin> DISPLAY ENABLE|DISABLE # Show or hide this input on the LCD
SET <pin> UNITS <units>          # Override display units (must measure the same quantity as the input; unknown names are refused)
SET <pin> ALARM <min> <max>      # Alarm outside a Min..Max band
SET <pin> ALARM MIN <v>          # One-sided: alarm below <v> (floor)
SET <pin> ALARM MAX <v>          # One-sided: alarm above <v> (ceiling)
SET <pin> ALARM OFF              # Disable alarm
```

### Input Control

```
SET <pin> INPUT ENABLE           # Enable input reading
SET <pin> INPUT DISABLE          # Disable input (keeps config)
CLEAR <pin>                      # Remove input configuration completely (and its Rules)
```

`CLEAR <pin>` also deletes every Rule sourced from that pin, reporting what it
removed and any Action it released, so no orphaned Rule is left holding a
`FAILSAFE ON` Action energized. Use `SAVE` to persist the removal.

### Available Units

| Unit | Aliases | Description |
|------|---------|-------------|
| `CELSIUS` | `C` | Temperature in Celsius |
| `FAHRENHEIT` | `F` | Temperature in Fahrenheit |
| `BAR` | | Pressure in bar |
| `PSI` | | Pressure in PSI |
| `KPA` | | Pressure in kilopascals |
| `INHG` | | Pressure in inches of mercury |
| `VOLTS` | `V` | Voltage |
| `RPM` | | Revolutions per minute |
| `KPH` | | Speed in kilometers per hour |
| `MPH` | | Speed in miles per hour |
| `PERCENT` | `%` | Percentage |
| `METERS` | `M` | Altitude in meters |
| `FEET` | `FT` | Altitude in feet |
| `KILOMETERS` | `KM` | Distance in kilometers (odometer, distance PIDs) |
| `MILES` | `MI` | Distance in miles |
| `SECONDS` | `S` | Time in seconds (run-time PIDs) |
| `MINUTES` | `MIN` | Time in minutes |
| `HOURS` | `H` | Time in hours |
| `GRAMS_PER_SEC` | `GPS` | Mass air flow in g/s |
| `LB_PER_MIN` | `LBMIN` | Mass air flow in lb/min |
| `LITERS_PER_HOUR` | `LPH` | Fuel rate in L/h |
| `GAL_PER_HOUR` | `GPH` | Fuel rate in US gallons per hour |

An input accepts only units of what it measures: a distance input takes `KM` or `MI`, not `MPH`.

---

## Custom Calibration

Override sensor preset calibration with custom values.

### Calibration Commands

```
CLEAR <pin> CALIBRATION [SAVE]     # Revert to preset calibration
SET <pin> CAL BIAS <resistor>        # Set bias resistor (Ω)
SET <pin> CAL STEINHART <bias> <a> <b> <c>  # Steinhart-Hart thermistor
SET <pin> CAL BETA <bias> <beta> <r0> <t0>  # Beta equation thermistor
SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>  # Linear pressure
SET <pin> CAL PRESSURE_POLY <bias> <a> <b> <c>  # Polynomial pressure (VDO)
SET <pin> CAL VOLTAGE_DIVIDER <r1> <r2> [<corr> [<offset>]]  # Battery divider resistors
SET <pin> DIVIDER <ratio>        # Voltage divider ratio for linear sensors (0 < ratio ≤ 1.0; refused on others except 1.0)
SET <pin> CAL RPM <poles> <ratio> <timeout> <min> <max>  # RPM (5 params)
SET <pin> CAL RPM <poles> <ratio> <mult> <timeout> <min> <max>  # RPM (6 params)
SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> <timeout> <max>  # Speed (5 params)
SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> <mult> <timeout> <max>  # Speed (6 params)
```

Use `INFO <pin> CALIBRATION` to view active calibration parameters (see [INFO Command](#info-command)).

### Thermistor Calibration Examples

**Steinhart-Hart (when you have A, B, C coefficients):**
```
SET A0 OIL_TEMP GENERIC_NTC_STEINHART
SET A0 CAL STEINHART 2490 1.129e-3 2.341e-4 8.775e-8
SAVE
```

**Beta equation (when you have β value from datasheet):**
```
SET A0 OIL_TEMP GENERIC_NTC_BETA
SET A0 CAL BETA 2490 3950 10000 25
SAVE
```

### Voltage Divider Ratio (5V sensors on 3.3V ADCs)

Linear sensors (pressure, MAP, generic 0.5–4.5V) wired through a hardware
voltage divider need a ratio applied so the calibration can stay expressed
in raw sensor terms. The ratio is `V_at_pin / V_at_sensor`.

```
SET A1 BOOST_PRESSURE GENERIC_BOOST
SET A1 DIVIDER 0.6                   # 2.2k/3.3k divider, ratio = 3.3/(2.2+3.3) = 0.6
SAVE
```

- `1.0` = no divider (default)
- Range: `(0.0, 1.0]`
- Per-input setting (different inputs can have different dividers)
- Persisted to EEPROM on `SAVE`
- Only applies to linear-calibration sensor types; auto-configured
  `VOLTAGE_DIVIDER` battery inputs and resistive (VDO/thermistor) sensors
  ignore this field.

### Pressure Calibration Examples

**Linear 0.5-4.5V sensor, 0-7 bar:**
```
SET A1 BOOST_PRESSURE GENERIC_BOOST
SET A1 CAL PRESSURE_LINEAR 0.5 4.5 0.0 7.0
SAVE
```

**VDO polynomial:**
```
SET A2 OIL_PRESSURE VDO_5BAR_CURVE
SET A2 CAL PRESSURE_POLY 1000 -0.3682 36.465 10.648
SAVE
```

### RPM Calibration Example

**12-pole alternator, 3:1 pulley ratio, fine-tuned +2%:**
```
SET 5 ENGINE_RPM W_PHASE_RPM
SET 5 CAL RPM 12 3.0 1.02 2000 100 8000
SAVE
```

### Speed Calibration Example

**Hall effect sensor, 100 pulses/rev, 2008mm tire circumference, 3.73 final drive:**
```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 CAL SPEED 100 2008 3.73 2000 300
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

> **Per-input alarm facade.** `SET <pin> ALARM ...` is the quick way to set an
> input's alarm — it creates or edits **one** `ALARM`-severity Rule keyed to that
> input. Under the hood alarms are control-engine Rules; for warning/info tiers,
> control-only thresholds, hysteresis, or more than one Rule per input, use
> `RULE ADD` directly (see the **Rules & Actions** section below). Timing (warmup/debounce) is seeded from the
> application's default template on `SET <pin> APPLICATION <app>` and lives on the
> Rule (see `RULE <id>`).
>
> **Units:** alarm and Rule limits are entered in base units — °C, bar, volts,
> RPM, km/h — whatever units the input displays in.

Configure an input's alarm as a two-sided band or a one-sided floor/ceiling.

### Understanding Alarm Timing

Alarm timing is a property of the Rule, seeded from the application default when
the application is assigned (`SET <pin> APPLICATION <app>`):

**Warmup Time** - Delay after system startup before the alarm can trigger
- **Purpose:** Prevents nuisance alarms during sensor stabilization
- **Common use case:** Oil pressure during engine start (needs seconds to build)

**Debounce/Persistence Time** - How long a threshold violation must persist before triggering
- **Purpose:** Filters out transient spikes or noise
- **Common use case:** Temperature spikes from exhaust gas turbulence

### Alarm Commands

```
SET <pin> ALARM <min> <max>          # Alarm outside a Min..Max band (min < max)
SET <pin> ALARM MIN <v>              # One-sided floor: alarm when value < v
SET <pin> ALARM MAX <v>              # One-sided ceiling: alarm when value > v
SET <pin> ALARM WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>]   # set/clear the warning threshold — see below
SET <pin> ALARM DISABLE              # Remove the input's alarm Rule
```

`SET <pin> ALARM WARN ...` is the facade for the same warning threshold
described under **Warning threshold (`WARN`)** below — it edits the
input's canonical alarm Rule by pin instead of by rule id. Moving `<min>`/`<max>`/`MIN`/`MAX`
past an explicit warn side reverts that side to `AUTO`.

Use `INFO <pin> ALARM` (or `RULE LIST` / `RULE <id>`) to view an input's live
alarm Rule — condition, limits, severity, and firing state (see the
**Rules & Actions** section).

### Acknowledging a Sounding Alarm

```
ALARM SILENCE                    # Acknowledge every alarm sounding right now (= mode button press)
ALARM STATUS                     # Severity, which rules are acknowledged, time remaining, policy
ALARM DURATION <secs|CLEAR>      # CONFIG: re-arm after 5..630 s (step 5), or hold until the rule clears
ALARM SCOPE <BUZZER|ALL>         # CONFIG: ALL also stops the LED blink (steady red) while acknowledged
```

Acknowledgement is **per rule**: `ALARM SILENCE` (or a short press of the mode
button — both take the same path) mutes only the rules that are sounding at
that moment; a *different* rule tripping later still sounds. The default is
`DURATION CLEAR`. With a timed `DURATION` the mute holds for exactly that long, even if the
reading dips out of alarm and back in — a flaky sensor does not re-sound on
every glitch. With `DURATION CLEAR` the mute holds until the reading returns
to normal, however long that takes, and a re-trip is heard again. For a broken
sensor that stays tripped on a long drive, `CLEAR` makes it one press; for one
that flaps in and out, use a longer timed duration or `SET <pin> ALARM DISABLE`. `ALARM SILENCE` and `ALARM STATUS` work
in RUN mode; the two policy verbs are CONFIG-only and need `SAVE`.

### Output Module Control (Global Alarm Hardware)

```
OUTPUT Alarm ENABLE              # Enable alarm hardware (buzzer, LED)
OUTPUT Alarm DISABLE             # Disable alarm hardware (silent mode)
OUTPUT Alarm INTERVAL <ms>       # Set alarm check interval (10-10000ms)
```

### Practical Examples

**Oil Pressure Alarm (one-sided floor):**
```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 ALARM MIN 0.7                 # Alarm only if pressure drops below 0.7 bar (~10 PSI)
```
*Why:* You care about a **loss** of oil pressure, not a ceiling — a one-sided floor
is exactly right. Warmup/debounce come from the application default (oil pressure
needs time to build during startup; brief drops during gear changes are normal).

**Coolant Temperature Alarm (band):**
```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A2 ALARM 60 120                  # Alarm if <60°C or >120°C
```
*Why:* Both a too-cold (not warmed up) and too-hot (overheating) reading matter, so
a two-sided band catches both ends with one Rule.

**CHT Alarm (one-sided ceiling):**
```
SET 6 CHT MAX6675
SET 6 ALARM MAX 260                  # Alarm only on overtemp above 260°C
```
*Why:* Cylinder head overtemp is the danger; a low CHT isn't an alarm condition.

**Disable Alarm:**
```
SET A3 ALARM DISABLE                 # Remove the input's alarm Rule
```

**Global Alarm Hardware Control:**
```
OUTPUT Alarm DISABLE                 # Silent mode (no buzzer)
# Alarms still check and log, but buzzer won't sound
```

### Alarm State Machine

Each Rule tracks its state: `INIT → WARMUP → READY ↔ ACTIVE` (or `PARKED` when disabled with `RULE <id> DISABLE`)

| State | Description |
|-------|-------------|
| PARKED | Rule disabled; it never fires |
| INIT | Brief initialization after the Rule is armed |
| WARMUP | Application-specific warmup (0-60s) before the Rule can fire |
| READY | Normal monitoring |
| ACTIVE | Condition met (buzzer sounds for `ALARM` severity) |

See **[Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md)** for complete documentation.

---

## Output Configuration

Control output modules at runtime.

### Output Module Availability

Output modules must be compiled into the firmware to be available. Which ones are included depends on the board (see `boards/<board>/board.h`).

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
| `RealDash` | RealDash frames on the data channel (USB or a serial/BLE module) |
| `Serial` | CSV lines on the data channel |
| `SD_Log` | SD card data logging |
| `Alarm` | Alarm system (buzzer, LED) |

### Examples

```
# Enable CAN at 100ms interval (the CAN bus itself must be up — see Bus Configuration)
BUS CAN OUTPUT CAN1 ENABLE 500000
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

### SD Logging Status

`OUTPUT STATUS` says whether SD logging is *enabled*. `SYSTEM SD` says whether it is *working*:

```
SYSTEM SD                    # One line of text
SYSTEM SD JSON               # The same as JSON (the mobile app's System tab polls it)
```

```bash
SYSTEM SD
SD: logging, LOG0042.CSV, 12400 rows, 0 errors, 15660 of 15699 MB free

SYSTEM SD JSON
{"sd":{"state":"logging","file":"LOG0042.CSV","rows":12400,"errors":0,"freeMB":15660,"totalMB":15699}}
```

- `state` is one of:
  - `none`: this board or build has no SD logging (for example a Teensy 4.0 without an SD module wired).
  - `nocard`: no card was found at boot. Cards are only detected at boot, so insert one and restart.
  - `removed`: the card was pulled while logging. Logging has stopped until a restart. Only Teensy boards can detect a pulled card; elsewhere it shows as `error`.
  - `off`: SD logging is disabled (`OUTPUT SD_Log ENABLE` turns it on).
  - `error`: logging stopped after write errors (usually a full or failing card), or no log file could be created.
  - `idle`: enabled but not writing, because preOBD is in CONFIG mode or no input is routed to SD.
  - `logging`: writing rows.
- `file` is the current or last log file (`null` before the first one). `rows` and `errors` count since logging was last enabled.
- `freeMB` and `totalMB` are decimal megabytes, as printed on the card. Free space is measured once at boot and then reduced by what preOBD writes, so it's an estimate. Both are `null` with no card, or after a removal.
- Both forms are read-only: they work in RUN mode, never touch the card and don't count as configuration changes. A removal is noticed by the logger before its next write, so it shows up only while logging. `SYSTEM DIAG HW` checks the slot directly.
- `SYSTEM SD JSON` is not available on the Arduino Mega (JSON is compiled out there), and the Mega has no SD logging, so `SYSTEM SD` answers `none`.

---

## Rules & Actions (control engine)

The control engine unifies alarms and relays into one condition→effect pipeline:
a **Rule** watches an input (threshold + hysteresis + debounce + warmup + severity)
and triggers **Actions** (relay/GPIO outputs) and/or the built-in Annunciator
(LED/buzzer/BLE). `SET <pin> APPLICATION <app>` seeds a default alarm Rule.

```
RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>] [WARN <lo|NONE|AUTO>] [INFO|WARNING|ALARM|NONE]         # one-sided
RULE ADD <pin> OUTSIDE <lo> <hi> [HYST <w>] [WARN <lo|NONE|AUTO> <hi|NONE|AUTO>] [INFO|WARNING|ALARM|NONE]  # both-sided band
RULE LIST                         # all Rules + live state (READY/ACTIVE) + severity
RULE <id>                         # one Rule's detail
RULE <id> WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>]   # edit an ALARM rule's warning threshold in place (no re-arm)
RULE <id> LIMIT ABOVE|BELOW <limit> | OUTSIDE <lo> <hi>   # move a Rule's limits in place (keeps id, severity, Action links; no re-arm). Values in base units (°C, bar), like RULE ADD
RULE <id> SEVERITY INFO|WARNING|ALARM|NONE   # change a Rule's tier in place (keeps id, limits, Action links; no re-arm). Leaving ALARM clears its warn tier; drops any acknowledgement
RULE <id> ENABLE | DISABLE        # re-arm / park a Rule without deleting it
RULE DEL <id>                     # delete (drops it from every Action's membership)
RULE RESET <id>                   # restore config from the application default + re-arm

ACTION ADD RELAY <pin> [FAILSAFE ON|OFF] [INVERT]   # actuator (failsafe on sensor loss)
ACTION <id> RULE <ruleId|NONE>    # membership := exactly that Rule (NONE = manual-only)
ACTION <id> RULE ADD <ruleId>     # add a member Rule (the Action is OR-driven)
ACTION <id> RULE REMOVE <ruleId>  # drop one member Rule
ACTION <id> NAME <text>           # operator label, e.g. "Cooling Fan"
ACTION <id> NAME                  # clear the label (back to unnamed)
ACTION <id> ON|OFF                # manual override (RUN-mode allowed)
ACTION <id> AUTO                  # release an operator override
ACTION LIST | ACTION <id> | ACTION DEL <id>
```

**An Action can follow several Rules**: it is asserted while **any**
member Rule is active, so one cooling fan can be demanded by coolant temp, trans
temp and an A/C request at once. `ACTION <id> RULE <ruleId>` *replaces* the whole
membership; use `RULE ADD` to keep the existing members. Every membership change
releases the Action to AUTO and starts it OFF.

A **parked** Rule (`RULE <id> DISABLE`) stays in the table and keeps its Action
memberships, but sits in `PARKED`: it never asserts, never annunciates, and
cannot force an Action's failsafe. `RULE <id> ENABLE` re-arms it (through
`WARMUP`); both verbs are CONFIG-only and idempotent — enabling an already-armed
Rule does nothing rather than silencing a live alarm for its warmup.

With several sources, a **live member's demand outranks a faulted sibling**: if one
sensor dies while another is legitimately calling for the output, the output stays
on. The `FAILSAFE` state applies only when no healthy member is demanding it.

### Warning threshold (`WARN`)

An `ALARM`-severity Rule optionally carries a **warning threshold** — a
pre-trip band that lights the LED/LCD yellow before the value actually
alarms. Each side (`lo`; `hi` too for `OUTSIDE`) is independently `AUTO`
(derived, default 90% of the limit — today's behavior if you say nothing),
`NONE` (skip the warning tier — straight to alarm), or an explicit value:

```
RULE ADD A0 ABOVE 230 WARN 210                # warn at 210, alarm at 230
RULE ADD A0 ABOVE 230 WARN NONE               # no pre-warning — straight to ALARM
RULE ADD A6 OUTSIDE 11.5 15.0 WARN 12.0 14.6
RULE 3 WARN 12.0 14.6                         # edit rule #3's warn tier in place
SET A0 ALARM WARN 210                         # same edit via the per-input facade
```

`WARN` is rejected on a non-`ALARM` rule
(`ERROR: WARN only applies to ALARM rules (a WARNING rule is its own tier)`)
and an explicit value must sit strictly on the safe side of the limit
(`ERROR: WARN must be below the ABOVE limit` / `...above the BELOW limit` /
`...inside the OUTSIDE band` / `ERROR: WARN lo must be < hi`). `RULE LIST` /
`RULE <id>` append the effective edge — `warn 210` (explicit), `warn 207*`
(derived; `*` marks `AUTO`), or `warn none`; `RULE <id>` additionally shows
the raw stored mode: `Warn: lo=SET 210  hi=AUTO (207)`. Moving a limit past
an explicit warn side (via `SET <pin> ALARM ...`) reverts that side to
`AUTO` with a `NOTE:`; `RULE RESET` and `SET <pin> APPLICATION` reseed both
return warn modes to `AUTO` along with the limits.

Severity: `NONE` = control-only (drives Actions, no annunciation); `WARNING` =
indicator + log + BLE, no buzzer; `ALARM` = + buzzer. An ALARM Rule also raises a
derived WARNING as its value approaches the limit. Structural edits (`ADD`/`DEL`/
`RULE`/`RESET`) are CONFIG-only; `LIST`/`<id>`/manual `ON|OFF` are RUN-allowed.

## Relay Control

The `RELAY` command is **deprecated**: it only prints a pointer to `RULE` / `ACTION`.
A relay is now an **Action** of type `RELAY` driven by a **Rule** (see
[Rules & Actions](#rules--actions-control-engine) above). Translating the old commands:

| Old `RELAY` command | Rules & Actions equivalent |
|---------------------|----------------------------|
| `RELAY 0 PIN 23` | `ACTION ADD RELAY 23` |
| `RELAY 0 INPUT A2` + `THRESHOLD 90 85` + `MODE AUTO_HIGH` | `RULE ADD A2 ABOVE 90 HYST 5 NONE`, then `ACTION <id> RULE <ruleId>` |
| `MODE AUTO_LOW` (on below 15, off above 20) | `RULE ADD A3 BELOW 15 HYST 5 NONE` |
| `MODE MANUAL_ON` / `MANUAL_OFF` | `ACTION <id> ON` / `ACTION <id> OFF` |
| Back to automatic | `ACTION <id> AUTO` |
| `RELAY LIST` | `ACTION LIST` (and `RULE LIST`) |

**Cooling fan example** (coolant on A2, fan relay on pin 23):
```
RULE ADD A2 ABOVE 90 HYST 5 NONE      # fan demand: on above 90°C, off below 85°C
ACTION ADD RELAY 23                   # the relay output
ACTION 0 RULE 0                       # link them (use the ids printed above)
SAVE
```

`NONE` severity makes it a control-only Rule (no alarm). For wiring and more
examples, see the [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md).

---

## Bus Configuration

Select which I2C, SPI, CAN bus, or serial ports to use for sensors and outputs. This is useful when you want to use alternate bus pins (e.g., Wire1 instead of Wire on Teensy) or enable additional serial ports for communication.

**preOBD never guesses which bus you wired.** An I2C, SPI or CAN bus is brought up
only when you select it here (`BUS I2C 0`, `BUS SPI 0`, `BUS CAN OUTPUT CAN1 ENABLE`)
or when your `boards/<board>/wiring.ini` declares it (`-D I2C_WIRED_BUS=0`,
`SPI_WIRED_BUS`, `CAN_WIRED_BUS`). So an LCD or BME280 needs an I2C bus, a
thermocouple needs an SPI bus, and the OBD-II CAN output needs a CAN bus
before they work. The boot log tells you when one is missing.

### Commands

```
BUS                              # Show BUS command help
BUS I2C                          # Show current I2C bus configuration
BUS I2C <0|1|2>                  # Select I2C bus (0=Wire, 1=Wire1, 2=Wire2)
BUS I2C NONE                     # Release the I2C bus (frees its pins on next reboot)
BUS I2C CLOCK <kHz>              # Set I2C clock speed (100, 400, or 1000 kHz)
BUS SPI                          # Show current SPI bus configuration
BUS SPI <0|1|2>                  # Select SPI bus (0=SPI, 1=SPI1, 2=SPI2)
BUS SPI CLOCK <Hz>               # Set SPI clock speed in Hz
BUS SPI NONE                     # Release the SPI bus
BUS CAN                                               # Show current CAN bus configuration
BUS CAN JSON                                          # Live CAN status as JSON (works in RUN mode)
BUS CAN NONE                                          # Release the CAN bus
BUS CAN BAUDRATE <bps>                                # Set CAN baudrate for both input/output (125000, 250000, 500000, 1000000)
BUS CAN INPUT <CAN1|CAN2|CAN3> <ENABLE|LISTEN|DISABLE> [bps]  # Configure CAN input bus with mode and optional baudrate
BUS CAN INPUT BAUDRATE <bps>                          # Set CAN input baudrate only
BUS CAN OUTPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [bps]  # Configure CAN output bus with optional baudrate
BUS CAN OUTPUT BAUDRATE <bps>                         # Set CAN output baudrate only
BUS SERIAL                       # Show all serial port status
BUS SERIAL <1-8>                 # Show specific port status
BUS SERIAL <1-8> ENABLE [baud]   # Enable serial port with optional baud rate
BUS SERIAL <1-8> DISABLE         # Disable serial port
BUS SERIAL <1-8> BAUDRATE <rate> # Set serial port baud rate
BUS SERIAL <1-8> ELM327 ENABLE   # Assign ELM327 emulator to serial port (boards with SUPPORTS_ELM327)
BUS SERIAL <1-8> ELM327 DISABLE  # Remove ELM327 emulator from serial port
```

### Platform Availability

| Platform | I2C Buses | SPI Buses | CAN Buses | Serial Ports |
|----------|-----------|-----------|-----------|--------------|
| Teensy 4.1 | Wire, Wire1, Wire2 | SPI, SPI1, SPI2 | CAN1, CAN2, CAN3 | Serial1-Serial8 |
| Teensy 4.0 | Wire, Wire1, Wire2 | SPI, SPI1, SPI2 | CAN1, CAN2, CAN3 | Serial1-Serial7 |
| Teensy 3.6 | Wire, Wire1, Wire2 | SPI, SPI1 | CAN1, CAN2 | Serial1-Serial6 |
| ESP32-S3 | Wire, Wire1 | SPI | CAN1 (TWAI) | Serial1-Serial2 |
| Arduino Mega | Wire | SPI | CAN1 (MCP2515 over SPI) | Serial1-Serial3 |

### CAN Input Modes

CAN input supports three operating modes:
- **DISABLE** - Input disabled, bus not initialized
- **ENABLE** (Normal) - Active input with ACK. Also polls the ECU: each imported OBD-II PID (source ID `0x7E8`-`0x7EF`) is requested in turn with a Mode 01 request, so a PID reads without a scan tool on the bus
- **LISTEN** (Passive) - Listen-only monitoring. No ACK bits, no error frames, no TX of any kind (no OBD-II polling). `BUS` refuses a LISTEN input on the CAN output's bus, in either order. A config imported or saved with that combination still runs, but the bus is not listen-only: input never polls, while output transmits and ACKs. Use when sniffing an existing CAN bus (e.g., reading from a car's OBD-II/ECU network) to avoid disrupting communication between other nodes

### CAN Baud Rates

Supported CAN baud rates: 125000, 250000, 500000, 1000000 (bps)

Input and output buses can operate at different speeds to support mixed protocols (e.g., input from a 250kbps bus, OBD-II output at 500kbps).

### CAN Scan and Status (JSON)

```bash
SCAN CAN [ms] [JSON]   # CONFIG: listen for ms (1000-60000, default 10000) and list what was seen
SCAN PIDS [JSON]       # CONFIG: ask the ECUs which OBD-II PIDs they support
SCAN CANCEL            # CONFIG: stop and clear the results
SCAN STATUS JSON       # Scan state and progress (any mode)
SCAN RESULTS JSON      # What SCAN CAN found (any mode)
SCAN SUPPORTED JSON    # What SCAN PIDS found, per ECU (any mode)
BUS CAN JSON           # Live CAN input/output/poll state (any mode)
```

`SCAN CAN` only listens: it reports frames that appear on the bus, including the answers to preOBD's own polling of imported PIDs. It sends no requests of its own, so on an ECU bus with nothing imported and nothing else polling it finds nothing. The text table printed when the scan ends counts real frames per `(CAN ID, PID)`. A trailing `JSON` skips that table; read the results with `SCAN RESULTS JSON` instead.

`SCAN PIDS` does transmit: it sends the OBD-II "supported PIDs" requests (Mode 01 PID 0x00, then 0x20, … while an ECU says there are more) to the functional address `0x7DF`, so every ECU answers, and collects each ECU's list. It takes under a second, needs CAN input in `ENABLE` mode (it is refused in `LISTEN`), and pauses the polling of imported PIDs while it runs. `SCAN PIDS` and `SCAN CAN` share one scan: starting either clears the other's results. Import a PID that only another ECU supports with `SET CAN <pid> <ecu>`, e.g. `SET CAN 0x05 0x7E9`. A PID with several sensors (0x78 EGT 1–4, …) takes `SENSOR <n>` after the ECU: `SET CAN 0x78 SENSOR 2`.

The four `JSON` forms are read-only. They answer in RUN mode and don't count as configuration changes (they leave the BLE status frame's `configGeneration` alone), so a client can poll them. Each prints a blank line, then one line of JSON:

```bash
SCAN STATUS JSON
{"state":"listening","scanId":1,"durationMs":4000,"remainingMs":1523,"found":2,"passive":true,"ecus":null}

SCAN SUPPORTED JSON
[{"canId":2024,"pids":[5,11,12,13]},{"canId":2025,"pids":[5,33]}]

SCAN RESULTS JSON
[{"canId":2024,"pid":5,"obd":true,"dataLength":1,"frames":201,"ageMs":4899,"data":"79"},{"canId":2024,"pid":12,"obd":true,"dataLength":2,"frames":200,"ageMs":4909,"data":"0F7C"}]

BUS CAN JSON
{"caps":{"buses":3,"baudrates":[125000,250000,500000,1000000],"listenShared":false,"maxScanResults":32,"scanMs":[1000,60000]},"input":{"bus":0,"mode":"NORMAL","baudrate":500000,"running":true,"sharedWithOutput":true,"rebootPending":false,"framesRx":229,"lastFrameAgeMs":8,"poll":{"active":true,"backoff":false,"missStreak":0,"requests":229,"answers":229,"txFail":0}},"output":{"bus":0,"enabled":true,"module":false,"baudrate":500000},"imports":[{"pin":192,"pinLabel":"CAN:0","sourceCanId":2024,"sourcePid":12,"dataOffset":0,"polled":true,"ageMs":8},{"pin":193,"pinLabel":"CAN:1","sourceCanId":2024,"sourcePid":120,"dataOffset":3,"polled":true,"ageMs":18}]}
```

- `state` is `idle`, `listening` (`SCAN CAN`), `querying` (`SCAN PIDS`) or `done`. `scanId` increases with every `SCAN CAN` or `SCAN PIDS`, so a client can tell that someone else restarted the scan. `found` changes when a new result appears.
- `passive` is `false` after `SCAN PIDS`. Then `found` counts the supported PIDs over all ECUs, and `ecus` is how many ECUs answered: `0` means none did (ignition off, no ECU), as opposed to an ECU that supports nothing. `ecus` is `null` for `SCAN CAN`.
- In `SCAN SUPPORTED JSON`, `canId` is each answering ECU's response ID (`2024` = `0x7E8`, the engine) and `pids` its supported PIDs in decimal, without the bitmap PIDs `0x00`/`0x20`/…. It is `[]` after a `SCAN CAN`.
- `pid` is the OBD-II PID for an OBD response (`obd: true`, CAN ID `0x7E8`-`0x7EF`), otherwise the frame's first byte. `data` is the last payload in hex. Look up names and units in `LIST PIDS JSON`.
- Each import's `dataOffset` is where its value starts in the reply, counted from the first byte after the PID. It tells apart the sensors of a multi-sensor PID: match it against the PID's `dataOffset` (sensor 1) and its `sensors` list in `LIST PIDS JSON`, e.g. `{"sensor":2,"name":"EGT Bank 1 Sensor 2","abbr":"EGT12","dataOffset":3,"supportBit":1}`. `supportBit` (on a multi-sensor PID's row and each of its sensors) is the value's bit in the reply's first byte, which says whether the ECU has that sensor.
- Buses are indexes (`0` = CAN1), and `255` means none, as in `SYSTEM DUMP JSON`. `mode` is `OFF`, `NORMAL` or `LISTEN`. `output.enabled` is the bus setting and `output.module` is `OUTPUT CAN ENABLE`.
- `input.bus`, `mode` and `baudrate` are the *configured* values. `BUS CAN INPUT` changes take effect after `SAVE` and a reboot, and until then `rebootPending` is `true` and `running` describes the old settings.
- `lastFrameAgeMs` and each import's `ageMs` are `null` until a frame has been received. `poll.backoff` is `true` while nothing answers the poller (it has slowed to one request per second). `poll.answers` counts an ECU's "PID not supported" reply as an answer, so `answers` matching `requests` doesn't mean every imported PID has a value: check each import's `ageMs`.
- These are not available on the Arduino Mega (JSON is compiled out there).

### Serial Port Baud Rates

Supported baud rates: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600

### Examples

**Switch to Wire1 for I2C sensors (Teensy 4.x):**
```
BUS I2C 1                        # Select Wire1 (pins 17/16 on Teensy 4.x)
SAVE                             # Save configuration
SYSTEM REBOOT                    # Reboot to apply
```

This moves the LCD too — every I2C client shares the one selected bus. The usual
reason to do it is to free the default bus's SDA/SCL (pins 18/19 = A4/A5 on
Teensy 4.x) for use as sensor inputs.

Whichever bus is up, its SDA/SCL pins are reserved and `SET <pin>` will refuse
them. With no display configured (`DISPLAY TYPE NONE`) and no explicit
`BUS I2C <n>`, no bus is brought up and the pins stay free for inputs.
`BUS I2C` reports the bus that is actually up, which the display may have
acquired on demand without an explicit selection.

Note that I2C **sensors** do not bring the bus up themselves — they need an
explicit `BUS I2C <n>`, saved, followed by a reboot. Only the display acquires a
bus on demand.

**Configure dual CAN buses with different baud rates:**
```
BUS CAN OUTPUT CAN1 ENABLE 500000   # OBD-II output on CAN1 at 500kbps
BUS CAN INPUT CAN2 ENABLE 250000    # Input on CAN2 at 250kbps (active mode)
SAVE
```

**Enable listen-only mode for passive vehicle bus monitoring:**
```
BUS CAN OUTPUT CAN1 ENABLE 500000   # OBD-II output on CAN1
BUS CAN INPUT CAN2 LISTEN 250000    # Passive monitoring on CAN2 (no ACK/TX)
SAVE
```

**Configure CAN with same baud rate (backward compatible):**
```
BUS CAN OUTPUT CAN1 ENABLE       # Enable CAN output on CAN1
BUS CAN BAUDRATE 500000          # Set both input/output to 500kbps
SAVE
```

**Update just the input baud rate:**
```
BUS CAN INPUT BAUDRATE 250000    # Change input to 250kbps
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

**Enable ELM327 emulator on Serial2 (e.g., HM-10 BLE module):**
```
BUS SERIAL 2 ENABLE 115200       # Enable Serial2 at 115200 baud
BUS SERIAL 2 ELM327 ENABLE       # Assign ELM327 emulator to Serial2
SAVE
```

**Disable ELM327 on Serial2:**
```
BUS SERIAL 2 ELM327 DISABLE      # Remove ELM327 from Serial2 (port stays enabled)
SAVE
```

### Bus Status Display

`BUS I2C`, `BUS SPI` and `BUS CAN` each print the status of that bus type, for example:
```
=== I2C Bus Configuration ===
Active: Wire (SDA=18, SCL=19) @ 400kHz
Available buses: 0=Wire, 1=Wire1, 2=Wire2

=== SPI Bus Configuration ===
Active: SPI (MOSI=11, MISO=12, SCK=13) @ 4.0MHz
Available buses: 0=SPI, 1=SPI1, 2=SPI2

=== CAN Bus Configuration ===
Input:  CAN2 (LISTEN) @ 250kbps
Output: CAN1 (ENABLED) @ 500kbps
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

  Serial1: ENABLED @ 115200 baud [ELM327] (RX=0, TX=1)
  Serial2: disabled (RX=7, TX=8)
  Serial3: disabled (RX=15, TX=14)
  Serial4: disabled (RX=16, TX=17)
  Serial5: ENABLED @ 9600 baud (RX=21, TX=20)
  Serial6: disabled (RX=25, TX=24)
  Serial7: disabled (RX=28, TX=29)
  Serial8: disabled (RX=34, TX=35)

Use BUS SERIAL <port> ENABLE [baudrate] to enable a port
Use BUS SERIAL <port> DISABLE to disable a port
Use BUS SERIAL <port> ELM327 ENABLE to assign ELM327 emulator to a port
```

### Notes

- Bus changes take effect on next reboot (use `SYSTEM REBOOT` after `SAVE`)
- All sensors of a type use the selected bus (no per-sensor bus assignment)
- The system will fall back to bus 0 if the selected bus fails to initialize
- Bus configuration is saved to EEPROM and included in JSON exports
- Serial ports can be assigned to TRANSPORT for message routing (see `TRANSPORT` command)
- Enabled serial ports register their pins with the pin registry to prevent conflicts

### ELM327 Serial Emulator

Available on boards whose `board.h` declares `SUPPORTS_ELM327`.

- The port must be enabled first: `BUS SERIAL <n> ENABLE <baud>` before running `ELM327 ENABLE`
- Only one serial port may act as the ELM327 port at a time
- A port assigned to ELM327 cannot simultaneously carry CONTROL/DATA/DEBUG router traffic; remove the transport assignment first
- `ELM327 DISABLE` removes the emulator role but leaves the port enabled
- Changes take effect immediately; use `SAVE` to persist across reboots

For full setup instructions (HM-10 wiring, phone app configuration), see
[`docs/guides/outputs/DIRECT_BLE_OBD_GUIDE.md`](../guides/outputs/DIRECT_BLE_OBD_GUIDE.md).

---

## AT Command Passthrough

Send a raw AT command to an attached serial module (HM-10, HC-05, ELM327 chip, etc.) and capture its reply, without rewiring the module to a USB-serial adapter.

### Syntax

```
AT <port> <command>
```

- `<port>` — serial port number (1–N, must already be enabled via `BUS SERIAL <port> ENABLE`)
- `<command>` — the AT string to send. Sent **raw with no CR/LF appended** (HM-10 and most BLE modules require this)

The command waits up to ~500 ms for a response, extending the window while bytes are still arriving, then prints what was received.

### Examples

```
BUS SERIAL 5 ENABLE 9600        # Bring up Serial5 at the module's baud rate first
AT 5 AT                         # Probe — HM-10 should answer "OK"
AT 5 AT+NAMEpreOBD              # Set BLE advertised name
AT 5 AT+BAUD0                   # Set HM-10 baud to 9600 (the BLE-friendly rate)
AT 5 AT+RESET                   # Reset module
```

### Notes

- The port must be active (`BUS SERIAL <port> ENABLE`) — `AT` will not auto-enable it
- HM-10 modules accept AT commands without line endings; HC-05 modules in AT mode require `\r\n` (which `AT` does not append — use a USB-serial adapter for HC-05 configuration)
- Stale bytes in the RX buffer are drained before the command is sent, so responses are not mixed with leftover module output

See [`BLUETOOTH_HARDWARE_GUIDE.md`](../guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md) for module-specific AT command references.

---

## Transport Configuration

Route control commands, sensor data output, and debug messages to different serial ports. Each message plane (CONTROL/DATA/DEBUG) has a **primary** and an optional **secondary** transport — both receive all output and both are polled for incoming commands.

### Commands

```
TRANSPORT STATUS                          # Show current transport routing
TRANSPORT <plane> <transport>             # Set primary transport for plane
TRANSPORT <plane> <transport> SECONDARY   # Set secondary transport for plane
TRANSPORT <plane> NONE SECONDARY          # Clear secondary transport
```

**Planes:** `CONTROL`, `DATA`, `DEBUG`

**Transports:** `USB_SERIAL`, `SERIAL1`–`SERIAL8`, `ESP32_BT`, `NONE`

Use `LIST TRANSPORTS` to see registered transports and their connection state.

### Simultaneous Multi-Port Control

Setting a secondary transport on the CONTROL plane lets two ports share control of the device — useful for keeping a laptop USB session open while also accepting commands from a Bluetooth module or other serial device.

```
BUS SERIAL 7 ENABLE 9600                  # Enable Serial7 (e.g. HM-10 at 9600 baud)
TRANSPORT CONTROL SERIAL7                 # HM-10 as primary control port
TRANSPORT CONTROL USB_SERIAL SECONDARY    # Laptop USB stays active alongside it
SAVE
```

After this, both ports accept commands and receive all responses. Responses always go to **both** primary and secondary — there is no exclusive ownership.

To revert to USB-only control:
```
TRANSPORT CONTROL USB_SERIAL              # Restore USB as primary
TRANSPORT CONTROL NONE SECONDARY          # Clear secondary
SAVE
```

### Notes

- Hardware serial ports must be enabled first (`BUS SERIAL <n> ENABLE <baud>`) before they can be assigned as a transport
- A serial port owned by ELM327 cannot be assigned as a transport; use `BUS SERIAL <n> ELM327 DISABLE` first
- Transport routing is persisted to EEPROM on `SAVE` and restored on boot

---

## Display Configuration

Configure display hardware and refresh rate.

**Note:** Unit preferences have moved to `SYSTEM UNITS`.

### Commands

```
DISPLAY STATUS                   # Show display hardware status
DISPLAY ENABLE                   # Enable display
DISPLAY DISABLE                  # Disable display
DISPLAY TYPE <LCD|OLED|NONE>     # Set display type (OLED: 128x64 SH1107, Feather F405 only)
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
SYSTEM DUMP JSON         # Export active configuration as JSON (copy/paste)
SYSTEM DUMP REGISTRY JSON  # Export firmware catalogs as JSON (Teensy/ESP32 only)
SYSTEM PINS              # Show all pin allocations (diagnostic)
INFO <pin>               # Query one pin (e.g., A0, CAN:0)
```

**SYSTEM STATUS** shows the number of active inputs, the ADC reference and
resolution, sea-level pressure, the sensor/alarm/display intervals, and the
log-filter and display settings.

**SYSTEM DUMP** shows complete configuration including all inputs, outputs, display, and system parameters.

**SYSTEM DUMP JSON** exports the active user configuration as JSON (inputs, system settings) for backup/restore.

**SYSTEM DUMP REGISTRY JSON** exports the firmware's compile-time catalogs (available applications, sensors, units, etc.) as a single JSON object. Equivalent to calling all `LIST … JSON` commands in one round-trip.

**SYSTEM PINS** displays pin allocation status organized by category:
```
=== Pin Allocation Status ===
Registry: 8 | Inputs: 4 | Relays: 2

System Pins:
  Pin  5: Button      - Mode Button
  Pin  3: Buzzer      - Buzzer

Bus Pins:
  Pin 18: Reserved    - Wire SDA
  Pin 19: Reserved    - Wire SCL

Input Pins:
  Pin A0: Input       - Oil Pressure (OIL)
  Pin A1: Input       - Coolant Temp (CLT)
  CAN:0:  Input       - Engine RPM (RPM)

Relay Pins:
  Pin 22: Output      - Relay 0
```

**INFO <pin>** queries a specific pin to check if it's allocated:
```bash
INFO A0        # Check if pin A0 is in use
INFO CAN:0     # Check CAN virtual pin 0
INFO 22        # Check digital pin 22
```

Example output:
```
Pin A0: Input     - Oil Pressure (OIL)
```
or if available:
```
Pin 13: Available
```

**Use cases:**
- Debugging pin conflicts before adding new inputs
- Understanding current pin usage across all subsystems
- Planning hardware wiring by checking available pins
- Troubleshooting configuration errors

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

# Check pin allocations
SYSTEM PINS                # Show all pins
INFO A0             # Check if A0 is available
INFO CAN:0          # Check CAN virtual pin

# Read sensors faster
SYSTEM INTERVAL SENSOR 50

# Check alarms more frequently
SYSTEM INTERVAL ALARM 100
```

**System Control:**
```bash
SYSTEM REBOOT                                  # Restart the device
SYSTEM RESET CONFIRM                           # Factory reset (erase config + reboot)
SYSTEM RESET CONFIG CONFIRM                    # Erase config, keep buses/serial/display, reboot
```

**System Control Examples:**
```bash
# Simple reboot
SYSTEM REBOOT

# Factory reset (requires CONFIRM for safety)
SYSTEM RESET CONFIRM                           # Erases all config except serial ports, and reboots

# Start the configuration over on a working unit
SYSTEM RESET CONFIG CONFIRM                    # Also keeps I2C/SPI/CAN buses and the display

```

**Comparison of Reset Commands:**

| Command | Clears Config? | Reboots? | Use Case |
|---------|---------------|----------|----------|
| `SYSTEM REBOOT` | No | Yes | Clean restart |
| `SYSTEM RESET CONFIRM` | Yes | Yes | Factory reset to defaults |
| `SYSTEM RESET CONFIG CONFIRM` | Yes, not the hardware setup | Yes | Start the configuration over, sensors and display still wired up |

`SYSTEM RESET CONFIRM` keeps the serial port settings (enabled ports, baud rates, framed mode) and the transport routing (which ports carry commands and data). They describe how the board is wired, and a reset sent over a Bluetooth bridge would otherwise cut the link it came over. Change them with `BUS SERIAL`.

`SYSTEM RESET CONFIG CONFIRM` erases inputs, rules, actions and settings (outputs, units, intervals, alarm acknowledge policy, SD log format, sea-level pressure, log levels) but also keeps the hardware setup: the whole `BUS` block (I2C/SPI selection and clocks, CAN buses, baud rates, CAN import mode, output enable and the ELM327 port) and the display (`DISPLAY`). SPI/I2C sensors and an LCD keep working without re-running `BUS` and `DISPLAY`.

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
| **RUN** | Read-only and diagnostic commands work. Commands that mutate persistent state, reboot, or factory-reset are blocked. |

### Available in RUN Mode

Whole commands (no gating):

- `HELP`, `?`
- `VERSION`
- `CONFIG`, `RUN` (mode switching)
- `INFO <pin>`
- `LIST *`
- `AT *` (raw serial passthrough — diagnostic)

Specific subcommands of mixed parents (read-only verbs only — destructive siblings are blocked):

- `SYSTEM STATUS`, `SYSTEM PINS`, `SYSTEM DUMP [JSON | REGISTRY JSON]`, `SYSTEM DIAG ...`
- `LOG STATUS`, `LOG TAGS`
- `RULE LIST`, `RULE <id>`
- `ACTION LIST`, `ACTION <id>`, `ACTION <id> ON|OFF|AUTO`
- `ALARM STATUS`, `ALARM SILENCE`

### Blocked in RUN Mode

Anything that writes EEPROM, mutates active config, factory-resets, or reboots:

- All `SET *` / `CLEAR`
- `OUTPUT *`, `DISPLAY *`, `TRANSPORT *`, `BUS *`
- `SAVE`, `LOAD`, `JSON`, `REBOOT`
- `SYSTEM RESET`, `SYSTEM REBOOT`, `SYSTEM SEA_LEVEL`, `SYSTEM UNITS`, `SYSTEM INTERVAL`
- `LOG LEVEL`
- `RULE ADD|DEL|RESET`, `RULE <id> ...` edits, `ACTION ADD|DEL`, `ACTION <id> RULE|NAME`
- `ALARM DURATION`, `ALARM SCOPE`
- `SCAN` (when applicable to the build)

Type `CONFIG` to switch to configuration mode and re-enable these.

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
- `SD:` - SD card storage (default if no prefix specified; currently the only destination)

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
```

**Workflow Example - Configuration Backup and Restore:**
```bash
# Save current configuration before making changes
SAVE SD:backup_before_changes.json

# Make configuration changes
RULE ADD A2 OUTSIDE 65 115

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
  Check: Destination prefix valid (SD)
```

### What Gets Saved

- All input configurations (pins, applications, sensors, names, alarms, calibrations)
- Output module enable/disable states and intervals
- Display type and unit preferences
- System parameters (sea level pressure, intervals)

### Best Practices

- Save after completing all configuration changes (not after each command)
- EEPROM has ~100,000 write cycles - avoid excessive saves
- Use SD file storage for archival and sharing configurations
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
- `HELP SET` - Input configuration (application, sensor, names, units, alarms, calibration)
- `HELP CONTROL` - Input control (enable, disable, clear, info)
- `HELP OUTPUT` - Output module configuration (CAN, RealDash, Serial, SD logging)
- `HELP BUS` - I2C / SPI / CAN / serial bus selection
- `HELP DISPLAY` - Display settings (LCD)
- `HELP TRANSPORT` - Message routing (control, data, debug messages)
- `HELP SYSTEM` - System configuration (units, sea level, intervals, diagnostics)
- `HELP CONFIG` - Persistence and modes (save, load, version)
- `HELP EXAMPLES` - Worked examples

**Examples:**
```
HELP                            # Show all categories
HELP SET                        # Show all SET commands
HELP SYSTEM                     # Show SYSTEM commands
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

**INFO <pin> ALARM** displays the input's canonical alarm **Rule** — the same
record `SET <pin> ALARM` writes — in the `RULE LIST` row format:
- Condition and thresholds, in the input's display units
- Severity and live rule state (`WARMUP` / `READY` / `ACTIVE` / `PARKED`)
- Warmup and debounce times, plus time in the current state
- Any Actions the alarm Rule drives

If the input has no alarm Rule, it says so and points at `SET <pin> ALARM`. Other
Rules watching the same input (warnings, control-only rules) are listed by
`RULE LIST`.

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
LIST SENSORS THERMISTOR          # Show thermistor sensors
LIST SENSORS TEMPERATURE         # Show all temperature sensors
```

`VERSION` also says why the board last restarted, e.g. `Last reset: power-on`.
After an unexpected restart by the watchdog (the firmware froze), Teensy 4
boards add what it was doing and where:

```
Last reset: WATCHDOG in SD (SRSR 0x10 WRSR 0x2), hung at PC 0x6000F69A LR 0x22FB7 after 812 s
```

`in SD` names the slow operation in progress (`SD`, `EEPROM`, `BOOT` during
startup, `LOOP` for everything else). The PC is the code that was stuck; please
include this line when reporting an unexpected restart. Developers can map it to
a function with `tools/watchdog_bench.py where 0x6000F69A`.

**Note:** For complete system dumps, use `SYSTEM DUMP` or `SYSTEM DUMP JSON` (see [System Configuration](#system-configuration)).

---

## Diagnostics

```
SYSTEM DIAG                  # Run all diagnostics (integrity + hardware + inputs)
SYSTEM DIAG INTEGRITY        # Validate the firmware's command tables
SYSTEM DIAG HW               # Probe configured peripherals (SD card, display, buses, ...)
SYSTEM DIAG INPUTS           # Per-input fault detection (open / short / out of range)
SYSTEM DIAG INPUTS VERBOSE   # Same, with the raw evidence for each verdict
SYSTEM DIAG PINS             # Sweep all analog pins with live raw/voltage readings
```

`SYSTEM DIAG PINS` is the quickest way to find which pin a sensor is actually
wired to. `SYSTEM DIAG INPUTS` explains why an input reads `ERROR` or `NaN`.
A `FAIL` from `SYSTEM DIAG INTEGRITY` indicates a firmware bug — please file an
issue with the output. All `SYSTEM DIAG` commands work in both CONFIG and RUN mode.

Developer bench builds (built with `-D INCLUDE_WATCHDOG_STALL=1`, never in a
release image) also have `SYSTEM DIAG WATCHDOG STALL <ms> [SCOPE <ms>]`. It stops
feeding the watchdog for `<ms>` (optionally inside a widened watchdog scope)
to check that it still resets the board: after 2 s in RUN, 8 s in CONFIG. It
resets the board on purpose. `tools/watchdog_bench.py stall` runs the checks.

---

## Quick Reference Examples

### Complete Setup Workflow

```
CONFIG
SET 6 CHT MAX6675
SET A0 OIL_TEMP GENERIC_NTC_BETA
SET A0 CAL BETA 2490 3950 10000 25
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A3 OIL_PRESSURE GENERIC_PRESSURE_150PSI
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
RULE ADD A2 OUTSIDE 60 120
RULE ADD A3 OUTSIDE 0.5 7.0
BUS CAN OUTPUT CAN1 ENABLE 500000
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
SET A0 OIL_TEMP GENERIC_NTC_STEINHART
SET A0 CAL STEINHART 2490 1.129e-3 2.341e-4 8.775e-8
SAVE
RUN
```

### Configure Hall Speed Sensor

```
CONFIG
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 CAL SPEED 100 2008 3.73 2000 300
SET 2 ABBR SPEED
SAVE
RUN
```

---

## Pin Notation

| Type | Format | Examples |
|------|--------|----------|
| Digital | Number | `0`, `1`, `6`, `13` |
| Analog | A + Number | `A0`, `A1`, `A6`, `A15` |
| I2C | `I2C:<n>` | BME280 and other I2C sensors (`I2C:0`, `I2C:1`, ...) |
| CAN import | `CAN` / `CAN:<n>` | `SET CAN <pid>` imports a PID; `CAN:0` addresses it afterwards |
| SPI CS | Number | Use any digital pin for chip select |

---

## Error Messages

If a command fails, you'll see helpful error messages:

```
ERROR: Unknown output 'CANBUS'

ERROR: SET requires CONFIG mode
  Type CONFIG to enter configuration mode
```

Most errors include a usage line or a hint pointing to the `LIST` command that shows valid values.

---

## See Also

- **[Quick Reference](../getting-started/QUICK_REFERENCE.md)** - Command cheat sheet
- **[Advanced Calibration Guide](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensor calibration
- **[Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md)** - Alarm state machine
- **[CONFIG/RUN Mode Guide](../guides/configuration/CONFIG_RUN_MODE_GUIDE.md)** - Configuration workflow

