# Alarm System Guide

preOBD warns you when a reading goes out of range: the status LED turns yellow for
a **warning** and red with the buzzer for an **alarm**. The LCD and Bluetooth
clients show the same state.

Alarms are built on the same **Rules** that drive relays (see the
[Relay Control Guide](../outputs/RELAY_CONTROL.md)). An alarm is a Rule whose
severity is `WARNING` or `ALARM`.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Default Alarms](#default-alarms)
3. [Setting an Input's Alarm](#setting-an-inputs-alarm)
4. [More Rules per Input](#more-rules-per-input)
5. [Warmup and Debounce](#warmup-and-debounce)
6. [Warnings Before the Alarm](#warnings-before-the-alarm)
7. [Acknowledging an Alarm](#acknowledging-an-alarm)
8. [When a Sensor Fails](#when-a-sensor-fails)
9. [Build and Wiring](#build-and-wiring)
10. [Troubleshooting](#troubleshooting)

---

## Quick Start

```
CONFIG
SET A1 OIL_PRESSURE VDO_5BAR_CURVE     # creates a default oil-pressure alarm
SET A1 ALARM MIN 0.7                   # alarm below 0.7 bar (~10 PSI) instead
SET A2 COOLANT_TEMP VDO_120C_STEINHART
SET A2 ALARM MAX 105                   # alarm above 105°C
SAVE
RUN
```

**Limits are always in base units** — °C, bar, volts, RPM, km/h — even if the
input displays °F or PSI.

`INFO <pin> ALARM` shows an input's alarm; `RULE LIST` shows every Rule.

---

## Default Alarms

`SET <pin> <application> <sensor>` creates a default alarm for the applications
below (an `ALARM`-severity band). Other applications start with no alarm.

| Application | Default alarm band (base units) | Warmup | Debounce |
|---|---|---|---|
| `CHT` | -1 to 260 °C | 30 s | 2 s |
| `EGT` | -1 to 600 °C | 20 s | 2 s |
| `COOLANT_TEMP` | -1 to 100 °C | 60 s | 5 s |
| `OIL_TEMP` | -1 to 150 °C | 60 s | 5 s |
| `TCASE_TEMP` | -1 to 100 °C | 60 s | 5 s |
| `TRANS_TEMP` | -1 to 130 °C | 60 s | 5 s |
| `DIFF_TEMP` | -1 to 120 °C | 60 s | 5 s |
| `GEAR_OIL_TEMP` | -1 to 120 °C | 60 s | 5 s |
| `OIL_PRESSURE` | 1 to 5 bar | 5 s | 1 s |
| `FUEL_LEVEL` | 10 to 100 % | 2 s | 5 s |
| `COOLANT_LEVEL`, `OIL_LEVEL` | 0 to 1 (see note) | 5 s / 10 s | 2 s / 3 s |

A band alarms when the value goes **outside** it; the `-1` lower edges mean the
temperature alarms are effectively over-temperature only.

> **Note:** for float switches (`COOLANT_LEVEL`, `OIL_LEVEL`) the default band
> currently never trips (a known firmware issue). Until it's fixed, set the alarm
> yourself: `SET <pin> ALARM MIN 0.5`.

Every application also has a warmup and debounce time that applies to the Rules
you add for it; `LIST APPLICATIONS` lists them all.

---

## Setting an Input's Alarm

`SET <pin> ALARM` edits the input's single alarm Rule (creating it if needed):

```
SET <pin> ALARM <min> <max>     # alarm outside the band
SET <pin> ALARM MIN <v>         # alarm below v (e.g. oil pressure)
SET <pin> ALARM MAX <v>         # alarm above v (e.g. temperature)
SET <pin> ALARM DISABLE         # remove the input's alarm
```

Examples:

```
SET A3 ALARM MIN 0.7            # oil pressure below 0.7 bar
SET 6 ALARM MAX 260             # CHT above 260°C
SET A6 ALARM 11.5 15.0          # battery outside 11.5-15.0 V
```

Changing the limits keeps the Rule's id, its warmup/debounce and any relays
linked to it.

---

## More Rules per Input

For anything beyond one alarm per input — a separate warning level, hysteresis,
an `INFO` log entry, or a silent Rule that only drives a relay — use `RULE`
directly:

```
RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>] [INFO|WARNING|ALARM|NONE]
RULE ADD <pin> OUTSIDE <lo> <hi> [HYST <w>] [INFO|WARNING|ALARM|NONE]
RULE <id> LIMIT ABOVE|BELOW <limit>       # move a Rule's limit
RULE <id> SEVERITY WARNING                # change its tier
RULE <id> DISABLE / ENABLE                # park / re-arm
RULE DEL <id>
RULE RESET <id>                           # back to the application default
```

| Severity | Effect |
|----------|--------|
| `NONE` | Nothing shown; used to drive relays |
| `INFO` | Logged / reported to Bluetooth clients only |
| `WARNING` | Yellow LED, LCD and Bluetooth; no buzzer |
| `ALARM` | Red LED and **buzzer**, plus LCD and Bluetooth |

Example — a warning at 100°C and an alarm at 110°C on the same input:

```
RULE ADD A2 ABOVE 100 WARNING
SET A2 ALARM MAX 110
```

---

## Warmup and Debounce

A Rule doesn't fire the instant a reading crosses its limit. It moves through
these states (shown by `RULE LIST` and `INFO <pin> ALARM`):

| State | Meaning |
|-------|---------|
| `INIT` | Just armed (1 s) |
| `WARMUP` | Waiting out the warmup time, and for a valid reading |
| `READY` | Monitoring |
| `ACTIVE` | The condition has held for the debounce time — alarm/warning is on |
| `PARKED` | Disabled with `RULE <id> DISABLE` |

- **Warmup** stops alarms during start-up — oil pressure takes a few seconds to
  build and a cold engine is below any "too cold" limit. It starts when the Rule
  is armed (at power-up, or after an edit that re-arms it).
- **Debounce** stops a brief spike or electrical noise from tripping an alarm: the
  reading must stay out of range for the whole debounce time.

Rules created by `SET <pin> <application> ...` use the application's times (table
above). Rules created with `RULE ADD` use a 3 s warmup and 500 ms debounce.
`RULE <id>` shows a Rule's times. They can be changed through a JSON import
(`warmupMs`, `debounceMs`, up to 65 s) — see the
[JSON Configuration Guide](JSON_CONFIGURATION_GUIDE.md).

---

## Warnings Before the Alarm

An `ALARM` Rule also raises a **warning** as the value approaches its limit, so the
LED turns yellow before the buzzer sounds. By default the warning edge is 90% of
the limit (`AUTO`). Set it explicitly, or turn it off:

```
RULE ADD A0 ABOVE 230 WARN 210        # warn at 210, alarm at 230
RULE <id> WARN NONE                   # no warning tier
SET A0 ALARM WARN 210                 # same, via the input's alarm
```

An explicit warning edge must be on the safe side of the limit.

---

## Acknowledging an Alarm

A short press of the mode button — or `ALARM SILENCE` — acknowledges every alarm
that is sounding at that moment. Acknowledgement is per alarm: a *different*
alarm tripping afterwards still sounds.

```
ALARM SILENCE                 # acknowledge (works in RUN and CONFIG mode)
ALARM STATUS                  # what's sounding, what's acknowledged, the policy
ALARM DURATION <secs|CLEAR>   # CONFIG: how long an acknowledgement lasts
ALARM SCOPE <BUZZER|ALL>      # CONFIG: silence just the buzzer, or the LED blink too
```

- **Default: `DURATION CLEAR`** — the alarm stays silent until its reading
  returns to normal; if it trips again later, you hear it again. One press
  covers a stuck sensor for the whole drive.
- **`DURATION <5-630>`** (seconds, in steps of 5) — the alarm re-sounds after that
  long if it is still active.
- **`SCOPE BUZZER`** (default) silences the buzzer and keeps the LED blinking;
  **`SCOPE ALL`** also stops the blink (steady red).

`ALARM DURATION` and `ALARM SCOPE` need `SAVE`.

`OUTPUT Alarm DISABLE` turns off the buzzer and LED alarm output entirely (Rules
still evaluate and Bluetooth clients still see them).

---

## When a Sensor Fails

A disconnected or shorted sensor reads `NaN`, shown as `ERR` on the LCD. A Rule
never fires on `NaN` — a broken wire shouldn't sound the "oil pressure low"
alarm. Instead, after 2 seconds of invalid readings (or the warmup time, if
longer), Bluetooth clients are told the input has a **sensor fault**.

So a failed sensor does **not** sound the buzzer. Watch for `ERR` on the display,
and use `SYSTEM DIAG INPUTS` to see why an input is failing.

Relays driven by a failed sensor go to their failsafe state instead — see the
[Relay Control Guide](../outputs/RELAY_CONTROL.md#when-the-sensor-fails).

---

## Build and Wiring

The alarm engine is included when the board's `board.h` declares
`SUPPORTS_ALARMS`. The buzzer, the mode (silence) button and the status LED are
optional hardware; each is enabled by wiring its pin in your
`boards/<board>/wiring.ini`:

```ini
[<board>]
wiring =
    -D BUZZER_PIN=3
    -D MODE_BUTTON_PIN=2
    -D RGB_PIN_R=6
    -D RGB_PIN_G=5
    -D RGB_PIN_B=4
```

See the [Build Configuration Guide](BUILD_CONFIGURATION_GUIDE.md) and
[LED Indicator Guide](../hardware/LED_INDICATOR_GUIDE.md).

In CONFIG mode the buzzer stays silent, so bench testing can't set it off.

---

## Troubleshooting

**An alarm doesn't trigger**
- `INFO <pin> ALARM` — is there an alarm Rule, and what state is it in?
  `WARMUP` means it's still waiting; `READY` means the condition isn't met.
- Are the limits in base units? `SET A1 ALARM MIN 10` on oil pressure means 10 **bar**.
- Are you in RUN mode? The buzzer is silent in CONFIG mode.
- Is the reading valid? A sensor showing `ERR` never trips a Rule.
- Is the alarm output on? `OUTPUT STATUS` should show `Alarm` enabled.
- Has it been acknowledged? `ALARM STATUS`.

**An alarm sounds at start-up**
- Its warmup is too short for your engine. `RULE <id>` shows the warmup; raise it
  with a JSON import, or loosen the limit.

**Brief spikes trip the alarm**
- Increase the Rule's debounce (JSON `debounceMs`), and check the sensor's wiring
  and grounding.

**The buzzer doesn't stop when I press the button**
- The button acknowledges only the alarms sounding at that moment; a new alarm
  sounds again. Check `ALARM STATUS`, and `MODE_BUTTON_PIN` in your `wiring.ini`.

---

## See Also

- [Serial Command Reference](../../reference/SERIAL_COMMANDS.md#alarm-configuration)
- [Relay Control Guide](../outputs/RELAY_CONTROL.md) — Rules that drive relays
- [Config/Run Mode Guide](CONFIG_RUN_MODE_GUIDE.md)
