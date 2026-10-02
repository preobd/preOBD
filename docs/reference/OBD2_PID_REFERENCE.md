# OBD-II PID Reference for preOBD

## Overview

preOBD answers OBD-II Service 01 ("Mode 01", show current data) requests, over CAN and through its ELM327 emulation. You wire analog sensors to preOBD and tell it what each one measures. A standard tool (Torque, Car Scanner, OBD Fusion or a handheld scanner) then reads them like any ECU value, **with no setup**, wherever SAE J1979 defines a PID for that quantity.

- **Standard PIDs** use J1979's exact encoding. A tool decodes them with the formula it already knows.
- **preOBD PIDs** (0xAA–0xB7) carry sensors J1979 has no PID for: oil pressure, cylinder head temperature, transmission temperature and so on. A tool needs a one-time custom-PID entry for each, with the formula in the table below.

Every application gets its PID automatically, and `SET <pin> OBD_PID <pid>` changes it. Each encoding was checked against Wikipedia's "OBD-II PIDs", CSS Electronics' J1979 table, and DashLogic's J1979 PID listing (see `src/sensors/can_sensor_library/obd_output_pids.h` for which source confirms what).

## Standard PIDs (read with no setup)

| PID | J1979 name | preOBD application | Bytes | Formula | Range |
|-----|-----------|--------------------|-------|---------|-------|
| `0x05` | Engine coolant temperature | `COOLANT_TEMP` | 1 | A − 40 | −40 to 215 °C |
| `0x0A` | Fuel pressure (gauge) | `FUEL_PRESSURE` | 1 | 3A | 0 to 765 kPa |
| `0x0B` | Intake manifold absolute pressure | `MAP_PRESSURE` | 1 | A | 0 to 255 kPa |
| `0x0C` | Engine speed | `ENGINE_RPM` | 2 | (256A + B) / 4 | 0 to 16383.75 rpm |
| `0x0D` | Vehicle speed | `VEHICLE_SPEED` | 1 | A | 0 to 255 km/h |
| `0x0F` | Intake air temperature | `INTAKE_AIR_TEMP` | 1 | A − 40 | −40 to 215 °C |
| `0x11` | Throttle position | `THROTTLE_POSITION` | 1 | 100A / 255 | 0 to 100 % |
| `0x2F` | Fuel tank level input | `FUEL_LEVEL` | 1 | 100A / 255 | 0 to 100 % |
| `0x33` | Absolute barometric pressure | `BAROMETRIC_PRESSURE` | 1 | A | 0 to 255 kPa |
| `0x42` | Control module voltage | `PRIMARY_BATTERY` | 2 | (256A + B) / 1000 | 0 to 65.535 V |
| `0x46` | Ambient air temperature | `AMBIENT_TEMP` | 1 | A − 40 | −40 to 215 °C |
| `0x5C` | Engine oil temperature | `OIL_TEMP` | 1 | A − 40 | −40 to 210 °C |
| `0x70` | Boost pressure control | `BOOST_PRESSURE` | 10 | sensor A: (256D + E) / 32, absolute | 0 to 2047.97 kPa |
| `0x78` | Exhaust gas temperature bank 1 | `EGT` | 9 | sensor 1: (256B + C) / 10 − 40 | −40 to 6513.5 °C |

- **Boost (0x70)** is absolute pressure. preOBD adds barometric pressure to the boost sensor's gauge reading, taken from a `BAROMETRIC_PRESSURE` input if you have one, otherwise the standard 101.325 kPa. Byte A is `02` (boost sensor A present). Bytes B–C (commanded boost) and J (control status) are 0.
- **EGT (0x78)**: byte A is `01` (sensor 1 present), and sensors 2–4 read 0.
- **0x70 and 0x78 are multi-frame replies**, longer than one CAN frame. preOBD sends the First Frame, waits for the tool's Flow Control, then sends the rest (ISO 15765-2). The ELM327 emulation prints them the way an ELM327 does.
- Any other single-value J1979 PID in preOBD's import table (`LIST PIDS JSON`) can be assigned with `SET <pin> OBD_PID`, as long as the input measures the same quantity.

## preOBD PIDs 0xAA–0xB7 (need a custom-PID entry)

J1979 has no PID for these quantities, so every tool needs them entered once. The formulas use Torque's syntax. Enter "Equation" as shown, "OBD Mode and PID" as `01` plus the PID (e.g. `01AA`), and the header `7E0` if asked.

| PID | preOBD application | Bytes | Formula (Torque) | Range | Unit |
|-----|--------------------|-------|------------------|-------|------|
| `0xAA` | `CHT` | 2 | `((A*256)+B)/10-40` | −40 to 6513.5 | °C |
| `0xAB` | `TCASE_TEMP` | 2 | `((A*256)+B)/10-40` | | °C |
| `0xAC` | `TRANS_TEMP` | 2 | `((A*256)+B)/10-40` | | °C |
| `0xAD` | `DIFF_TEMP` | 2 | `((A*256)+B)/10-40` | | °C |
| `0xAE` | `GEAR_OIL_TEMP` | 2 | `((A*256)+B)/10-40` | | °C |
| `0xAF` | `INTERCOOLER_TEMP` | 2 | `((A*256)+B)/10-40` | | °C |
| `0xB0` | `OIL_PRESSURE` | 2 | `((A*256)+B)-100` | −100 to 65435 | kPa (gauge) |
| `0xB1` | `HYDRAULIC_PRESSURE` | 2 | `((A*256)+B)-100` | | kPa |
| `0xB2` | `BRAKE_PRESSURE` | 2 | `((A*256)+B)-100` | | kPa |
| `0xB3` | `AUXILIARY_BATTERY` | 2 | `((A*256)+B)/1000` | 0 to 65.535 | V |
| `0xB4` | `HUMIDITY` | 1 | `A*100/255` | 0 to 100 | % |
| `0xB5` | `ELEVATION` | 2 | `((A*256)+B)-500` | −500 to 65035 | m |
| `0xB6` | `COOLANT_LEVEL` | 1 | `A/255` | 1 = OK, 0 = low | |
| `0xB7` | `OIL_LEVEL` | 1 | `A/255` | 1 = OK, 0 = low | |

- Temperatures use J1979's own wide temperature layout (the catalyst-temperature formula), so a CHT or an air-cooled head above 215 °C is not clipped.
- Pressures are gauge, in 1 kPa steps. Vacuum reads negative.
- 0xAA–0xBF is unassigned in the J1979 tables used as sources. A future J1979 revision could assign it. 0xB8–0xBF are free.

## Your own PID

`SET <pin> OBD_PID <pid>` also accepts a PID that neither table defines, such as a manufacturer-style number. preOBD then sends its **legacy encoding** for the input's measurement, with `SET <pin> OBD_LENGTH <1-5>` bytes:

| Measurement | Sent as |
|-------------|---------|
| Temperature | °C + 40 |
| Pressure | bar × 10 |
| Voltage | V × 10 |
| RPM | rpm / 4 |
| Speed | km/h |
| Percent, humidity | % × 2.55 |
| Digital | 0 or 255 |
| Elevation | m |

This is preOBD's own format, not J1979, so enter the matching formula in your tool.

A standard or preOBD PID only accepts an input that measures the same quantity. `SET <pin> OBD_PID 0x0C` on a temperature input is refused (`PID 0x0C carries RPM`). Its length is fixed, and `OBD_LENGTH` keeps the table's value.

## Upgrading from 0.14.0-beta or earlier (breaking)

Earlier firmware answered some sensors on PIDs that J1979 assigns to something else, and encoded standard PIDs in its own format: RPM as rpm/4, MAP and baro as bar × 10, voltage as V × 10. **Inputs keep the PID they were saved with. Nothing is renamed on upgrade.**

- **An input on a standard PID** (coolant 0x05, RPM 0x0C, MAP 0x0B, EGT 0x78 …) now answers with J1979's encoding. Standard tools read it correctly with no setup.
- **An input on one of the old preOBD PIDs** keeps answering there in the old preOBD encoding (see "Your own PID"). To move it to its new PID, set it up again with `SET <pin> <APPLICATION> <SENSOR>`, or run `SET <pin> OBD_PID <new pid>`, then `SAVE`. The old PIDs:

| Application | Old PID | New PID |
|-------------|---------|---------|
| `BOOST_PRESSURE` | `0x6F` (J1979: turbo compressor *inlet* pressure) | `0x70` |
| `CHT` | `0xC8` | `0xAA` |
| `TCASE_TEMP` | `0xC9` | `0xAB` |
| `TRANS_TEMP` | `0xA4` (J1979: transmission gear ratio) | `0xAC` |
| `DIFF_TEMP` | `0xCF` | `0xAD` |
| `GEAR_OIL_TEMP` | `0xD0` | `0xAE` |
| `INTERCOOLER_TEMP` | `0xD2` | `0xAF` |
| `OIL_PRESSURE` | `0xCA` | `0xB0` |
| `HYDRAULIC_PRESSURE` | `0xCD` | `0xB1` |
| `BRAKE_PRESSURE` | `0xCE` | `0xB2` |
| `PRIMARY_BATTERY` | `0xCB` | `0x42` |
| `AUXILIARY_BATTERY` | `0xCC` | `0xB3` |
| `ELEVATION` | `0xA1` (J1979: NOx sensor data) | `0xB5` |
| `COOLANT_LEVEL` | `0xA2` (J1979: cylinder fuel rate) | `0xB6` |
| `OIL_LEVEL` | `0xD1` | `0xB7` |
| `HUMIDITY` | none | `0xB4` |

- Update Torque custom PIDs to the formulas above, and re-import `docs/realdash/preOBD.xml`.

## Protocol Details

### Request Format (ISO 15765-4)

```
Byte 0: Length (0x02 for Mode 01 queries)
Byte 1: Mode (0x01 = Show current data)
Byte 2: PID (Parameter ID to query)
Byte 3-7: Padding (0x00 or 0xAA)
```

### Response Format (ISO 15765-4)

Single frame (up to 5 data bytes):
```
Byte 0: Length (0x02 + data bytes)
Byte 1: Mode + 0x40 (0x41 for Mode 01 responses)
Byte 2: PID (echoed from request)
Byte 3-7: Data (PID-specific format)
```

Multi-frame (0x70, 0x78; ISO 15765-2):
```
First Frame:        10 LL 41 PID d0 d1 d2 d3      LL = total length (mode + PID + data)
Flow Control (tool): 30 BS ST 00 00 00 00 00      sent to 0x7E0; BS/ST = block size, gap (ms)
Consecutive Frames: 21 d4 d5 d6 d7 d8 d9 d10, 22 ...
```

Example: 0.85 bar boost with standard baro is 186 kPa absolute, raw 5950 (0x173E):
```
7E8  10 0C 41 70 02 00 00 17
7E0  30 00 00 00 00 00 00 00
7E8  21 3E 00 00 00 00 00 00
```

### Negative Response (ISO 14229-1)

Sent only to a request addressed to preOBD (`0x7E0`). A request to every ECU
(`0x7DF`) for a PID or service preOBD doesn't have gets no reply at all, as
from a real ECU: the tool sees silence (an ELM327 prints `NO DATA`), not an error.

```
Byte 0: 0x03 (length)
Byte 1: 0x7F (negative response indicator)
Byte 2: Service ID (e.g., 0x01 for Mode 01)
Byte 3: NRC (Negative Response Code)
   0x11 = Service not supported (a mode preOBD doesn't answer)
   0x31 = Request out of range (no input on that PID, or no valid reading)
Byte 4-7: Padding (0x00)
```

### CAN Addressing

**Request IDs:**
- `0x7DF` - Functional addressing (broadcast to all ECUs)
- `0x7E0` - Physical addressing (ECU 0, preOBD); also Flow Control

**Response ID:**
- `0x7E8` - ECU 0 response (preOBD)

---

## See Also

- [SAE J1979 Standard](https://www.sae.org/standards/content/j1979_202104/) - Official OBD-II specification
- [ISO 15765-4](https://www.iso.org/standard/66574.html) - CAN diagnostic protocol
- [OBD-II PIDs on Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs) - Community reference
- [OBD-II Scanner Setup Guide](../guides/outputs/OBD2_SCANNER_GUIDE.md) - Using ELM327 adapters
- [Output PID table source](../../src/sensors/can_sensor_library/obd_output_pids.cpp) - The encodings above, as code
