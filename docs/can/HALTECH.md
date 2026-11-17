# HALTECH: Haltech's ECU broadcast for gauges and dashes

`HALTECH` is an [output](../reference/SERIAL_COMMANDS.md#listing-and-changing-outputs)
that sends preOBD's sensors as the CAN frames a Haltech Elite or Nexus ECU
broadcasts (the *Haltech ECU Broadcast CAN Protocol*, version 2.0). A gauge or
dash built to listen to a Haltech ECU (the Haltech multi-function CAN gauge, a
Haltech iC-7 or uC-10, or a third-party dash with a Haltech preset) shows
preOBD's readings with no setup on the gauge's side.

```
BUS CAN CAN2 BAUDRATE 1000000
BUS CAN CAN2 ENABLE
OUTPUT ADD HALTECH CAN2 [INTERVAL <ms>]     # 20-60000 ms, default 20
```

Not on every board: the Mega 2560 doesn't have it.

> Checked against the protocol document byte by byte, and on a 1 Mbit/s bus
> with the frames decoded by Haltech's own conversions. Not yet checked with a
> real Haltech gauge or dash (#689).

## A 1 Mbit/s gauge network

Haltech's protocol runs at **1 Mbit/s**, so `HALTECH` is refused on a bus at
any other rate, and `BUS CAN <bus> BAUDRATE` won't move the rate while a
`HALTECH` output is enabled there. A 500 kbit/s car network and a Haltech gauge
need two buses.

The [gauge-network rules of `PREOBD`](README.md#gauge-network-not-car-network)
apply as they are: `HALTECH` transmits, so it is refused on the bus CAN input
reads and on a `LISTEN` bus, and something on the bus must ACK its frames.
`HALTECH`, `PREOBD` and `OBD` can share a bus: their CAN IDs don't overlap.
Never put `HALTECH` on a bus that has a real Haltech ECU on it: both would send
the same IDs.

## The frames

Eleven frames, 11-bit IDs, 8 bytes each, **big-endian**, all at the output's
interval (default 20 ms, 50 Hz; Haltech's own ECU sends the slower frames at
5-20 Hz, and a gauge reads the faster rate the same way).

| ID | Bytes | Haltech channel | preOBD application | Raw value |
|---|---|---|---|---|
| 0x360 | 0-1 | RPM | `ENGINE_RPM` | rpm |
| | 2-3 | Manifold Pressure | `MAP_PRESSURE` | kPa absolute × 10 |
| | 4-5 | Throttle Position | `THROTTLE_POSITION` | % × 10 |
| 0x361 | 0-1 | Fuel Pressure | `FUEL_PRESSURE` | kPa absolute × 10 |
| | 2-3 | Oil Pressure | `OIL_PRESSURE` | kPa absolute × 10 |
| 0x36B | 0-1 | Brake Pressure Front | `BRAKE_PRESSURE` | kPa absolute (× 1) |
| 0x370 | 0-1 | Vehicle Speed | `VEHICLE_SPEED` | km/h × 10 |
| 0x372 | 0-1 | Battery Voltage | `PRIMARY_BATTERY` | V × 10 |
| | 6-7 | Barometric Pressure | `BAROMETRIC_PRESSURE` | kPa absolute × 10 |
| 0x373 | 0-7 | EGT Sensor 1-4 | `EGT` 1-4 | K × 10 |
| 0x374 | 0-7 | EGT Sensor 5-8 | `EGT` 5-8 | K × 10 |
| 0x376 | 0-1 | Ambient Air Temperature | `AMBIENT_TEMP` | K × 10 |
| | 2-3 | Relative Humidity | `HUMIDITY` | % × 10, signed |
| 0x3E0 | 0-1 | Coolant Temperature | `COOLANT_TEMP` | K × 10 |
| | 2-3 | Air Temperature | `INTAKE_AIR_TEMP` | K × 10 |
| | 6-7 | Oil Temperature | `OIL_TEMP` | K × 10 |
| 0x3E1 | 0-1 | Gearbox Oil Temperature | `TRANS_TEMP` | K × 10 |
| | 2-3 | Diff Oil Temperature | `DIFF_TEMP` | K × 10 |
| 0x6F8 | 6-7 | Fuel Level Percentage 0 | `FUEL_LEVEL` | % × 10 |

Several inputs of one application (`EGT` 1 to 8) are numbered in pin order, as
in [`PREOBD`](README.md#which-input-goes-in-which-signal). `OUTPUT <id>` lists
which input fills each signal.

Haltech has no channel for `CHT`, `COOLANT_LEVEL`, `OIL_LEVEL` (a float switch
here; Haltech's is a percentage), `ELEVATION`, `INTERCOOLER_TEMP`,
`GEAR_OIL_TEMP`, `AUXILIARY_BATTERY`, `BOOST_PRESSURE`, `HYDRAULIC_PRESSURE` or
`TCASE_TEMP`; preOBD has no application for Haltech's lambda, coolant pressure,
fuel temperature, injector, ignition or knock channels. Those fields stay 0.

### Pressures are absolute

Every Haltech pressure is absolute; the gauge subtracts 101.3 kPa to show gauge
pressure. preOBD keeps oil, fuel and brake pressure as gauge pressure, so the
output adds the barometric reference first: your barometric input if you have
one, else 101.325 kPa ([absolute and gauge pressure](../guides/sensor-types/PRESSURE_SENSOR_GUIDE.md#absolute-and-gauge-pressure)).
With an engine-off oil pressure of 0, the frame carries 101.3 kPa and the gauge
shows 0. Away from sea level, with a barometric input fitted, the gauge's
reading differs from preOBD's by your barometric pressure minus 101.3 kPa,
which is also what a Haltech ECU's own absolute sensors show. `MAP_PRESSURE`
and `BAROMETRIC_PRESSURE` are absolute already and go out as they are.

`BOOST_PRESSURE` is not sent: Haltech carries manifold pressure, not boost. Fit
a MAP sensor as `MAP_PRESSURE` for the gauge's boost display.

### No data

Haltech has no "not available" value; its own channels send **0** when a sensor
is in error. `HALTECH` does the same:

- a field whose input is not fitted, disabled, faulted, has no reading yet or
  measures the wrong quantity is 0;
- a frame none of whose signals has an input routed to this output is **not
  sent**, so a gauge reading a channel you don't have sees no data rather than
  a 0;
- a real value uses the whole field (0-65535), clamped at the ends.

A 0 reads on the gauge as 0 rpm, 0 km/h, −273 °C or −101.3 kPa: that is a
sensor in error, as on a Haltech ECU.

Routes work as for every layout: `SET <pin> ROUTE <id> DISABLE` takes one input
off this output (its field goes to 0, and its frame goes off the bus if nothing
else in it is routed here).

## Bus load

All eleven frames at the default 20 ms are about 6% of a 1 Mbit/s bus.
`OUTPUT <id>` prints the estimate for its interval and the count of passes
that were skipped or unACKed.
