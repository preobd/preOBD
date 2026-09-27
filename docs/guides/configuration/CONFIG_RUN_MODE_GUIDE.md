# Config/Run Mode Guide

preOBD has two operating modes:

- **CONFIG mode** — configuration is unlocked. Use it to set up or change inputs,
  alarms, outputs and buses.
- **RUN mode** — configuration is locked. This is normal operation in the car.

CONFIG mode is a **write gate**, not a pause: sensors keep reading and the live
outputs keep working, so you can watch readings change while you configure.
What CONFIG mode *does* hold back is anything physical or recorded — see
[What changes in CONFIG mode](#what-changes-in-config-mode).

---

## Switching Modes

| To enter | How |
|----------|-----|
| CONFIG | Type `CONFIG` |
| CONFIG | Hold the mode button while powering on |
| CONFIG | Automatic on first boot, or whenever no valid configuration is stored |
| RUN | Type `RUN` |
| RUN | Automatic on boot when a valid configuration is stored and the button isn't held |

`CONFIG` and `RUN` work in either mode, so you can never lock yourself out.

On entering each mode the firmware prints a banner:

```
========================================
  ENTERED CONFIG MODE
  ...
  Type RUN to resume normal operation
========================================
```

```
========================================
  ENTERED RUN MODE
  ...
  Type CONFIG to modify configuration
========================================
```

On a first boot with nothing stored you'll see:

```
========================================
  NO CONFIGURATION FOUND
  Automatically entering CONFIG mode
========================================
```

---

## What Changes in CONFIG Mode

| | CONFIG mode | RUN mode |
|---|---|---|
| Sensor readings | Keep updating | Keep updating |
| LCD | Shows every configured input | Shows enabled inputs |
| CAN / OBD-II, ELM327, RealDash, BLE live values | Keep working | Working |
| Relays and other Actions | **Held off** | Follow their Rules |
| Alarm buzzer | **Silent** | Sounds for `ALARM` Rules |
| Serial CSV output | **Paused** (keeps the terminal readable) | Running |
| SD card logging | **Paused** (no bench data in the drive log) | Running |
| Configuration commands | Allowed | Blocked |
| Read-only commands | Allowed | Allowed |
| Watchdog | Disabled where the board allows | Enabled (2 s) |

Relays and the buzzer are held off so that a sensor sitting on the bench — or
disconnected while you wire it — can't start a fan, run a pump or sound an alarm.

---

## Command Gating

### Available in both modes

| Command | Purpose |
|---------|---------|
| `CONFIG`, `RUN` | Switch modes |
| `HELP`, `?` | Command help |
| `VERSION` | Firmware version |
| `INFO <pin>` | One input in detail |
| `LIST ...` | Inputs, applications, sensors, outputs, transports |
| `SYSTEM STATUS`, `SYSTEM PINS`, `SYSTEM DUMP [JSON]`, `SYSTEM DIAG ...` | Status, pin map, configuration dump, diagnostics |
| `LOG STATUS`, `LOG TAGS` | Log filter status |
| `RULE LIST`, `RULE <id>`, `ACTION LIST`, `ACTION <id>` | Alarm / relay status |
| `ACTION <id> ON|OFF|AUTO` | Manual relay override |
| `ALARM STATUS`, `ALARM SILENCE` | Alarm status and acknowledge |
| `AT <n> <command>` | Raw passthrough to a serial module |

### CONFIG mode only

Everything that changes configuration, including:

- `SET ...`, `CLEAR ...`
- `RULE ADD/DEL/RESET`, `RULE <id> LIMIT/WARN/SEVERITY/ENABLE/DISABLE`
- `ACTION ADD/DEL`, `ACTION <id> RULE ...`, `ACTION <id> NAME ...`
- `ALARM DURATION`, `ALARM SCOPE`
- `OUTPUT ...`, `DISPLAY ...`, `BUS ...`, `TRANSPORT ...`
- `SYSTEM UNITS/INTERVAL/SEA_LEVEL/REBOOT/RESET`, `LOG LEVEL`
- `SAVE`, `LOAD`, `JSON IMPORT`, `SCAN`

Trying one of these in RUN mode names the command that was blocked:

```
========================================
  ERROR: SET requires CONFIG mode
  Type CONFIG to enter configuration mode
========================================
```

---

## The Mode Button

The mode button is optional. Its pin is `MODE_BUTTON_PIN`, set in your board's
wiring file (`boards/<board>/wiring.ini`; copy `wiring.ini.example`). With no
pin set, the feature is simply left out.

| When | Action | Effect |
|------|--------|--------|
| Powering on | Hold | Start in CONFIG mode |
| RUN mode | Short press | Acknowledge the alarm(s) sounding right now (same as `ALARM SILENCE`) |
| RUN mode | Hold for 2 seconds | Turn the display on/off |

Acknowledgement is per alarm: a different alarm tripping later still sounds.
`ALARM DURATION` and `ALARM SCOPE` change how long and how much is silenced; see
the [Alarm System Guide](ALARM_SYSTEM_GUIDE.md).

**Wiring:** a momentary push button between `MODE_BUTTON_PIN` and GND. The
firmware enables the pin's internal pull-up, so no external resistor is needed.

---

## Typical Workflows

### First setup

```
CONFIG                                   # (automatic on first boot)
SET 6 CHT MAX6675
SET A1 COOLANT_TEMP VDO_120C_TABLE
SET A2 OIL_PRESSURE VDO_5BAR_CURVE
LIST INPUTS                              # check what you configured
SAVE
RUN
```

`SET <pin> <application> <sensor>` also creates a default alarm for the input.
Adjust it with `SET <pin> ALARM ...` or see the [Alarm System Guide](ALARM_SYSTEM_GUIDE.md).

### Changing something

```
CONFIG
SET A3 BOOST_PRESSURE GENERIC_BOOST
SET A3 ALARM -0.5 2.0                    # bar
SAVE
RUN
```

Because readings keep updating in CONFIG mode, you can check a new sensor with
`INFO <pin>` before you type `RUN`.

### No laptop in the field

The configuration is stored in EEPROM, so the unit boots straight into RUN mode
and needs no connection. If you need to change something, connect over USB (or a
Bluetooth serial module, if fitted), type `CONFIG`, make the change, `SAVE`, `RUN`.

---

## Troubleshooting

**"requires CONFIG mode"** — type `CONFIG` first.

**The unit boots into CONFIG mode every time** — nothing has been saved, or the
stored configuration is invalid. Configure, run `SAVE`, and check that it reports
success before rebooting.

**Holding the button at power-on doesn't enter CONFIG mode** — check that
`MODE_BUTTON_PIN` is set in your `wiring.ini`, that the firmware was rebuilt after
setting it, and that the button pulls the pin to GND. Typing `CONFIG` always works.

**Relays or the buzzer don't work while testing** — they are held off in CONFIG
mode. Type `RUN`.

**No CSV output or SD log** — both pause in CONFIG mode. Type `RUN`.

**Changes are lost after a power cycle** — run `SAVE` before leaving CONFIG mode.

---

## Related Documentation

- [Serial Command Reference](../../reference/SERIAL_COMMANDS.md) — complete command list
- [Alarm System Guide](ALARM_SYSTEM_GUIDE.md) — alarm states, warmup and acknowledgement
- [Relay Control Guide](../outputs/RELAY_CONTROL.md) — Rules and Actions for relays
