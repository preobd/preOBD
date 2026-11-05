# Data Logging Guide

preOBD can record readings two ways:

- **SD card** — a CSV file per drive, for graphing in a spreadsheet afterwards
- **Serial CSV** — live lines on the USB serial port (or a Bluetooth module), for a laptop logger

Both pause in CONFIG mode, so bench setup doesn't end up in your log.

---

## SD Card Logging

**Hardware:** the Teensy 4.1 and 3.6 and the Feather F405 have a built-in microSD
slot. On a Teensy 4.0 or ESP32-S3, wire an SD card module to the SPI bus and set its
chip-select pin in `boards/<board>/wiring.ini`. The Arduino Mega has no room for SD
logging. Use a FAT32-formatted card.

An external card shares the SPI bus with any thermocouples, so it needs a bus like they
do: declare it in `wiring.ini`, or run `BUS SPI <n>` then `SAVE` and reboot. With no
bus, the card stays unmounted and boot says what to run.

```ini
[teensy40]
wiring =
    -D SD_PIN=10          ; chip-select
    -D SPI_WIRED_BUS=0    ; the card is on SPI (Teensy 4.0: MOSI 11, MISO 12, SCK 13)
```

Check your module's supply: one with its own 3.3 V regulator (most blue "HW-125"
style boards) needs **5 V** on VCC and fails on 3.3 V, while a bare 3.3 V module
must never get 5 V.
The card runs at up to 12 MHz. If a card reads as unformatted on long or breadboarded
wiring, lower it with `-D SD_SPI_MAX_HZ=4000000`.

```
CONFIG
OUTPUT SD_Log ENABLE
OUTPUT SD_Log INTERVAL 1000     # One row per second (default 5000)
OUTPUT SD_Log FORMAT WIDE       # or LONG — see below (default WIDE)
SAVE
RUN
```

Only inputs routed to SD are logged — see `SET <pin> OUTPUT SD_LOG ENABLE|DISABLE`
(all data outputs are on by default).

### File format

Two layouts are available.

`WIDE` (default) writes one row per sample with a column per logged input. The
unit is folded into the column heading, so it drops straight into a spreadsheet
and graphs without a pivot:

```
millis,CLT_C,OILP_PSI,EGT_C
14231,87.50,42.10,610.00
19231,88.10,41.90,615.50
24231,88.40,ERROR,617.20
```

`LONG` writes one row per reading. Every row carries its own sensor name and
unit, so the layout absorbs a changing input set without needing a new header —
and it is the layout preOBD wrote before the wide format existed, so scripts
built on it keep working:

```
Time,Sensor,Value,Units
14231,CLT,87.50,C
14231,OILP,42.10,PSI
14231,EGT,610.00,C
```

In both layouts a faulted or disconnected sensor records `ERROR` rather than
disappearing, so a fault is distinguishable from a gap. (This is the one
difference from pre-`FORMAT` behaviour, which dropped the reading silently —
worth knowing if a script converts the value column without guarding.)

`millis`/`Time` is uptime in milliseconds, captured once per sample — every
reading in a tick shares one timestamp. There is no RTC on any supported board,
so rows are correlatable within a session but not tied to wall-clock time.

### Files

Files are written to the card root as `LOG0001.CSV`, `LOG0002.CSV`, … The
next free number is claimed when logging starts, so a power cycle always begins
a new file rather than appending to the previous drive's.

In `WIDE`, the column set is fixed when a file is opened. Changing what gets
logged mid-session — adding or clearing an input, toggling its SD routing,
changing its units — closes the current file and starts the next one, so a
header always describes the rows beneath it. `LONG` needs no such roll, since
each row names its own sensor. Switching `FORMAT` always starts a new file.

### Durability

Rows are flushed at least once a second, so an abrupt power cut
loses at most about a second of data. `OUTPUT SD_Log DISABLE` closes the file
cleanly.

### Checking on it

A mounted card does not mean rows are landing — a full card,
or one pulled mid-drive, fails every write. `SYSTEM DIAG HW` reports the live
state:

```
[hardware]    SD card .............. OK
[hardware]    SD logging ........... OK (LOG0007.CSV, WIDE, 412 rows, 0 errors)
```

`OUTPUT STATUS` also shows the active format, and it round-trips through
`SYSTEM DUMP JSON` / `JSON IMPORT` as `system.outputs.sd.format`.

---

## Serial CSV Output

```
CONFIG
OUTPUT Serial ENABLE
OUTPUT Serial INTERVAL 1000     # one set of lines per second (default)
SAVE
RUN
```

Every interval, each input routed to the serial output prints one line:

```
ECT,87.50,C
EOP,3.20,bar
BAT,13.80,V
```

`name,value,unit` — the input's short label (`SET <pin> ABBR`), the value in its
display units, and the unit. A failed sensor prints `ERROR` as the value.

The lines go to preOBD's **data channel** — USB by default. `TRANSPORT DATA SERIAL1`
sends them to a Bluetooth module on Serial1 instead (see `TRANSPORT STATUS`).
RealDash output uses the same channel, so enable one or the other.

Choose which inputs appear with `SET <pin> OUTPUT SERIAL ENABLE|DISABLE`
(all are on by default).

---

## See Also

- [Serial Commands Reference](../../reference/SERIAL_COMMANDS.md#output-configuration)
- [JSON Configuration Guide](../configuration/JSON_CONFIGURATION_GUIDE.md) — saving your *configuration* (not readings) to SD
