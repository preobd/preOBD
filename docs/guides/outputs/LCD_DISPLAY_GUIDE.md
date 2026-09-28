# LCD Display Guide

preOBD can show live readings on a **20×4 character LCD** with an I2C
"backpack" (PCF8574 — the common blue or green module with four pins).

## What It Shows

The screen is split into two columns of four rows — eight cells of ten
characters each. Each enabled input gets a cell, in order:

```
┌────────────────────┐
│°ECT:87.5 °EOT:95.0 │
│ EOP:3.2   BAT:13.8 │
│°CHT:185   RPM:2450 │
│ BST:0.8   AAT:22.4 │
└────────────────────┘
```

Each cell is an icon, the input's short label (`ABBR`), and the value in the
input's units. Special values:

| Shown | Meaning |
|-------|---------|
| `ERR` | The sensor isn't giving a valid reading (open, shorted, out of range) — see `SYSTEM DIAG INPUTS` |
| `CFG` | The input is configured but disabled (CONFIG mode) |
| `---` | No reading yet (the input was just added or the board just started) |

**Up to eight inputs fit.** A ninth and later input isn't shown correctly yet
(a known firmware issue), so keep the LCD to eight inputs.

Units aren't printed; set them per input with `SET <pin> UNITS` or for everything
with `SYSTEM UNITS`.

## Wiring

| LCD backpack | Teensy 4.x | Arduino Mega |
|--------------|------------|--------------|
| GND | GND | GND |
| VCC | 5V (see warning) | 5V |
| SDA | 18 (SDA) | 20 (SDA) |
| SCL | 19 (SCL) | 21 (SCL) |

> ⚠️ **3.3V boards:** most of these LCDs need 5V, and the backpack pulls SDA/SCL
> up to its own VCC. On a Teensy or ESP32 that puts 5V on the I2C pins. Use an
> I2C level shifter between them, or remove the backpack's pull-up resistors and
> fit pull-ups to 3.3V instead.

## Setup

```
CONFIG
BUS I2C 0                 # the LCD is wired to the first I2C bus (Wire)
DISPLAY TYPE LCD          # there is no display until you say so
DISPLAY ADDRESS 0x27      # 0x27 is the default; many modules use 0x3F
DISPLAY ENABLE
SAVE
SYSTEM REBOOT
```

`DISPLAY STATUS` shows the display settings. If the screen stays blank, try the
other address.

### Which I2C bus

preOBD never guesses which bus you wired: it brings an I2C bus up only when you
select it with `BUS I2C <n>` (0 = `Wire`, 1 = `Wire1`, 2 = `Wire2`), or when your
`boards/<board>/wiring.ini` declares it with `-D I2C_WIRED_BUS=<n>`. Without one
of those the log says `LCD: no I2C bus`. Once a bus is up, its SDA/SCL pins are
reserved and `SET <pin>` refuses them. See the
[Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#bus-configuration).

## Choosing What's Shown

```
SET A2 ABBR ECT            # short label in the cell (keep it to ~4 characters)
SET A5 DISPLAY DISABLE     # blank this input's cell
SET A2 UNITS F             # show °F for this input
SYSTEM UNITS PRESSURE PSI  # default units for all pressure inputs
DISPLAY INTERVAL 500       # refresh every 500 ms (default)
```

A disabled input still keeps its cell (blank) — it doesn't make room for another.

**Turning the screen off while driving:** hold the mode button for 2 seconds in
RUN mode (if you've wired one). `DISPLAY DISABLE` turns it off permanently.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Backlight on, no characters | Adjust the contrast pot on the backpack; wrong address (`DISPLAY ADDRESS 0x3F`) |
| Nothing at all | `DISPLAY STATUS` shows type `LCD` and enabled? `SAVE` and `SYSTEM REBOOT` after changing it |
| Boot says "no display configured" | `DISPLAY TYPE LCD` |
| "LCD: no I2C bus" in the log | `BUS I2C` shows which bus is up; the SDA/SCL pins must match the wiring |
| `ERR` in a cell | That sensor isn't reading — `INFO <pin>`, `SYSTEM DIAG INPUTS` |

`SYSTEM DIAG HW` checks whether the display answers on the bus.

## See Also

- [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#display-configuration)
- [Config/Run Mode Guide](../configuration/CONFIG_RUN_MODE_GUIDE.md) — the mode button
