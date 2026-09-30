# CAN Sensor Import Guide

**Import sensors from a CAN bus (OBD-II PIDs and simple custom frames)**

---

## Overview

preOBD can import sensors from the CAN bus and use them alongside local analog/digital sensors. This allows you to:

- **Monitor vehicle ECU sensors** (engine RPM, coolant temp, vehicle speed) via OBD-II
- **Combine remote and local sensors** (ECU sensors + your custom oil temp sensor)
- **Use dual-CAN setups** (import from vehicle ECU on CAN1, broadcast all sensors on CAN2)
- **Decode simple custom frames** from other CAN devices (configured through JSON)

**Key features:**
- Up to 32 CAN-imported sensors (virtual pins CAN:0 to CAN:31)
- Automatic configuration for ~30 common OBD-II PIDs
- EEPROM persistence (save/load CAN sensor configurations)
- Independent input and output buses (read from one CAN bus, broadcast to another)

> **`ENABLE` polls the ECU; `LISTEN` only listens.** With CAN input set to
> `ENABLE`, preOBD sends an OBD-II Mode 01 request for each imported PID in turn
> (to `0x7E0` for an ECU answering on `0x7E8`), so it can read the ECU with
> nothing else on the bus. It waits for each answer (up to 50 ms), and when
> nothing answers ten requests in a row (ignition off, no ECU) it slows to one
> request per second until something does. With `LISTEN`, preOBD never
> transmits: an imported PID updates only when a *response* for it appears on
> the bus because a scan tool, dash or other module asked for it. If nothing
> is asking, it reads `NaN`. Custom (non-OBD-II) CAN IDs are never polled;
> those devices send without being asked.

---

## Quick Start

### Example: Import Engine RPM from Vehicle ECU

```bash
# Enter configuration mode
> CONFIG

# Enable CAN input on CAN1
> BUS CAN INPUT CAN1 ENABLE

# Import Engine RPM (OBD-II PID 0x0C)
> SET CAN 0x0C
✓ Imported CAN sensor CAN:0 - PID 0x0C (Engine RPM)

# Save configuration
> SAVE

# Exit configuration mode
> RUN

# View sensor data
> LIST INPUTS
Active Inputs:
  CAN:0: RPM (Engine RPM) = 2850 RPM
```

---

## CAN Input Architecture

### Dual-Bus Design

preOBD supports **completely independent** CAN input and output:

```
Input CAN Bus (CAN1)                Output CAN Bus (CAN2)
       ↓                                     ↓
  Vehicle ECU                          RealDash / Torque
       ↓                                     ↓
Import PIDs 0x0C, 0x0D, 0x05          Broadcast all sensors
       ↓                                     ↑
  CAN Frame Cache                            |
       ↓                                     |
  CAN:0 (RPM)                                |
  CAN:1 (Speed)              ← Combine →     |
  CAN:2 (Coolant)                            |
       ↓                                     |
  + Local Sensors                            |
  A0 (Oil Temp)                              |
  A1 (Oil Pressure)                          |
       ↓                                     |
  ────────────────────────────────────────────
```

**Why dual-bus?**
- Read from vehicle ECU on CAN1 (500 kbps)
- Broadcast all sensors (ECU + local) to dashboard on CAN2 (500 kbps)
- Prevents bus conflicts and allows different baud rates per bus

---

## Configuration Commands

### Enable CAN Input

```bash
# Enable CAN input on CAN1
BUS CAN INPUT CAN1 ENABLE

# Enable CAN output on CAN2 (separate from input)
BUS CAN OUTPUT CAN2 ENABLE

# Disable CAN input (keep output active)
BUS CAN INPUT CAN1 DISABLE

# Show CAN bus status
BUS CAN
```

---

## Importing Sensors

### Method 1: SET CAN <pid> (Recommended)

Use this when you **know the PID** you want to import.

```bash
# Import Engine RPM (PID 0x0C)
SET CAN 0x0C

# Import Vehicle Speed (PID 0x0D)
SET CAN 0x0D

# Import Coolant Temperature (PID 0x05)
SET CAN 0x05

# Import Intake Air Temperature (PID 0x0F)
SET CAN 0x0F
```

**What happens:**
1. preOBD allocates the next available CAN virtual pin (CAN:0, CAN:1, etc.)
2. Looks up PID 0x0C in the standard OBD-II PID table
3. Configures sensor with correct calibration (scale factor, offset, data length)
4. Sets display name ("Engine RPM") and units (RPM) from table
5. Enables the sensor automatically

**PID format:**
- Hex: `0x0C`, `0x0D`, `0x05` (preferred)
- Decimal: `12`, `13`, `5` (also works)

### Method 2: SCAN CAN (Discovery)

Use this when you **don't know what PIDs are available** on the bus.

```bash
# Enable CAN input first
BUS CAN INPUT CAN1 ENABLE

# Scan for 15 seconds
SCAN CAN 15000
```

**Output:**
```
Scanning CAN bus for 15000 ms...
Listening for all CAN frames...

[Wait 15 seconds]

=== CAN Scan Complete ===
Found 12 PIDs:

  PID   Name                    CAN ID  Len  Samples
  ----- ----------------------- ------- ---- --------
  0x05  Coolant Temperature     0x7E8   1    450
  0x0C  Engine RPM              0x7E8   2    450
  0x0D  Vehicle Speed           0x7E8   1    450
  0x0F  Intake Air Temp         0x7E8   1    450
  0x11  Throttle Position       0x7E8   1    450

To import a PID: SET CAN <pid_hex>
Example: SET CAN 0x0C (imports Engine RPM)

Type 'SCAN CANCEL' to clear results.
```

Then import sensors using `SET CAN <pid>` as shown above.

---

## Standard OBD-II PIDs

preOBD includes automatic configuration for ~30 common OBD-II PIDs (`LIST PIDS JSON`
prints the full table). The most common:

| PID  | Name                      | Units   | Data Length |
|------|---------------------------|---------|-------------|
| 0x05 | Coolant Temperature       | °C      | 1 byte      |
| 0x0C | Engine RPM                | RPM     | 2 bytes     |
| 0x0D | Vehicle Speed             | km/h    | 1 byte      |
| 0x0F | Intake Air Temperature    | °C      | 1 byte      |
| 0x0B | Intake Manifold Pressure  | kPa     | 1 byte      |
| 0x11 | Throttle Position         | %       | 1 byte      |
| 0x42 | Control Module Voltage    | V       | 2 bytes     |
| 0x2F | Fuel Tank Level           | %       | 1 byte      |
| 0x46 | Ambient Air Temperature   | °C      | 1 byte      |
| 0x5C | Engine Oil Temperature    | °C      | 1 byte      |

**Unknown PIDs:**
If you import a PID not in the table (e.g., manufacturer-specific), preOBD uses default calibration:
```bash
> SET CAN 0x99
✓ Imported CAN sensor CAN:0 - PID 0x99 (unknown PID - using defaults)
  Hint: Use 'SET CAN:0 ...' commands to customize
```

Its scaling can then be set through JSON (see [Custom Frames](#custom-frames)).

---

## Virtual Pin Management

CAN sensors use virtual pins `CAN:0` through `CAN:31`:

```bash
# Import sensors (automatic pin allocation)
> SET CAN 0x0C
✓ Imported CAN sensor CAN:0 - PID 0x0C (Engine RPM)

> SET CAN 0x0D
✓ Imported CAN sensor CAN:1 - PID 0x0D (Vehicle Speed)

# Reference CAN sensors by index in other commands
> RULE ADD CAN:0 OUTSIDE 500 6000
> SET CAN:1 ABBR SPEED
> INFO CAN:0
> SET CAN:1 INPUT ENABLE
> CLEAR CAN:0
```

**Virtual pin allocation:**
- `SET CAN` automatically assigns the next available pin (CAN:0, CAN:1, ...)
- `SET CAN:0`, `SET CAN:1` reference existing sensors by index
- Maximum 32 CAN sensors (CAN:0 to CAN:31)

---

## Customization

### Modify Imported Sensors

Once imported, CAN sensors support all standard SET commands:

```bash
# Change labels
SET CAN:0 ABBR ERPM
SET CAN:0 NAME Engine RPM

# Set alarm thresholds (band; setting it arms the alarm Rule)
SET CAN:0 ALARM 500 6000            # Alarm if <500 or >6000 RPM

# Change units
SET CAN:1 UNITS MPH          # Change speed from km/h to MPH

# Stale-data timeout (default 2000 ms)
SET CAN:0 CAN_TIMEOUT 500

# Report it on preOBD's own OBD-II output under a PID
SET CAN:0 OBD_PID 0x0C
SET CAN:0 OBD_LENGTH 2
```

### Custom Frames

For a PID preOBD doesn't know, or a frame from another CAN device, the
decoding is set through a [JSON import](../configuration/JSON_CONFIGURATION_GUIDE.md) —
there is no serial command for it. Import the input first (`SET CAN 0x99`), export
with `SYSTEM DUMP JSON`, edit that input's calibration, and import it back:

```json
"calibration": {
  "type": "CAN_IMPORT",
  "source": "CUSTOM",
  "params": {
    "sourceCanId": 1024,
    "sourcePid": 153,
    "dataOffset": 0,
    "dataLength": 2,
    "isBigEndian": true,
    "scaleFactor": 0.25,
    "offset": 0.0,
    "timeoutMs": 2000
  }
}
```

How a frame is matched:
- **OBD-II Mode 01 responses** (mode byte `0x41`): matched on the CAN ID and the PID; data starts after the PID.
- **Any other frame**: matched on the CAN ID and **the first data byte**, which is treated as an identifier; data starts after it.
- The value is `raw × scaleFactor + offset`, reading `dataLength` bytes (1-4) starting `dataOffset` bytes into the data.
- The result must be in preOBD's base unit for its quantity: °C, **bar** (not kPa: a kPa value needs a scale 100× smaller), km/h, V, RPM, %. The display converts from there to the input's units.

**Limitations:** CAN IDs are stored as 16 bits, and a non-OBD frame must carry a
fixed identifier in its first byte. That fits many simple 11-bit broadcast
protocols, but **not J1939**, whose 29-bit IDs and data layout don't match this
scheme. J1939 decoding is not supported yet.

---

## Dual-Bus Example Workflow

### Scenario: Monitor Vehicle ECU + Local Sensors

**Goal:**
- Import engine RPM, speed, coolant temp from vehicle ECU (CAN1)
- Add local oil temp and oil pressure sensors (analog pins)
- Broadcast all sensors to RealDash (CAN2)

**Setup:**

```bash
# Enter configuration mode
CONFIG

# Poll the vehicle ECU on CAN1 (use LISTEN instead if a scan tool is already polling)
BUS CAN INPUT CAN1 ENABLE 500000   # Requests the imported PIDs from the ECU
BUS CAN OUTPUT CAN2 ENABLE 500000  # Broadcast to RealDash on CAN2

# Import CAN sensors from vehicle ECU
SET CAN 0x0C    # Engine RPM → CAN:0
SET CAN 0x0D    # Vehicle Speed → CAN:1
SET CAN 0x05    # Coolant Temp → CAN:2

# Configure local sensors
SET A0 OIL_TEMP VDO_150C_STEINHART    # Oil temp → A0
SET A1 OIL_PRESSURE VDO_5BAR_CURVE    # Oil pressure → A1

# Set alarms
RULE ADD CAN:0 OUTSIDE 500 6000              # RPM alarm
RULE ADD A0 OUTSIDE 50 130                   # Oil temp alarm
RULE ADD A1 OUTSIDE 1.0 6.0                  # Oil pressure alarm

# Enable outputs
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100               # Broadcast every 100ms

# Save configuration
SAVE

# Exit configuration mode
RUN
```

**Result:**
- CAN1 receives frames from vehicle ECU → populates cache
- CAN sensors read from cache (CAN:0, CAN:1, CAN:2)
- Local sensors read from analog pins (A0, A1)
- All 5 sensors broadcast together on CAN2 to RealDash

---

## Troubleshooting

### No PIDs Detected During Scan

**Symptoms:**
```
=== CAN Scan Complete ===
No PIDs detected during scan period.
Ensure CAN input is enabled and bus is active.
```

**Solutions:**

1. **Check CAN input is enabled:**
   ```bash
   BUS CAN
   # Output should show: CAN Input: CAN1 (ENABLED, 500000 bps)
   ```

2. **Verify physical wiring:**
   - CAN_H and CAN_L connected correctly
   - 120Ω termination resistors at both ends of bus
   - Good ground connection

3. **Check baud rate:**
   ```bash
   BUS CAN INPUT BAUDRATE 500000   # Most vehicles use 500 kbps
   BUS CAN INPUT BAUDRATE 250000   # Some buses run at 250 kbps
   ```

4. **Pick the input mode for what else is on the bus:**
   ```bash
   BUS CAN INPUT CAN1 ENABLE 500000   # preOBD polls the ECU itself
   BUS CAN INPUT CAN1 LISTEN 500000   # No transmit; another tool must be polling
   ```

5. **Verify vehicle ECU is active:**
   - Engine running or ignition ON
   - Some vehicles only broadcast with engine running

6. **Check for OBD-II activity:**
   - Try connecting an OBD-II scanner first
   - Verify vehicle has OBD-II (post-1996 for US vehicles)

### Sensor Shows NAN (Not a Number)

**Symptoms:**
```
CAN:0: RPM (Engine RPM) = NAN RPM
```

**Causes:**

1. **No CAN data received** (most common) — `INFO CAN:0` shows `Last frame: never`
   - CAN input not enabled
   - CAN input in `LISTEN` mode and nothing else on the bus is requesting this PID
   - The ECU does not support this PID (it ignores the request)

2. **Data timeout** (2 second default)
   - Frame was received but is now stale
   - ECU stopped broadcasting

**Solutions:**

```bash
# Check if data is being received
INFO CAN:0

# Verify CAN input is enabled
BUS CAN

# Try re-scanning to see if PID is present
SCAN CAN 10000
```

### CAN Input and Output Interfere

**Symptoms:**
- Bus errors when both enabled
- Garbled data
- Timeouts

**Solution:** Use separate physical CAN buses

```bash
# Input on CAN1, output on CAN2
BUS CAN INPUT CAN1 ENABLE
BUS CAN OUTPUT CAN2 ENABLE
```

**Hardware:**
- Teensy 4.1: Has CAN1 and CAN2 (requires external transceivers)
- Use MCP2551 or SN65HVD230 CAN transceivers
- See [CAN Transceiver Guide](../hardware/CAN_TRANSCEIVER_GUIDE.md)

---

## Advanced Topics

### Frame Cache Details

Received frames are kept in a small cache that sensors read from:

- **Size:** 16 entries (8 on the Arduino Mega)
- **Timeout:** a value older than the input's CAN timeout (default 2000 ms) reads as `NaN`

If more distinct frames/PIDs are active than the cache holds, the least recently
seen are evicted, so low-rate values can drop out on a busy bus.

---

## Examples

### Example 1: Basic Vehicle Monitoring

Monitor essential vehicle parameters from ECU:

```bash
CONFIG
BUS CAN INPUT CAN1 ENABLE
SET CAN 0x0C    # Engine RPM
SET CAN 0x0D    # Vehicle Speed
SET CAN 0x05    # Coolant Temperature
SET CAN 0x11    # Throttle Position
SAVE
RUN
```

### Example 2: Hybrid Setup (ECU + Local Sensors)

Combine vehicle ECU sensors with custom sensors:

```bash
CONFIG

# Import from ECU
BUS CAN INPUT CAN1 ENABLE
SET CAN 0x0C    # Engine RPM
SET CAN 0x0D    # Vehicle Speed

# Add local sensors
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A1 OIL_PRESSURE VDO_5BAR_CURVE
SET 6 EGT MAX6675

# Broadcast everything to dashboard
BUS CAN OUTPUT CAN2 ENABLE
OUTPUT CAN ENABLE

SAVE
RUN
```

### Example 3: Listen-Only Input, Separate Output Bus

```bash
CONFIG

# Read the vehicle bus passively (no ACK, no transmit)
BUS CAN INPUT CAN1 LISTEN 500000

# Broadcast to the dashboard on its own bus
BUS CAN OUTPUT CAN2 ENABLE 500000

SET CAN 0x0C    # Engine RPM
SET CAN 0x05    # Coolant Temperature

SAVE
RUN
```

---

## Limitations

1. **Polling needs `ENABLE`** - `LISTEN` is passive; see the note at the top. While preOBD polls, `SCAN CAN` shows the ECU's answers to its requests (only PIDs the ECU supports) plus anything else on the bus
2. **Frame cache size** - received frames are cached per (CAN ID, PID) in a small table (16 entries on Teensy/ESP32, 8 on the Mega). Importing more PIDs than that, or a busy bus with other traffic, can evict a PID between polls, so it flickers to `NaN` and `INFO CAN:<n>` shows `Last frame: never`
3. **Ignition off** - with nothing answering, `ENABLE` keeps sending one request per second. If no other node is powered to acknowledge it, the CAN controller retransmits that frame continuously. Power preOBD from switched ignition, or use `LISTEN`, if that bus activity could keep other modules awake
4. **OBD-II Mode 01 only** - single-frame PIDs; no ISO-TP multi-frame reassembly (Mode 09, Mode 22)
5. **No J1939** - 29-bit IDs are not decoded (see [Custom Frames](#custom-frames))

### Platform Support

| Platform | CAN Buses | Notes |
|----------|-----------|-------|
| Teensy 4.1 / 4.0 | 3 (CAN1-CAN3) | Native FlexCAN; external transceivers required |
| Teensy 3.6 | 2 (CAN1, CAN2) | Native FlexCAN; external transceivers required |
| ESP32-S3 | 1 (TWAI) | External transceiver required |
| Arduino Mega | 1 | MCP2515 SPI controller + transceiver |

---

## Related Documentation

- **[CAN Transceiver Guide](../hardware/CAN_TRANSCEIVER_GUIDE.md)** - Hardware setup for CAN buses
- **[OBD-II Scanner Guide](../outputs/OBD2_SCANNER_GUIDE.md)** - CAN output configuration
- **[BUS Commands Reference](../../reference/SERIAL_COMMANDS.md)** - Complete BUS command syntax
- **[Standard PID Reference](../../reference/OBD2_PID_REFERENCE.md)** - Full OBD-II PID table

