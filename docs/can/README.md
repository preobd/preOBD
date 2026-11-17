# PREOBD: preOBD's own CAN frames for gauges

`PREOBD` is an [output](../reference/SERIAL_COMMANDS.md#listing-and-changing-outputs)
that pushes the same fixed set of CAN frames on a bus every interval, so a
dash, a logger or a CAN gauge can show every preOBD sensor without any OBD-II
request. The frames are published as a DBC file, [`preOBD.dbc`](preOBD.dbc),
which any DBC-reading tool imports.

```
OUTPUT ADD PREOBD CAN1 [INTERVAL <ms>]      # 20-60000 ms, default 100
```

Not on every board: the Mega 2560 doesn't have it.

## Gauge network, not car network

A CAN bus does one job (#632). `PREOBD` transmits, so it goes on the **gauge
network**, the bus your dash or gauges listen on, and never on the bus that CAN
input reads from the car. preOBD refuses the combination:

- `PREOBD` is refused on a bus where CAN input reads (`BUS CAN <bus> INPUT
  ENABLE` is refused while `PREOBD` runs there, and the other way round).
- `PREOBD` is refused on a `LISTEN` bus, which never transmits.
- A board with **one** CAN bus therefore runs `PREOBD` **or** CAN input, not
  both. A board with two buses can read the car on one and feed the gauges on
  the other.

`PREOBD` and the `OBD` output can share a gauge bus: they use different CAN IDs
(`OBD` owns 0x7DF, 0x7E0 and 0x7E8).

The receiving side must **ACK** the frames (a gauge, a dash or a second node on
the bus does). A bus with no receiver cannot ACK: the controller retransmits
forever. preOBD bounds that: a pass that has not left by the next interval is
dropped (`unACKed` in `OUTPUT <id>`), so the transmit queue never fills and an
OBD answer on the same bus is never starved.

## The frames

Nine frames, CAN IDs 0x500-0x508 (11-bit), 8 bytes each, **big-endian**. Every
frame is sent every pass, whether or not its sensors are fitted.

| ID | Bytes 0-1 | 2-3 | 4-5 | 6-7 |
|----|-----------|-----|-----|-----|
| 0x500 | `ENGINE_RPM` | `VEHICLE_SPEED` | `MAP_PRESSURE` | `BOOST_PRESSURE` |
| 0x501 | `COOLANT_TEMP` | `OIL_TEMP` | `INTAKE_AIR_TEMP` | `INTERCOOLER_TEMP` |
| 0x502 | `OIL_PRESSURE` | `FUEL_PRESSURE` | `BRAKE_PRESSURE` | `HYDRAULIC_PRESSURE` |
| 0x503 | `TRANS_TEMP` | `DIFF_TEMP` | `TCASE_TEMP` | `GEAR_OIL_TEMP` |
| 0x504 | `EGT` #1 | `EGT` #2 | `EGT` #3 | `EGT` #4 |
| 0x505 | `CHT` #1 | `CHT` #2 | `CHT` #3 | `CHT` #4 |
| 0x506 | `PRIMARY_BATTERY` | `AUXILIARY_BATTERY` | `THROTTLE_POSITION` | `FUEL_LEVEL` |
| 0x507 | `AMBIENT_TEMP` | `BAROMETRIC_PRESSURE` | `HUMIDITY` | `ELEVATION` |
| 0x508 | `COOLANT_LEVEL` (byte 0), `OIL_LEVEL` (byte 1) | spare | spare | spare |

All fields are unsigned, 16-bit (the two level bytes excepted); the physical
value is `raw * factor + offset`:

| Quantity | Factor | Offset | Carries |
|----------|--------|--------|---------|
| Temperatures (°C) | 0.1 | -50 | -50.0 to 6503.4 |
| Pressures (kPa) | 0.1 | 0 | 0.0 to 6553.4 |
| `BOOST_PRESSURE` (kPa gauge) | 0.1 | -100 | -100.0 to 6453.4 |
| `MAP_PRESSURE`, `BAROMETRIC_PRESSURE` | 0.1 | 0 | absolute kPa |
| Voltage (V) | 0.001 | 0 | 0 to 65.534 |
| `VEHICLE_SPEED` (km/h) | 0.01 | 0 | 0 to 655.34 |
| `ENGINE_RPM` | 1 | 0 | 0 to 65534 |
| `THROTTLE_POSITION`, `FUEL_LEVEL`, `HUMIDITY` (%) | 0.1 | 0 | 0.0 to 6553.4 (100 is full scale) |
| `ELEVATION` (m) | 1 | -1000 | -1000 to 64534 |
| `COOLANT_LEVEL`, `OIL_LEVEL` | 1 | 0 | one byte, 0 or 1 (a float switch) |

The values are the sensor's reading in the units above whatever the display units are:
`SYSTEM UNITS` and per-input units don't change the frames.

### No data

A field of **all ones** (`0xFFFF`, or `0xFF` for a level byte) means *no data*:
the signal has no input, the input is disabled, hasn't read yet, isn't routed to
this output (`SET <pin> ROUTE <id> ENABLE`), or isn't finite. A real value never reaches that
pattern: it clamps one step lower. The DBC gives each signal a `VAL_` entry
for it (`"N/A"`) and a maximum one step below, so a tool that ignores `VAL_`
doesn't show 6513 °C for a missing sensor.

This reservation is PREOBD's own; the OBD-II output encodes as J1979 says.

### Which input goes in which signal

A signal names an application and an instance. Instance *n* of an application
is the *n*-th configured input of that application **in pin order**, so the
four `EGT` and four `CHT` signals follow the pin numbers, not the order you
added the inputs, and survive a `SAVE` and a reload. A disabled input keeps
its place (it sends *no data*); the others don't shift. An input whose
measurement type is not the signal's (a pressure under `OIL_TEMP`) sends no
data. `OUTPUT <id>` prints the map:

```
  Signals:
    ENGINE_RPM = none, VEHICLE_SPEED = none,
    MAP_PRESSURE = A11, BOOST_PRESSURE = A0,
    COOLANT_TEMP = A2, OIL_TEMP = none,
    ...
    EGT#1 = none, EGT#2 = none,
```

An input is sent only if it is routed to the output (`SET <pin> ROUTE <id> ENABLE`).

## Using the DBC

[`preOBD.dbc`](preOBD.dbc) describes the frames above for any DBC-reading
tool:

- **A DBC-reading dash or logger** (a CAN dash, Metrix and similar): import the
  DBC, put the dash on the same bus at the bus's rate, and pick signals by name.
- **RealDash CAN**: import the DBC when adding a CAN connection and map signals
  to gauges.
- **SavvyCAN**: *File, Load DBC*, then watch the frames decode.
- **`cantools`** (Python): `cantools.database.load_file('docs/can/preOBD.dbc')`,
  then `db.decode_message(0x501, data)`.

The signals are Motorola (big-endian, `@0+`); the start bit is the position of
each signal's most significant bit, as DBC defines it. The DBC is generated
from the firmware's table by `tools/gen_preobd_dbc.py`, and the native test
suite fails if the committed file and the table differ.

## Bus load

A full pass is 9 frames, roughly 111 bits each. At 125 kbit/s a 100 ms interval
is about 8 % of the bus; the 20 ms floor is about 40 %, which `OUTPUT <id>`
flags with a NOTE above 30 %. It prints the estimate at the rate the bus is
running. Pick the slowest interval your gauges tolerate.

## Inputs imported from CAN

An input imported from the car's network (`SET CAN`) gets its PID's
application (0x0C `ENGINE_RPM`, 0x05 `COOLANT_TEMP`, ...) and maps like a wired
input: instance n is the n-th input of that application in pin order, so a
wired sensor (a lower pin than `CAN:0`) comes first. An imported 0x70 boost is
`BOOST_PRESSURE`, converted to gauge. A PID with no application reads as no data. The full list is in
the CAN sensor import guide.

## Known limitation

A long console reply (the connect-time `SYSTEM DUMP` the app sends, for
example) can pause the whole loop for about 2 seconds, and the gauges see a
gap in the frames while it lasts. Tracked in #635; until it is fixed or
measured on your setup, don't treat PREOBD as a hard-real-time feed for safety
gauges.
