# CAN Sensor Import Guide

**Import sensors from CAN bus (OBD-II, J1939, custom protocols)**

---

## Overview

preOBD can import sensors from the CAN bus and use them alongside local analog/digital sensors. This allows you to:

- **Monitor vehicle ECU sensors** (engine RPM, coolant temp, vehicle speed) via OBD-II
- **Combine remote and local sensors** (ECU sensors + your custom oil temp sensor)
- **Use dual-CAN setups** (import from vehicle ECU on CAN1, broadcast all sensors on CAN2)
- **Support custom protocols** (J1939, proprietary CAN protocols)

**Key features:**
- Up to 32 CAN-imported sensors (virtual pins CAN:0 to CAN:31)
- Automatic configuration for ~30 common OBD-II PIDs
- EEPROM persistence (save/load CAN sensor configurations)
- Independent input and output buses (read from one CAN bus, broadcast to another)

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
BUS CAN INPUT DISABLE

# Show CAN bus status
BUS CAN
```

**Output:**
```
CAN Input:  CAN1 (ENABLED, 500000 bps)
CAN Output: CAN2 (ENABLED, 500000 bps)
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

Use this when you **don't know what PIDs are available**.

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

Use SET CAN <pid> to import a sensor.
Type 'SCAN CANCEL' to clear results.
```

Then import sensors using `SET CAN <pid>` as shown above.

---

## Standard OBD-II PIDs

preOBD includes automatic configuration for ~30 common OBD-II PIDs:

| PID  | Name                      | Units   | Data Length |
|------|---------------------------|---------|-------------|
| 0x05 | Coolant Temperature       | °C      | 1 byte      |
| 0x0C | Engine RPM                | RPM     | 2 bytes     |
| 0x0D | Vehicle Speed             | km/h    | 1 byte      |
| 0x0F | Intake Air Temperature    | °C      | 1 byte      |
| 0x10 | MAF Air Flow Rate         | g/s     | 2 bytes     |
| 0x11 | Throttle Position         | %       | 1 byte      |
| 0x21 | Distance Since MIL        | km      | 2 bytes     |
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

Then manually configure with SET commands (see Customization section).

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
> SET CAN:0 ALARM 500 6000
> SET CAN:1 NAME "SPEED"
> INFO CAN:0
> ENABLE CAN:1
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
# Change display name
SET CAN:0 NAME "ERPM"
SET CAN:0 DISPLAY_NAME "Engine RPM"

# Set alarm thresholds
SET CAN:0 ALARM 500 6000     # Min 500, Max 6000 RPM
SET CAN:0 ALARM ENABLE

# Change units
SET CAN:1 UNITS MPH          # Change speed from km/h to MPH

# Configure output PID
SET CAN:0 OBD 0x0C 2         # Broadcast as PID 0x0C (2 bytes)
```

### Manual CAN Calibration

For unknown PIDs or custom protocols, manually configure calibration:

```bash
# Import unknown PID
SET CAN 0x99

# Configure CAN source (CAN ID and PID)
SET CAN:0 CAN SOURCE 0x400 0x99

# Configure data format
SET CAN:0 CAN FORMAT 2 BIGENDIAN SCALE 0.25 OFFSET 0.0
```

**CAN FORMAT syntax:**
```
SET <pin> CAN FORMAT <bytes> <BIGENDIAN|LITTLEENDIAN> SCALE <factor> OFFSET <offset>
```

**Parameters:**
- `<bytes>`: Number of data bytes (1-4)
- `BIGENDIAN`: Most significant byte first (OBD-II standard)
- `LITTLEENDIAN`: Least significant byte first (some protocols)
- `SCALE <factor>`: Multiply raw value by this (e.g., 0.25 for RPM)
- `OFFSET <offset>`: Add this to scaled value (e.g., -40 for temperature)

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

# Configure CAN buses with listen-only mode for vehicle ECU monitoring
BUS CAN INPUT CAN1 LISTEN 500000   # Passive read from vehicle ECU (no ACK/TX)
BUS CAN OUTPUT CAN2 ENABLE 500000  # Broadcast to RealDash on CAN2

# Import CAN sensors from vehicle ECU
SET CAN 0x0C    # Engine RPM → CAN:0
SET CAN 0x0D    # Vehicle Speed → CAN:1
SET CAN 0x05    # Coolant Temp → CAN:2

# Configure local sensors
SET A0 OIL_TEMP VDO_150C_STEINHART    # Oil temp → A0
SET A1 OIL_PRESSURE VDO_5BAR_CURVE    # Oil pressure → A1

# Set alarms
SET CAN:0 ALARM 500 6000              # RPM alarm
SET A0 ALARM 50 130                   # Oil temp alarm
SET A1 ALARM 1.0 6.0                  # Oil pressure alarm

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
   BUS CAN INPUT BAUDRATE 250000   # Some older vehicles (J1939) use 250 kbps
   ```

4. **Use listen-only mode for vehicle ECU buses:**
   ```bash
   BUS CAN INPUT CAN1 LISTEN 500000   # Passive monitoring (recommended for vehicles)
   # Listen mode prevents disrupting vehicle ECU communication
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

1. **No CAN data received** (most common)
   - Frame cache is empty for this PID
   - CAN input not enabled
   - Vehicle ECU not broadcasting this PID

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

### J1939 Protocol Support

J1939 uses 29-bit extended CAN IDs and different addressing:

```bash
# Import J1939 PGN (example: Engine Speed from address 0)
SET CAN:0 CAN SOURCE 0x18F00400 0x01
SET CAN:0 CAN FORMAT 2 LITTLEENDIAN SCALE 0.125 OFFSET 0.0
SET CAN:0 NAME "J1939RPM"
```

**Note:** J1939 support is basic in this release. Future versions will add PGN tables and automatic configuration.

### Custom CAN Protocols

For proprietary CAN protocols:

```bash
# Import sensor with custom CAN ID and identifier
SET CAN 0x99                          # Import unknown PID
SET CAN:0 CAN SOURCE 0x300 0x10       # CAN ID 0x300, identifier byte 0x10
SET CAN:0 CAN FORMAT 4 LITTLEENDIAN SCALE 0.01 OFFSET -100.0
SET CAN:0 NAME "CUSTOM"
```

### Frame Cache Details

preOBD uses a circular buffer to cache CAN frames:

- **Size:** 16 entries (200 bytes RAM)
- **Replacement:** LRU (Least Recently Used)
- **Timeout:** 2000ms (frames older than 2 seconds treated as stale)
- **Hash function:** `(can_id ^ pid) & 0x0F`

**Implications:**
- If >16 different PIDs are active, oldest ones get evicted
- High-frequency PIDs (RPM, speed) stay in cache
- Low-frequency PIDs (fuel level) may get evicted if many sensors configured

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

### Example 3: Dual-Bus with Separate Baud Rates (Mixed Protocols)

```bash
CONFIG

# Configure input bus (J1939 vehicle ECU at 250 kbps, listen-only)
BUS CAN INPUT CAN1 LISTEN 250000    # Passive monitoring of J1939 bus

# Configure output bus (OBD-II/RealDash at 500 kbps)
BUS CAN OUTPUT CAN2 ENABLE 500000   # Active broadcast to dashboard

# Import J1939 sensors from vehicle
SET CAN:0 ENGINE_RPM                 # Map CAN:0 to engine RPM
SET CAN:1 COOLANT_TEMP               # Map CAN:1 to coolant temp

SAVE
RUN
```

---

## Limitations

### Current Version (v0.7.0-beta)

1. **OBD-II focus** - Standard PID table only includes OBD-II Mode 01 PIDs
2. **No multi-frame support** - Only single-frame PIDs (no ISO-TP reassembly for Mode 09, etc.)
3. **Basic J1939** - No PGN table or automatic J1939 configuration
4. **Static config not supported** - CAN sensor import requires CONFIG/RUN mode with EEPROM

### Platform Limitations

| Platform | CAN Buses | Notes |
|----------|-----------|-------|
| Teensy 4.1 | 3 (CAN1, CAN2, CAN3) | Requires external transceivers |
| Teensy 4.0 | 2 (CAN1, CAN2) | Requires external transceivers |
| ESP32 | 1 (TWAI) | Built-in transceiver on some boards |
| Arduino Mega | 0 | Requires MCP2515 shield |

---

## Related Documentation

- **[CAN Transceiver Guide](../hardware/CAN_TRANSCEIVER_GUIDE.md)** - Hardware setup for CAN buses
- **[OBD-II Scanner Guide](../outputs/OBD2_SCANNER_GUIDE.md)** - CAN output configuration
- **[BUS Commands Reference](../../reference/SERIAL_COMMANDS.md)** - Complete BUS command syntax
- **[Standard PID Reference](../../reference/OBD2_PID_REFERENCE.md)** - Full OBD-II PID table

---

## Future Enhancements

Planned for future releases:

- **ISO-TP support** - Multi-frame PID reassembly (Mode 09, Mode 22)
- **J1939 PGN table** - Automatic configuration for J1939 sensors
- **Interactive import** - Select PIDs from SCAN results directly
- **CAN message logging** - Log raw CAN frames to SD card
- **Hardware CAN filters** - Reduce CPU load by filtering at hardware level
