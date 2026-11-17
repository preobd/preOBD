# Relay Output Control

preOBD can switch 12V loads through a relay — cooling fans, electric water pumps,
warning lights, heaters, solenoids — based on sensor readings.

A relay is set up with two pieces of the **control engine**:

- a **Rule** watches an input and decides *when* (threshold + hysteresis), and
- an **Action** of type `RELAY` is the output pin that the Rule drives.

The same engine runs alarms, so a Rule can drive a relay, sound the buzzer, or both.

> The older `RELAY ...` command is deprecated. It now only prints a pointer to
> `RULE` / `ACTION`; see [Migrating from `RELAY`](#migrating-from-relay) at the end.

## Table of Contents
- [Hardware Setup](#hardware-setup)
- [Quick Start: Cooling Fan](#quick-start-cooling-fan)
- [How Rules Switch a Relay](#how-rules-switch-a-relay)
- [Command Reference](#command-reference)
- [Use Cases](#use-cases)
- [Troubleshooting](#troubleshooting)
- [Safety Considerations](#safety-considerations)
- [Migrating from `RELAY`](#migrating-from-relay)

---

## Hardware Setup

### Keep the relay OFF while the pin floats

From power-up until preOBD claims the pin, and again after a reset or after you
delete the Action, the MCU pin is **not driven**: it floats (high impedance).
Nothing in software can hold a relay off during that time, so the wiring has to.
**The relay driver must be OFF while its input pin floats.**

- **Pull the driver's input to its OFF level, with 10 kΩ or less.**
  - Low-side switch (N-channel MOSFET, NPN transistor, most bare relay drivers):
    a **pull-down** from the input to GND.
  - High-side switch (P-channel MOSFET or PNP switching +12 V): **don't wire it
    to the pin directly.** A 3.3 V or 5 V pin can't pull a 12 V P-FET's gate
    high enough to turn it off, and its gate pull-up goes to 12 V. Put an NPN
    or N-channel stage between them: the pin drives that stage (low-side, so
    it gets the pull-down above), and the P-FET's own gate pull-up to its
    source stays on the 12 V side, never on the pin.
  - Some pins have a pull of their own at reset. ESP32-S3 GPIO0 has a ~45 kΩ
    pull-up, so even a 10 kΩ pull-down leaves about 0.6 V on it, enough to
    partly turn on a transistor or an opto input. Avoid the pins in the table
    below.
- **Never pull an MCU pin above its own supply.** Teensy, ESP32 and STM32 pins are
  3.3 V; a pull-up to 5 V (or 12 V) damages them. The Mega is a 5 V part.
- Many relay modules already have this pull resistor on board. Check your module
  before adding one.

### Relay modules

**Typical relay module** (like common 5V/12V relay boards):

```
┌─────────────────────┐
│   Relay Module      │
├─────────────────────┤
│ VCC  ──→ see below  │  Input-side power
│ GND  ──→ GND        │  Ground
│ IN   ──→ GPIO Pin   │  Control signal (e.g., pin 9)
│                     │
│ COM  ──┐            │  Common (to fused +12V)
│ NO   ──┼──→ Load    │  Normally Open: load ON while the coil is energized
│ NC   ──┘            │  Normally Closed: load ON while the coil is off
└─────────────────────┘
```

Many relay modules are **active-low** (the relay turns on when `IN` is pulled to
GND). Add `INVERT` to `ACTION ADD RELAY` for those.

**Active-low optocoupler modules on a 3.3 V board** (Teensy, ESP32, Feather
STM32): the module's `IN` is pulled up to its `VCC`. If `VCC` is 5 V, the module
puts 5 V on your 3.3 V pin, and a 3.3 V "high" may not turn the opto fully off.

- If the module has a **`JD-VCC` jumper**: remove it, power `VCC` from the
  board's **3.3 V**, and power `JD-VCC` (the coil side) from 5 V or 12 V as the
  module requires.
- If it has **no `JD-VCC` jumper**: drive `IN` through an NPN transistor or a
  level shifter, not straight from the pin.

On the Mega (5 V) the module can be powered from 5 V directly.

### NO or NC: OFF is de-energized, not necessarily safe

When preOBD resets or loses power, every relay drops to **de-energized**. On the
**NO** contact that switches the load **off**. Decide per load whether that is
what you want:

- Most loads (a warning light, a heater): wire to **NO**; de-energized is off.
- A load that must **keep running** when the controller resets or loses power
  (a cooling fan, a fuel or water pump): wire to **NC**, so de-energized leaves
  it on. The relay is then energized to switch the load *off*, so the Action's
  sense is reversed: add `INVERT` (or, for an active-low module that already
  needs `INVERT`, leave it off).

This is separate from `FAILSAFE`, which only covers a failed *sensor* while
preOBD is running.

### Example: Cooling Fan Wiring

**Components needed:**
- 12V electric cooling fan (with an automotive relay or a module rated for it)
- Relay module with optocoupler isolation and a `JD-VCC` jumper
- preOBD running on a Teensy 4.1

**Wiring:**
1. **Relay module power**: remove the `JD-VCC` jumper; `VCC` → Teensy 3.3 V,
   `JD-VCC` → 5 V, `GND` → GND
2. **Control signal**: `IN` → Teensy pin 9 (an active-low module: `INVERT` on `NO`, no `INVERT` on `NC`)
3. **Fan power**:
   - Fused +12V → relay `COM`
   - Relay `NC` → fan positive (so the fan runs if preOBD resets; see above),
     or `NO` if you'd rather it stopped
   - Fan negative → battery ground

With `NC` and an active-low module the two inversions cancel:
`ACTION ADD RELAY 9 FAILSAFE ON` without `INVERT`. The coil is then energized
whenever the fan is off, so the module draws coil current most of the time, and
the fan also stops whenever preOBD holds the Action OFF: when it claims the pin
at boot, and in CONFIG mode for a rule-driven Action.

### GPIO Pin Selection

Use a pin nothing else uses: **`SYSTEM PINS`** lists every pin and what holds it.
If you pick a pin another function already holds, the Action is refused and
shows **`NOT DRIVEN`** in `ACTION LIST` (and in the app): preOBD never touches
that pin for it. Move the relay (or the other function) to a free pin.

**Avoid pins the chip drives or pulls at reset**, before preOBD runs:

| Board | Avoid | Why |
|---|---|---|
| Arduino Mega | 13 | The bootloader blinks the LED on it at reset |
| Arduino Mega | 0, 1 | USB serial |
| Arduino Mega | 50–53 | SPI (53 is SS, held by the SPI bus) |
| ESP32-S3 | 0, 3, 45, 46 | Strapping pins, pulled or sampled by the chip at reset |
| ESP32-S3 | 43, 44 | Boot UART: the ROM prints on it at reset |
| ESP32-S3 | 19, 20 | USB |
| ESP32-S3 | 26–32 (and 33–37 with octal PSRAM) | Flash and PSRAM |
| Feather STM32F405 | PA13, PA14, PA15, PB3, PB4 (chip pin names) | JTAG/SWD: pulled or driven at reset |
| Teensy | 13 | The on-board LED, and SPI SCK |

On Teensy 4.x the remaining pins reset to an input with a weak *keeper* that
holds whatever level the pin last had, and a released pin is a plain input; a
10 kΩ pull to the OFF level sets it either way.

**Recommended pins** for relays:
- Teensy 4.x: 9, and 32, 33, 36, 37 on a 4.1. Avoid 2–6 and 8 (the buzzer,
  button, RGB LED, nRF flow control and test-mode pins in the reference
  `wiring.ini.example`), 7/8 (Serial2), 10–13 (SPI), 22/23 (CAN1), 24/25
  (Wire2) and 26/27 (SPI1). Your own wiring may differ: check `SYSTEM PINS`
- Arduino Mega: 22, 24, 26, 28

---

## Quick Start: Cooling Fan

Fan on above 90°C, off again below 85°C. Coolant sensor on A2, relay on pin 9.

```
CONFIG
SET A2 COOLANT_TEMP VDO_120C_STEINHART   # the sensor (also creates its default alarm Rule)
RULE ADD A2 ABOVE 90 HYST 5 NONE         # prints: Rule #<r> added
ACTION ADD RELAY 9                      # prints: Action #<a> added
ACTION <a> RULE <r>                      # the relay now follows the Rule
SAVE
RUN
```

Use the ids the firmware prints — `SET A2 ...` already created an alarm Rule for
the input, so your fan Rule is usually not `#0`. `RULE LIST` and `ACTION LIST`
show every id.

`NONE` at the end of `RULE ADD` makes this a **control-only** Rule: it switches
the relay but does not light the warning LED or sound the buzzer. The input's
own alarm (from `SET <pin> ALARM`) is unaffected.

**Rules only switch relays in RUN mode.** In CONFIG mode rule-driven Actions are
held off so a sensor on the bench can't start a fan or pump while you are wiring it.

---

## How Rules Switch a Relay

### Thresholds and hysteresis

| Rule | Turns ON when | Turns OFF when |
|------|---------------|----------------|
| `ABOVE <limit> HYST <w>` | value rises above `limit` | value falls below `limit - w` |
| `BELOW <limit> HYST <w>` | value falls below `limit` | value rises above `limit + w` |
| `OUTSIDE <lo> <hi> HYST <w>` | value leaves the `lo..hi` band | value is back inside by `w` |

```
Temperature (°C)            RULE ADD A2 ABOVE 90 HYST 5
    ↑
 95 |                    ┌────────────
 90 |            ┌───────┘            ← turns ON above 90
 85 |════════════╝                    ← turns OFF below 85
 80 |
    └──────────────────────────────→ Time
         OFF    |    ON    |   OFF
```

The hysteresis band stops the relay chattering when the reading hovers around
the limit.

**Units:** limits are always in base units — °C, bar, volts, RPM, km/h — even if
the input displays °F or PSI.

### Timing

A Rule doesn't act the instant a reading crosses the limit:
- **Warmup** — after power-up (or after the Rule is armed) it waits before acting.
  Rules created with `RULE ADD` use a 3 s warmup; the alarm Rule seeded by
  `SET <pin> <application> ...` uses the application's warmup.
- **Debounce** — the condition must hold for a short time (500 ms by default)
  before the Rule changes state.

`RULE <id>` shows a Rule's warmup and debounce. They can be changed through a
JSON import (`warmupMs`, `debounceMs`); see the
[JSON Configuration Guide](../configuration/JSON_CONFIGURATION_GUIDE.md).

### When the sensor fails

If the Rule's input stops producing a trustworthy reading (disconnected,
shorted, pegged at the rail) for **2 seconds**, the Action goes to its
**failsafe** state. A shorter dropout (one bad sample, a missed CAN frame)
leaves the relay as it was: a running fan keeps running. The 2 seconds also
apply at startup, so a relay whose sensor is dead from power-on reaches its
failsafe about 2 seconds after the first reading. During a Rule's warmup the
app may not yet show a sensor fault for that input (it waits out the warmup so
a CAN sensor that hasn't started sending isn't flagged), but the relay is
already in its failsafe state.

A sensor whose normal output reaches a supply rail (a 0-5 V sender reading
0 V, a float switch) can't be told apart from a fault, so it never triggers
the failsafe.

- `FAILSAFE OFF` (default) — the relay turns off. Right for a fuel pump.
- `FAILSAFE ON` — the relay turns on. Right for a cooling fan: if you lose the
  temperature sensor, you'd rather the fan ran.

```
ACTION ADD RELAY 9 FAILSAFE ON
```

### Several Rules, one relay

An Action can follow several Rules and is on while **any** of them is active —
for example one fan demanded by coolant temperature *or* transmission
temperature. A healthy Rule that is asking for the output outranks a sibling
whose sensor has failed; the failsafe applies only when no healthy Rule is
asking.

### Manual override

`ACTION <id> ON` / `ACTION <id> OFF` force the relay, outranking both the Rule
and the failsafe. `ACTION <id> AUTO` hands control back to the Rule. The override
works in RUN mode and is not saved — after a reboot the relay is back under Rule
control.

---

## Command Reference

### Rules

```
RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>] [INFO|WARNING|ALARM|NONE]
RULE ADD <pin> OUTSIDE <lo> <hi> [HYST <w>] [INFO|WARNING|ALARM|NONE]
RULE LIST                          # all Rules, their state and severity
RULE <id>                          # one Rule in detail (limits, warmup, debounce, linked Actions)
RULE <id> LIMIT ABOVE|BELOW <limit>   # move the limit, keeping the id and links
RULE <id> SEVERITY INFO|WARNING|ALARM|NONE
RULE <id> ENABLE|DISABLE           # park / re-arm without deleting
RULE DEL <id>
```

Severity: `NONE` = control only; `INFO` = log only; `WARNING` = indicator LED;
`ALARM` = indicator + buzzer. Use `NONE` for a Rule that only drives a relay.

### Actions

```
ACTION ADD RELAY <pin> [FAILSAFE ON|OFF] [INVERT]
ACTION <id> RULE <ruleId>          # follow exactly this Rule (NONE = manual only)
ACTION <id> RULE ADD <ruleId>      # also follow another Rule
ACTION <id> RULE REMOVE <ruleId>   # stop following one Rule
ACTION <id> NAME <text>            # label, e.g. Cooling Fan (no text clears it)
ACTION <id> ON|OFF                 # manual override (RUN mode)
ACTION <id> AUTO                   # release the override
ACTION LIST                        # every Action and its current state
ACTION <id>                        # one Action in detail
ACTION DEL <id>
```

`RULE ADD/DEL`, `RULE <id> LIMIT/SEVERITY/ENABLE/DISABLE`, `ACTION ADD/DEL`,
`ACTION <id> RULE` and `ACTION <id> NAME` need CONFIG mode. `SAVE` makes them
permanent.

### Capacity

Most boards hold up to 64 Rules and 64 Actions (sharing EEPROM space with the
inputs). The **Arduino Mega holds only 2 of each** because of its 8 KB of RAM —
and each input's default alarm counts as one Rule.

---

## Use Cases

### 1. Electric Cooling Fan (fails ON)

```
SET A2 COOLANT_TEMP VDO_120C_STEINHART
RULE ADD A2 ABOVE 90 HYST 5 NONE           # Rule #r
ACTION ADD RELAY 9 FAILSAFE ON            # Action #a
ACTION <a> RULE <r>
ACTION <a> NAME Cooling Fan
SAVE
```

- Below 85°C: fan off
- 85-90°C: fan keeps its current state (hysteresis)
- Above 90°C: fan on
- Sensor fails: fan on

### 2. Low Oil Pressure Warning Light

```
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
RULE ADD A3 BELOW 0.5 HYST 0.5 NONE        # on below 0.5 bar, off above 1.0 bar
ACTION ADD RELAY 32
ACTION <a> RULE <r>
SAVE
```

### 3. Electric Water Pump

```
SET A2 COOLANT_TEMP VDO_120C_STEINHART
RULE ADD A2 ABOVE 80 HYST 5 NONE           # on above 80°C, off below 75°C
ACTION ADD RELAY 9 FAILSAFE ON
ACTION <a> RULE <r>
SAVE
```

### 4. Two-Speed Fan

Two relays on the same sensor at different temperatures:

```
RULE ADD A2 ABOVE 85 HYST 5 NONE           # low speed, Rule #r1
RULE ADD A2 ABOVE 95 HYST 5 NONE           # high speed, Rule #r2
ACTION ADD RELAY 9 FAILSAFE ON            # low-speed relay, Action #a1
ACTION ADD RELAY 32 FAILSAFE ON            # high-speed relay, Action #a2
ACTION <a1> RULE <r1>
ACTION <a2> RULE <r2>
SAVE
```

### 5. One Fan, Two Sensors

Fan on if coolant is above 95°C **or** transmission fluid is above 110°C:

```
RULE ADD A2 ABOVE 95 HYST 5 NONE           # coolant, Rule #r1
RULE ADD A4 ABOVE 110 HYST 5 NONE          # trans temp, Rule #r2
ACTION ADD RELAY 9 FAILSAFE ON            # Action #a
ACTION <a> RULE <r1>
ACTION <a> RULE ADD <r2>
SAVE
```

### 6. Fan Plus an Alarm

The fan comes on at 90°C; the buzzer sounds only if the coolant reaches 110°C
anyway:

```
SET A2 ALARM MAX 110                       # the input's alarm (buzzer)
RULE ADD A2 ABOVE 90 HYST 5 NONE           # the fan (silent)
ACTION ADD RELAY 9 FAILSAFE ON
ACTION <a> RULE <r>
SAVE
```

### 7. Manual Testing

In RUN mode:

```
ACTION <a> ON          # relay should click and the load should run
ACTION <a> OFF
ACTION <a> AUTO        # back to Rule control
```

---

## Troubleshooting

### Relay doesn't activate

1. **Are you in RUN mode?** Rule-driven Actions are held off in CONFIG mode. Type `RUN`.
2. **Is the Action linked?** `ACTION <a>` should list the Rule it follows.
3. **Is the Rule active?** `RULE <r>` shows its state. `WARMUP` means it is still
   waiting after power-up; `READY` means the condition isn't met yet; `ACTIVE`
   means it is asking for the output.
4. **Are the limits in the right units?** Limits are °C / bar, not °F / PSI.
5. **Does the sensor read?** `INFO <pin>` — a `NaN`/`ERROR` value puts the Action
   in its failsafe state.
6. **Is it wiring?** `ACTION <a> ON` in RUN mode. If the relay doesn't click,
   check the wiring and whether the module needs `INVERT`.

### `ACTION LIST` shows `NOT DRIVEN`

Another function holds the Action's pin, so preOBD never drives it: the relay
reads OFF and `ACTION <a> ON` is refused. `SYSTEM PINS` shows what holds the
pin. Delete the Action and add it on a free pin (or move the other function).

### Relay clicks at power-up or reset

The driver input floats before preOBD claims the pin. Add a pull resistor to the
OFF level and check the pin isn't one the chip drives at reset; see
[Keep the relay OFF while the pin floats](#keep-the-relay-off-while-the-pin-floats).

### Relay is on when it should be off (or the other way round)

- Active-low relay module: delete and re-add the Action with `INVERT`.
- A manual override is still in effect: `ACTION <a> AUTO`.
- The sensor has failed and the Action is `FAILSAFE ON`: check `INFO <pin>`.

### Relay chatters (rapid ON/OFF)

Increase the hysteresis — e.g. `RULE ADD A2 ABOVE 90 HYST 5` rather than `HYST 1`.
Typical values: temperature 5-10°C, pressure 0.2-0.5 bar, voltage 0.5-1.0V.

### Configuration lost after power cycle

Run `SAVE` after changes. Manual `ON`/`OFF` overrides are intentionally never saved.

---

## Safety Considerations

### Electrical
1. **Wire every relay OFF while its pin floats**, and choose NO or NC per load
   ([see above](#keep-the-relay-off-while-the-pin-floats))
2. **Use isolated relay modules** with optocouplers
3. **Fuse the load circuit** appropriately for the fan/pump
4. **Check relay ratings** (10A+ relays for high-current loads, or drive an
   automotive relay from the module)
5. **Suppress back-EMF** from inductive loads (flyback diodes)

### Software
1. Once preOBD claims a relay pin it sets it OFF before enabling the output, and
   holds rule-driven relays off in CONFIG mode. Before that, and across a reset, only your
   wiring keeps the relay off
2. Rules wait out their warmup before switching anything
3. Choose `FAILSAFE ON` or `OFF` per load for what should happen if a sensor fails
4. Manual overrides are RAM-only and cleared by a reboot

---

## Migrating from `RELAY`

| Old `RELAY` command | Rules & Actions equivalent |
|---------------------|----------------------------|
| `RELAY 0 PIN 23` | `ACTION ADD RELAY 23` |
| `RELAY 0 INPUT A2` + `THRESHOLD 90 85` + `MODE AUTO_HIGH` | `RULE ADD A2 ABOVE 90 HYST 5 NONE`, then `ACTION <a> RULE <r>` |
| `RELAY 1 INPUT A3` + `THRESHOLD 0.5 1.0` + `MODE AUTO_LOW` | `RULE ADD A3 BELOW 0.5 HYST 0.5 NONE`, then `ACTION <a> RULE <r>` |
| `MODE MANUAL_ON` / `MANUAL_OFF` | `ACTION <a> ON` / `ACTION <a> OFF` |
| Back to an `AUTO_*` mode | `ACTION <a> AUTO` |
| `RELAY <n> DISABLE` | `ACTION DEL <a>` (or `RULE <r> DISABLE` to park the Rule) |
| `RELAY LIST` / `RELAY <n> STATUS` | `ACTION LIST` / `ACTION <a>` and `RULE LIST` / `RULE <r>` |

Differences worth knowing:
- Old relay thresholds followed the input's display units; Rule limits are always
  base units (°C, bar).
- Actions have a sensor-loss failsafe the old relays didn't.
- There is no fixed limit of two relays (except on the Mega).

---

## See Also

- [Alarm System Guide](../configuration/ALARM_SYSTEM_GUIDE.md) — Rule states, warmup and acknowledgement
- [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#rules--actions-control-engine)
- Type `HELP` in the serial console for on-device help
