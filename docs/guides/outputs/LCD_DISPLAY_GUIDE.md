# LCD Display Guide

preOBD can show live readings on a **20×4 character LCD** with an I2C
"backpack" (PCF8574 — the common blue or green module with four pins).

## What It Shows

The screen is split into two columns of four rows — eight cells of ten
characters each. Each enabled input gets a cell, in order:

```
┌────────────────────┐
│ECT:87°C  EOT:95°C  │
│EOP:3.2   BAT:13.8V │
│CHT:185°C RPM:2450  │
│BST:0.8   AAT:22°C  │
└────────────────────┘
```

Each cell is the input's short label (`ABBR`), a colon, and the value in the
input's units. The unit is printed after the value when it fits whole in the
cell, and is left off otherwise (`RPM:2450`, not `RPM:2450rp`). A left-hand
cell has nine characters, leaving one blank column before the right-hand
cell; a right-hand cell has all ten. A temperature gets `°C`/`°F`, or just the
degree sign, when there's room. The degree sign is the LCD's own built-in character, so no custom
characters are loaded. Special values:

| Shown | Meaning |
|-------|---------|
| `ERR` | The sensor isn't giving a valid reading (open, shorted, out of range) — see `SYSTEM DIAG INPUTS` |
| `CFG` | The input is configured but disabled (CONFIG mode) |
| `---` | No reading yet (the input was just added or the board just started) |

**When a value doesn't fit,** the LCD never shows part of a number (a `1650`
cut to `16` would read as a real, wrong reading). In order, it:

1. drops the decimals (`BOOST:11.4` becomes `BOOST:11` in a left-hand cell),
2. then shortens the label, down to two characters (`RUNTOT:12345` becomes
   `RUN:12345`; `EGT11:1650` becomes `EGT1:1650`),
3. and if the whole number still doesn't fit, fills the room with `#`
   (`ODO:#####`, or `OAT:-####` for a negative value). Check the value with
   `INFO <pin>` or on another output, or move the input to a right-hand cell,
   which has one more character.

The unit is only added in the room left after the value. `ERR`, `CFG` and
`---` shorten a long label the same way rather than being cut.

**Eight inputs fit at once.** With more, the LCD pages through them in groups
of eight, every 4 seconds. An input hidden with `SET <pin> DISPLAY DISABLE`
takes no cell, so hiding one makes room for another. `DISPLAY STATUS` shows how
many inputs are on the LCD (and whether it is paging).

Set the units per input with `SET <pin> UNITS` or for everything with
`SYSTEM UNITS`.

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
SET A2 ABBR ECT            # short label in the cell (keep it to ~4 characters; longer ones are shortened to fit the value)
SET A5 DISPLAY DISABLE     # hide this input (its cell goes to the next one)
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
| "LCD: no answer, retrying" in the log | Nothing answered at 0x27: check the wiring. preOBD keeps trying every 5 s, so a reconnected LCD comes back without a reboot |
| `ERR` in a cell | That sensor isn't reading — `INFO <pin>`, `SYSTEM DIAG INPUTS` |

`SYSTEM DIAG HW` checks whether the display answers on the bus.

## See Also

- [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#display-configuration)
- [Config/Run Mode Guide](../configuration/CONFIG_RUN_MODE_GUIDE.md) — the mode button

## OLED FeatherWing (Feather STM32F405)

On the Feather STM32F405 the same readings can go to the Adafruit
**FeatherWing OLED 128x64** (product 4650, SH1107, I2C address 0x3C). Stack
the wing on the Feather, then:

```
CONFIG
DISPLAY TYPE OLED
SAVE
SYSTEM REBOOT
```

The OLED shows the LCD's layout: the same eight cells, the same labels and
units, and pages of eight when there are more inputs. The rows are spaced out
to fill the taller screen. `DISPLAY STATUS` says whether the panel is
answering. If it's unplugged, or fitted after boot, preOBD checks for it
every 5 seconds and starts drawing when it answers, without a restart.

The wing's buttons are on the Feather's pins: **A = D9, B = D6, C = D5**.
D9 is also the CAN receive pin, so button A can't be used with CAN wired.
D5/D6 are the usual nRF flow-control pins; to use buttons B or C, move flow
control to other pins (e.g. D11/D12) in `boards/feather_f405/wiring.ini`.
The mode button can then be one of them, e.g. `-D MODE_BUTTON_PIN=6` for
button B.
