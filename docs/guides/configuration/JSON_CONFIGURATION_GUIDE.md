# JSON Configuration Guide

preOBD can export its whole configuration as JSON and import it again. Use it to:

- **Back up** a working setup before you change things
- **Restore** a setup after a factory reset or on a replacement board
- **Keep several setups** on an SD card (street, track, dyno) and swap between them
- **Edit in bulk** — change a file on your computer instead of typing many commands

> **Board support:** JSON export/import is available on Teensy 3.6/4.0/4.1 and
> ESP32-S3. It is left out of the Arduino Mega build to fit its flash and RAM.
> SD card save/load additionally needs an SD card (built in on Teensy 4.1).

## Table of Contents
- [Export](#export)
- [Import](#import)
- [SD Card Backup and Restore](#sd-card-backup-and-restore)
- [File Format](#file-format)
- [Things to Know](#things-to-know)
- [For Developers](#for-developers)

---

## Export

```
SYSTEM DUMP JSON
```

Prints the complete active configuration as JSON. Copy it from the serial
terminal into a file. It works in both CONFIG and RUN mode.

(`SYSTEM DUMP REGISTRY JSON` is different: it exports the firmware's built-in
catalog of sensors, applications and units, not your configuration.)

---

## Import

In CONFIG mode:

```
JSON IMPORT
```

then paste the JSON and finish with a line containing only:

```
END JSON
```

The imported configuration takes effect immediately but is **not saved** until
you run `SAVE`. Check it first with `LIST INPUTS`, `RULE LIST` and `OUTPUT STATUS`.

Tools that send the file programmatically can use `JSON IMPORT <byte-count>` and
stream exactly that many bytes, with no `END JSON` line.

---

## SD Card Backup and Restore

```
SAVE SD:mycar.json          # write the configuration to the SD card
LOAD SD:mycar.json          # read it back (then SAVE to keep it)
```

- `SAVE` / `LOAD` on their own (or with `EEPROM`) use the internal EEPROM, as usual.
- A filename without a prefix goes to the SD card: `SAVE mycar.json`.
- A relative name is stored under `/config/` on the card; start it with `/` to
  choose the folder yourself (`SAVE SD:/backups/2026-09.json`).
- Paths are limited to 32 characters including the `SD:` prefix.

**Backup and restore workflow:**

```
CONFIG
SAVE SD:before_changes.json     # backup
... make changes, test them ...
LOAD SD:before_changes.json     # didn't work out: restore
SAVE                            # make the restored config permanent
RUN
```

---

## File Format

The file is a single JSON object. `SYSTEM DUMP JSON` on your own board is the
best reference; the overall shape is:

```json
{
  "schemaVersion": 1,
  "mode": "runtime",
  "firmware": { "version": "0.13.0-beta", "platform": "...", "maxInputs": 40, "...": "..." },
  "system": {
    "outputs":   { "can": { "enabled": true, "interval": 100 }, "sd": { "enabled": true, "interval": 5000, "format": "WIDE" }, "...": {} },
    "display":   { "enabled": true, "type": "LCD", "address": "0x27", "updateInterval": 1000,
                   "defaultUnits": { "temperature": "CELSIUS", "pressure": "BAR", "elevation": "METERS", "speed": "KPH" } },
    "timing":    { "sensorRead": 100, "alarmCheck": 100 },
    "constants": { "seaLevelPressure": 1013.25 },
    "buses":     { "i2c": 0, "spi": 0, "canOutputBus": 0, "canOutputBaudrate": 500000, "...": "..." },
    "serial":    { "ports": [ { "port": 1, "enabled": true, "baudrate": 9600, "elm327": false } ] },
    "logFilter": { "controlLevel": "INFO", "dataLevel": "INFO", "debugLevel": "WARN", "enabledTags": "0xFFFFFFFF" }
  },
  "inputs": [
    {
      "idx": 0,
      "pin": 16,
      "pinLabel": "A2",
      "abbr": "ECT",
      "name": "Engine Coolant Temperature",
      "app": "COOLANT_TEMP",
      "sensor": "VDO_120C_STEINHART",
      "units": "CELSIUS",
      "alarm": { "ruleId": 0, "min": -1.0, "max": 100.0 },
      "enabled": true,
      "displayEnabled": true,
      "outputMask": 15
    }
  ],
  "rules": [
    { "id": 0, "input": 16, "condition": "OUTSIDE", "severity": "ALARM",
      "limitLo": -1.0, "limitHi": 100.0, "hysteresis": 0.0,
      "warmupMs": 60000, "debounceMs": 500, "enabled": true }
  ],
  "actions": [
    { "id": 0, "type": "RELAY", "target": 23, "rule": 1, "name": "Cooling Fan",
      "invert": false, "failsafe": "ON" }
  ]
}
```

### Inputs

| Field | Meaning |
|-------|---------|
| `pin` | The board's pin number. **This is what import uses** — see [Things to Know](#things-to-know) |
| `pinLabel` | The pin as you'd type it (`A2`, `I2C:0`, `CAN:0`); informational |
| `app`, `sensor` | Application and sensor names, as in `SET <pin> <app> <sensor>` |
| `units` | Display units |
| `abbr`, `name` | Short (LCD) and full labels |
| `enabled`, `displayEnabled` | Input on/off; shown on the LCD or not |
| `outputMask` | Which data outputs carry this input (CAN, RealDash, Serial, SD) |
| `divider` | Voltage-divider ratio, present only when one is set (`SET <pin> DIVIDER`) |
| `obd2` | `{ "pid", "length" }` when the input reports as an OBD-II PID |
| `alarm` | Read-only summary of the input's alarm Rule — edit the `rules` array instead |
| `calibration` | Present only for a custom calibration — see below |

### Custom calibration

A custom calibration looks like this:

```json
"calibration": {
  "type": "THERMISTOR_BETA",
  "source": "CUSTOM",
  "params": { "biasResistor": 2490, "beta": 3950, "r0": 10000, "t0": 25 }
}
```

| `type` | `params` |
|--------|----------|
| `THERMISTOR_STEINHART` | `biasResistor`, `steinhartA`, `steinhartB`, `steinhartC` |
| `THERMISTOR_BETA` | `biasResistor`, `beta`, `r0`, `t0` |
| `THERMISTOR_TABLE`, `PRESSURE_TABLE` | `biasResistor` |
| `PRESSURE_POLYNOMIAL` | `biasResistor`, `polyA`, `polyB`, `polyC` |
| `LINEAR` | `voltageMin`, `voltageMax`, `outputMin`, `outputMax` |
| `VOLTAGE_DIVIDER` | `r1`, `r2`, `correction`, `offset` |
| `RPM` | `poles`, `pulleyRatio`, `calibrationMult`, `timeoutMs`, `minRPM`, `maxRPM` |
| `SPEED` | `pulsesPerRev`, `tireCircumferenceMm`, `finalDriveRatio`, `calibrationMult`, `timeoutMs`, `maxSpeedKph` |
| `CAN_IMPORT` | `sourceCanId`, `sourcePid`, `dataOffset`, `dataLength`, `isBigEndian`, `scaleFactor`, `offset`, `timeoutMs` |

`source` must be `"CUSTOM"` for the parameters to apply (`"PRESET"` means "use the
sensor's built-in calibration"), and `type` must match the calibration type of the
input's sensor.

Some calibrations can **only** be set through JSON: the battery voltage divider
resistors (`VOLTAGE_DIVIDER` — `r1`, `r2`, `correction`, `offset`) and custom CAN
frame decoding (`CAN_IMPORT`). The others can also be set with `SET <pin> CAL ...`.

### Rules

| Field | Meaning |
|-------|---------|
| `id` | Rule id (referenced by actions) |
| `input` | The **pin number** the Rule watches |
| `condition` | `ABOVE`, `BELOW` or `OUTSIDE` |
| `severity` | `NONE`, `INFO`, `WARNING` or `ALARM` (default `NONE`) |
| `limitLo`, `limitHi` | Limit (`ABOVE`/`BELOW` use `limitLo`), or the band for `OUTSIDE` — in base units (°C, bar, ...) |
| `hysteresis` | Hysteresis band width |
| `warmupMs`, `debounceMs` | Warmup after arming, and how long the condition must hold (max 65535 ms) |
| `enabled` | `false` = parked |
| `warnLo`, `warnHi` | `ALARM` rules only: a number sets the warning edge, `null` means no warning tier, and leaving the key out means automatic |

`warmupMs` and `debounceMs` can only be changed through JSON.

### Actions

| Field | Meaning |
|-------|---------|
| `id` | Action id |
| `type` | `RELAY` |
| `target` | Output pin |
| `rule` / `rules` | The Rule id it follows, or an array of ids when it follows several; leave out for manual-only |
| `name` | Optional label |
| `invert` | Active-low output |
| `failsafe` | `ON` or `OFF` — state when the Rule's sensor fails |

---

## Things to Know

- **Pins are board pin numbers.** `A2` is pin 16 on a Teensy 4.x but a different
  number on another board, so a file exported from one board type should not be
  imported on a different one without editing the `pin`, `input` and `target`
  numbers.
- **Missing sections are left alone.** A file with only `inputs` leaves the
  system settings, Rules and Actions as they were.
- **Inputs are written by slot.** Each entry in `inputs` is written into slot
  `idx`; inputs in other slots are kept. To start clean, `SYSTEM RESET CONFIRM`
  first. A `rules` or `actions` array, by contrast, replaces that whole table.
- **Missing fields take defaults**, so an older export still imports on newer firmware.
- **`schemaVersion` must be 1** and `mode` must be `"runtime"`; anything else is refused.
- **Import doesn't save.** Run `SAVE` afterwards.

---

## For Developers

Changing the configuration format (adding fields, calibration types, or
bumping the schema version) is covered in the contributor checklist:
[JSON_SCHEMA_CHANGES.md](../../development/JSON_SCHEMA_CHANGES.md). The code lives in
`src/storage/json_config.cpp` and `src/rules/rules_json.cpp`.

## See Also

- [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#persistence-commands) — `SAVE` / `LOAD`
- [EEPROM Structure](../../development/architecture/EEPROM_STRUCTURE.md) — how the configuration is stored internally
