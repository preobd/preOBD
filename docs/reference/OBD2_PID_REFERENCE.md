# OBD-II PID Reference for openEMS

## Overview

This document provides a complete reference of OBD-II Parameter IDs (PIDs) used by openEMS for SAE J1979 Mode 01 (Show current data) communication.

### What is an OBD-II PID?

A **Parameter ID (PID)** is a standardized code used to request specific sensor data from a vehicle's ECU. openEMS implements Mode 01 PIDs to allow standard OBD-II scanner tools (like Torque) to read sensor data.

### PID Format

**Mode 01 Request:**
```
[Length] [Mode] [PID] [Padding...]
[  0x02] [ 0x01] [0x05] [0x00...]
```

**Mode 01 Response:**
```
[Length] [Mode+0x40] [PID] [Data...] [Padding...]
[  0x03] [    0x41] [0x05] [  0x5A ] [0x00...]
                             ^^^^^
                           90°C = 0x5A (Celsius + 40)
```

---

## Standard SAE J1979 PIDs

openEMS uses standard PIDs where applicable for maximum compatibility with scanner tools.

### Temperature PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x05` | Engine Coolant Temperature | `COOLANT_TEMP` | 1 | A - 40 | -40 to 215°C | °C |
| `0x0F` | Intake Air Temperature | `AMBIENT_TEMP` | 1 | A - 40 | -40 to 215°C | °C |
| `0x46` | Ambient Air Temperature | `AMBIENT_TEMP` | 1 | A - 40 | -40 to 215°C | °C |
| `0x5C` | Engine Oil Temperature | `OIL_TEMP` | 1 | A - 40 | -40 to 210°C | °C |

### Pressure PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x0A` | Fuel Pressure | `FUEL_PRESSURE` | 1 | A * 3 | 0 to 765 kPa | kPa |
| `0x23` | Fuel Rail Pressure | `FUEL_PRESSURE` | 2 | ((A*256)+B) * 10 | 0 to 655,350 kPa | kPa |
| `0x33` | Barometric Pressure | `BAROMETRIC_PRESSURE` | 1 | A | 0 to 255 kPa | kPa |

### RPM and Speed PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x0C` | Engine RPM | `ENGINE_RPM` | 2 | ((A*256)+B)/4 | 0 to 16,383 rpm | rpm |
| `0x0D` | Vehicle Speed | `VEHICLE_SPEED` | 1 | A | 0 to 255 km/h | km/h |

### Voltage PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x42` | Control Module Voltage | `PRIMARY_BATTERY` | 2 | ((A*256)+B)/1000 | 0 to 65.535 V | V |

### Special PIDs

| PID | Name | Description | Data Bytes |
|-----|------|-------------|------------|
| `0x00` | Supported PIDs 01-20 | Bitmap of supported PIDs in range 0x01-0x20 | 4 (bitmap) |
| `0x20` | Supported PIDs 21-40 | Bitmap of supported PIDs in range 0x21-0x40 | 4 (bitmap) |

---

## Manufacturer-Specific PIDs (openEMS Extended)

PIDs 0x40 and above are manufacturer-specific. openEMS uses these for sensors not covered by standard PIDs.

### Extended Temperature PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x78` | Exhaust Gas Temperature | `EGT` | 2 | ((A*256)+B) - 40 | -40 to 655°C | °C |
| `0xC8` | Cylinder Head Temperature | `CHT` | 1 | A - 40 | -40 to 215°C | °C |
| `0xC9` | Transfer Case Temperature | `TCASE_TEMP` | 1 | A - 40 | -40 to 215°C | °C |

### Extended Pressure PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0x6F` | Turbo Boost Pressure | `BOOST_PRESSURE` | 2 | ((A*256)+B) / 100 | 0 to 655.35 bar | bar |
| `0xCA` | Oil Pressure | `OIL_PRESSURE` | 1 | A / 20 | 0 to 12.75 bar | bar |

### Extended Voltage PIDs

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0xCB` | Primary Battery Voltage | `PRIMARY_BATTERY` | 2 | ((A*256)+B) / 1000 | 0 to 65.535 V | V |
| `0xCC` | Auxiliary Battery Voltage | `AUXILIARY_BATTERY` | 2 | ((A*256)+B) / 1000 | 0 to 65.535 V | V |

### Humidity and Altitude

| PID | Name | openEMS Application | Data Bytes | Formula | Range | Units |
|-----|------|---------------------|------------|---------|-------|-------|
| `0xA1` | Relative Humidity | `HUMIDITY` (BME280) | 1 | A / 2.55 | 0 to 100 % | % |
| `0xA2` | Altitude | `ELEVATION` (BME280) | 2 | ((A*256)+B) - 500 | -500 to 65,035 m | m |

---

## PID Encoding Examples

### Example 1: Coolant Temperature (PID 0x05)

**Sensor reading:** 90°C

**Encoding:**
```
OBD-II format = Temperature + 40 = 90 + 40 = 130 = 0x82
```

**Response frame:**
```
[03 41 05 82 00 00 00 00]
 |  |  |  |
 |  |  |  +-- Data: 0x82 (130 decimal)
 |  |  +----- PID: 0x05 (Coolant Temp)
 |  +-------- Mode: 0x41 (Mode 01 response)
 +----------- Length: 0x03 (3 data bytes: mode + PID + 1 data byte)
```

**Decoding:**
```
Temperature = 0x82 - 40 = 130 - 40 = 90°C
```

### Example 2: Engine RPM (PID 0x0C)

**Sensor reading:** 3000 rpm

**Encoding:**
```
OBD-II format = RPM * 4 = 3000 * 4 = 12000 = 0x2EE0
Byte A = 0x2E (high byte)
Byte B = 0xE0 (low byte)
```

**Response frame:**
```
[04 41 0C 2E E0 00 00 00]
 |  |  |  |  |
 |  |  |  |  +-- Data byte B: 0xE0
 |  |  |  +----- Data byte A: 0x2E
 |  |  +-------- PID: 0x0C (Engine RPM)
 |  +----------- Mode: 0x41 (Mode 01 response)
 +-------------- Length: 0x04 (4 data bytes)
```

**Decoding:**
```
RPM = ((0x2E * 256) + 0xE0) / 4
    = (11776 + 224) / 4
    = 12000 / 4
    = 3000 rpm
```

### Example 3: PID 00 - Supported PIDs Bitmap

**Configured sensors:** Coolant Temp (0x05), RPM (0x0C), Speed (0x0D)

**Bitmap generation:**
```
PID 0x05 (Coolant): Byte 0, Bit 7-4 = Bit 2 → 0x20
PID 0x0C (RPM):     Byte 1, Bit 7-4 = Bit 3 → 0x10
PID 0x0D (Speed):   Byte 1, Bit 7-4 = Bit 2 → 0x08

Bitmap = [0x20, 0x18, 0x00, 0x00]
```

**Response frame:**
```
[06 41 00 20 18 00 00 00]
 |  |  |  |  |  |  |
 |  |  |  |  |  |  +-- Bitmap byte D (PIDs 0x19-0x20)
 |  |  |  |  |  +----- Bitmap byte C (PIDs 0x11-0x18)
 |  |  |  |  +-------- Bitmap byte B (PIDs 0x09-0x10): 0x18
 |  |  |  +----------- Bitmap byte A (PIDs 0x01-0x08): 0x20
 |  |  +-------------- PID: 0x00
 |  +----------------- Mode: 0x41
 +-------------------- Length: 0x06 (6 data bytes)
```

---

## OBD Conversion Functions

openEMS uses type-specific conversion functions to encode sensor values in OBD-II format. These are defined in `src/inputs/sensor_read.cpp`.

### Temperature Conversion

```cpp
float obdConvertTemperature(float celsius) {
    return celsius + 40.0;  // OBD-II: -40°C = 0, 0°C = 40, 100°C = 140
}
```

**Range:** -40 to 215°C (1-byte) or -40 to 6553.5°C (2-byte)

### Pressure Conversion

```cpp
float obdConvertPressure(float bar) {
    return bar * 100.0;  // Convert bar to kPa, then encode
}
```

**Range:** 0 to 655.35 kPa (2-byte)

### RPM Conversion

```cpp
float obdConvertRPM(float rpm) {
    return rpm * 4.0;  // OBD-II format: RPM * 4
}
```

**Range:** 0 to 16,383 rpm (2-byte)

### Voltage Conversion

```cpp
float obdConvertVoltage(float volts) {
    return volts * 1000.0;  // Convert to millivolts
}
```

**Range:** 0 to 65.535 V (2-byte)

### Speed Conversion

```cpp
float obdConvertSpeed(float kph) {
    if (kph > 255.0) return 255.0;  // Clamp to max
    return kph;  // Direct value, no conversion
}
```

**Range:** 0 to 255 km/h (1-byte)

---

## Custom PID Assignment

To assign custom PIDs to your sensors, edit `src/lib/application_presets.h`:

```cpp
{
    .name = PSTR_MY_CUSTOM_SENSOR,
    .abbreviation = PSTR_MY_CUSTOM_ABBR,
    .label = PSTR_MY_CUSTOM_LABEL,
    .description = nullptr,
    .defaultSensor = SENSOR_LINEAR_TEMP,
    .defaultUnits = 0,  // Default units index
    .defaultMinValue = -40.0,
    .defaultMaxValue = 150.0,
    .obd2pid = 0xD0,           // Custom PID (manufacturer-specific)
    .obd2length = 2,           // 2-byte response (higher resolution)
    .defaultAlarmEnabled = true,
    .defaultDisplayEnabled = true,
    .expectedMeasurementType = MEASURE_TEMPERATURE,
    .nameHash = 0x1234,        // Compute with djb2_hash()
    .warmupTime_ms = 5000,
    .persistTime_ms = 2000
},
```

**Guidelines:**
- Use standard PIDs (0x01-0x4E) for compatible sensor types
- Use manufacturer-specific PIDs (0x40-0xFF) for custom sensors
- Avoid conflicts with existing assignments
- Use 2-byte responses for high-resolution data

---

## Protocol Details

### Request Format (ISO 15765-4)

```
Byte 0: Length (0x02 for Mode 01 queries)
Byte 1: Mode (0x01 = Show current data)
Byte 2: PID (Parameter ID to query)
Byte 3-7: Padding (0x00 or 0xAA)
```

### Response Format (ISO 15765-4)

```
Byte 0: Length (0x02 + data bytes)
Byte 1: Mode + 0x40 (0x41 for Mode 01 responses)
Byte 2: PID (echoed from request)
Byte 3-7: Data (PID-specific format)
```

### Negative Response (ISO 14229-1)

```
Byte 0: 0x03 (length)
Byte 1: 0x7F (negative response indicator)
Byte 2: Service ID (e.g., 0x01 for Mode 01)
Byte 3: NRC (Negative Response Code)
   0x12 = Sub-function not supported (wrong mode)
   0x31 = Request out of range (unknown PID)
Byte 4-7: Padding (0x00)
```

### CAN Addressing

**Request IDs:**
- `0x7DF` - Functional addressing (broadcast to all ECUs)
- `0x7E0` - Physical addressing (ECU 0, openEMS)

**Response ID:**
- `0x7E8` - ECU 0 response (openEMS)

---

## See Also

- [SAE J1979 Standard](https://www.sae.org/standards/content/j1979_202104/) - Official OBD-II specification
- [ISO 15765-4](https://www.iso.org/standard/66574.html) - CAN diagnostic protocol
- [OBD-II PIDs on Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs) - Community reference
- [OBD-II Scanner Setup Guide](../guides/outputs/OBD2_SCANNER_GUIDE.md) - Using ELM327 adapters
- [Application Presets Source](../../src/lib/application_presets.h) - Full PID assignments

