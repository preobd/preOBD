# EEPROM Structure and Versioning

**Technical guide to preOBD EEPROM layout and version management**

---

## Table of Contents

1. [Overview](#overview)
2. [EEPROM Layout](#eeprom-layout)
3. [Version History](#version-history)
4. [Input Configuration Storage](#input-configuration-storage)
5. [System Configuration Storage](#system-configuration-storage)
6. [Checksum Validation](#checksum-validation)
7. [Version Migration](#version-migration)
8. [EEPROM Wear Management](#eeprom-wear-management)
9. [Troubleshooting](#troubleshooting)

---

## Overview

preOBD uses EEPROM (Electrically Erasable Programmable Read-Only Memory) to persist configuration across power cycles. The EEPROM structure uses **hash-based storage** for portability across firmware versions.

**Key Concepts:**
- **Versioned storage:** Each EEPROM write includes version number
- **Hash-based:** Sensor/application names stored as hashes, not indices
- **Checksum validation:** Detects corruption from power loss or wear
- **Auto-reset:** Incompatible versions trigger clean reset
- **Dual configuration:** Input configs + System config stored separately

---

## EEPROM Layout

### Two-Block Model

preOBD splits EEPROM into two blocks that grow toward each other:

- **Input block** grows up from address 0 (header + N × `InputEEPROM`)
- **SystemConfig** is anchored at the **end** of EEPROM, so its address adapts to the platform automatically

```
┌──────────────────────────────────────────────────────────────┐
│ 0x0000          EEPROM Header (8 bytes)                      │
│ 0x0008 …        InputEEPROM[0]                               │
│                 InputEEPROM[1]                               │
│                 …                                            │
│                 InputEEPROM[MAX_EEPROM_INPUTS - 1]           │
│                 (free space)                                 │
│ END − sizeof(SystemConfig)                                   │
│                 SystemConfig (~140 bytes, grows with schema) │
│ E2END                                                        │
└──────────────────────────────────────────────────────────────┘
```

```cpp
constexpr size_t SYSTEM_CONFIG_ADDRESS =
    EEPROM_TOTAL_BYTES - sizeof(SystemConfig);

constexpr size_t MAX_EEPROM_INPUTS =
    (SYSTEM_CONFIG_ADDRESS - sizeof(EEPROMHeader)) / sizeof(InputEEPROM);
```

A compile-time `static_assert` prevents the two regions from overlapping.

**Why anchored at the end?** Earlier firmware used a hardcoded `SYSTEM_CONFIG_ADDRESS`. On platforms with smaller EEPROM than expected (Teensy 4.0, 1080 bytes) the hardcoded address fell past `E2END` and writes silently failed. Anchoring SystemConfig at the end makes the layout self-adjust per platform.

### Platform Capacity

`MAX_INPUTS` is a **RAM** limit (number of `Input` structs allocated). `MAX_EEPROM_INPUTS` is a **persistence** limit derived from EEPROM size. They can differ:

| Platform | EEPROM | `MAX_INPUTS` (RAM) | `MAX_EEPROM_INPUTS` (persists) |
|----------|--------|--------------------|--------------------------------|
| Teensy 4.1 | 4284 bytes | 40 | ~40 (full RAM set persists) |
| Teensy 4.0 | 1080 bytes | 40 | ~10 (extras truncated on save) |
| Arduino Mega | 4096 bytes | 40 | ~40 |
| ESP32 | Emulated, configurable | 40 | depends on partition |

When `numActiveInputs > MAX_EEPROM_INPUTS`, `saveInputConfig()` writes the first `MAX_EEPROM_INPUTS` inputs and prints a warning to the control plane:

```
⚠ EEPROM holds 10 inputs; truncating 14 — extras will not persist across reboot
```

The extra inputs continue to function until reboot — they're configured in RAM, just not written to EEPROM.

---

## Version History

There are two independent version numbers:

- **`EEPROM_VERSION`** (currently `4`) — input block schema (`EEPROMHeader` + `InputEEPROM[]`).
- **`SYSTEM_CONFIG_VERSION`** (currently `10`) — `SystemConfig` schema. Bumped whenever the struct's layout or address changes.

A version mismatch on either block is non-fatal: the affected block resets to defaults on next boot, the user re-saves, and operation continues. The two blocks version independently — bumping `SYSTEM_CONFIG_VERSION` does not invalidate input configs and vice versa.

### Input Block Version Progression

| Version | Firmware | Key Changes |
|---------|----------|-------------|
| **v1** | v0.3.x | Enum-based system; stored sensor/app indices |
| **v2** | v0.4.0+ | Registry architecture; switched to hash-based storage |
| **v3** | — | Internal layout iterations |
| **v4** | current | Added `divider_ratio` to the per-input record |

### SystemConfig Version Progression

| Version | Key Changes |
|---------|-------------|
| **v3** | Earlier baseline (output enables, display, units, intervals, pins, sea-level) |
| **v4** | Added transport router config |
| **v5** | Added relay config (`ENABLE_RELAY_OUTPUT` builds) |
| **v6** | Added log filter config |
| **…**  | Bus config, serial port config |
| **v10** | Layout shift — `SystemConfig` now anchored at the end of EEPROM (per-platform address) |
| **v11** | Removed system pin fields (`modeButtonPin`, `buzzerPin`, `canCSPin`, `canIntPin`, `sdCSPin`, `testModePin`) — pins are compile-time constants in the board profile, not runtime state |

> **Note:** v9→v10 is a pure address change, not a struct change. T4.1 users who upgrade past v10 will see SystemConfig reset to defaults on first boot and need to re-`SAVE`. Input configs are unaffected.
>
> **v10→v11:** Struct change (pin fields removed). SystemConfig resets to defaults on first boot. Re-`SAVE` to persist transport routing and output settings. Input configs are unaffected.

### Detail: Version 1 (v0.3.x - Deprecated)

**Characteristics:**
- Sensor and application stored as **enum indices**
- No hash-based lookups
- Fragile: Adding sensors mid-enum broke existing EEPROMs

**Problem:**
If you inserted a new sensor into the enum:
```cpp
enum Sensor {
    SENSOR_NONE = 0,
    SENSOR_NEW_SENSOR = 1,  // INSERTED HERE!
    SENSOR_MAX6675 = 2,     // Was 1, now 2 - BREAKS EEPROM!
};
```

All saved configurations became invalid.

---

### Version 2 (v0.4.0+ - Current)

**Introduced:** Registry architecture (hash-based storage)

**Key Changes:**
- Replaced enum indices with **16-bit hash values**
- Hash computed from sensor/application **name strings**
- Portable across firmware versions (hash stays constant)
- Added `unitsHash` for unit conversion

**Benefits:**
- Adding sensor doesn't break EEPROM (hash remains same)
- Can reorder registry without affecting saved configs
- Foundation for future JSON-based custom sensors

---

## Input Configuration Storage

### EEPROM Header (8 bytes @ 0x0000)

```cpp
struct EEPROMHeader {
    uint32_t magic;       // 0x4F454D53 ("OEMS" in ASCII)
    uint16_t version;     // EEPROM_VERSION (currently 4)
    uint8_t numInputs;    // 0–MAX_EEPROM_INPUTS (platform-dependent)
    uint8_t reserved;     // Stores XOR checksum
};
```

**Purpose:**
- **Magic:** Detect uninitialized EEPROM (0xFFFFFFFF on new chips)
- **Version:** Trigger reset if mismatch
- **numInputs:** How many InputEEPROM structs follow
- **reserved:** Stores checksum for corruption detection

### InputEEPROM Structure (~84 bytes each, depends on alignment)

```cpp
struct InputEEPROM {
    // Hardware (1 byte)
    uint8_t pin;

    // User Configuration - stored as hashes (46 bytes)
    char abbrName[8];              // "CHT", "OIL"
    char displayName[32];          // "Cylinder Head Temp"
    uint16_t applicationHash;      // djb2_hash of application name
    uint16_t sensorHash;           // djb2_hash of sensor name
    uint16_t unitsHash;            // djb2_hash of units name

    // Alarm Thresholds (8 bytes)
    float minValue;
    float maxValue;

    // OBDII (2 bytes)
    uint8_t obd2pid;
    uint8_t obd2length;

    // Flags (1 byte)
    uint8_t flagsByte;             // Packed: enabled, alarm, display, useCustomCal

    // Calibration (17 bytes)
    uint8_t calibrationType;
    CalibrationOverride customCalibration;  // 16-byte union
};
```

**Size:** ~84 bytes per input on Teensy (ARM alignment); slightly less on AVR. Use `sizeof(InputEEPROM)` at runtime — the layout is alignment-sensitive.

### Input Slots Layout

Slots are packed back-to-back starting at `EEPROM_HEADER_SIZE` (0x0008). The number of slots that fit is `MAX_EEPROM_INPUTS`, computed at compile time from the gap between the header and `SYSTEM_CONFIG_ADDRESS` (see [Two-Block Model](#two-block-model)).

```
Address                                  | Content
-----------------------------------------|---------------------------
0x0008                                   | InputEEPROM[0]
0x0008 + 1·sizeof(InputEEPROM)           | InputEEPROM[1]
…                                        | …
0x0008 + (MAX_EEPROM_INPUTS-1)·sizeof()  | last persisted slot
```

### Flags Byte Packing

```cpp
flagsByte =
    (isEnabled           ? 0x01 : 0) |
    (alarm               ? 0x02 : 0) |
    (display             ? 0x04 : 0) |
    (useCustomCalibration? 0x08 : 0);
```

### Calibration Override Union

```cpp
union CalibrationOverride {
    ThermistorSteinhartCalibration steinhart;
    PressureLinearCalibration pressureLinear;
    PressurePolynomialCalibration pressurePolynomial;
    RPMCalibration rpm;
    byte raw[16];  // 16-byte union
};
```

**Size:** 16 bytes per input (always allocated, even if unused)

---

## System Configuration Storage

### SystemConfig Structure (~140 bytes, anchored at end of EEPROM)

```cpp
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 ("SC")
    uint8_t version;             // SYSTEM_CONFIG_VERSION (currently 10)
    uint8_t checksum;            // XOR checksum

    // Output Modules (NUM_OUTPUTS = 7: CAN, RealDash, Serial, SD, Alarm, Relay, ELM327)
    uint8_t outputEnabled[NUM_OUTPUTS];
    uint16_t outputInterval[NUM_OUTPUTS];

    // Display, units, intervals, sea-level pressure
    // (see src/lib/system_config.h for the full layout)
    // Note: hardware pin assignments are NOT stored here — they are compile-time
    // constants defined in the board profile (src/profiles/profile_*.h)

    // Transport router (v4+) — control/data/debug primary+secondary, BT
    struct { /* ... */ } router;

    // Relays (v5+, ENABLE_RELAY_OUTPUT only)
    RelayConfig relays[MAX_RELAYS];

    // Bus + serial port config — which I2C/SPI/CAN bus, which serial ports
    BusConfig buses;
    SerialPortConfig serial;

    // Log filter (v6+) — per-plane log levels and tag bitmap
    struct { /* ... */ } logFilter;
};
```

The struct above is a sketch — `SystemConfig` has grown across versions and may grow again. Treat [`src/lib/system_config.h`](../../src/lib/system_config.h) as authoritative.

**Location:** `EEPROM_TOTAL_BYTES − sizeof(SystemConfig)` (per platform). On Teensy 4.1 (4284-byte EEPROM) this is roughly `0x1080`; on Teensy 4.0 (1080-byte EEPROM) it's roughly `0x03B0`. The exact address is computed at compile time from `E2END`.

**Default Values:**
Set from board profile and `config.h` at first boot:
- Feature flags (`ENABLE_CAN`, etc.) determine which output slots default ON/OFF
- `DEFAULT_TEMPERATURE_UNITS`, `DEFAULT_PRESSURE_UNITS` etc. from `config.h`
- Hardware pin assignments are NOT stored — they are always read directly from the board profile at compile time

---

## Checksum Validation

### Input Checksum

**Algorithm:** XOR of all InputEEPROM struct bytes
```cpp
uint8_t calculateConfigChecksum() {
    uint8_t checksum = 0;
    uint16_t addr = EEPROM_HEADER_SIZE;

    for (uint8_t i = 0; i < numActiveInputs; i++) {
        InputEEPROM eepromInput;
        EEPROM.get(addr, eepromInput);

        const uint8_t* data = (const uint8_t*)&eepromInput;
        for (size_t k = 0; k < sizeof(InputEEPROM); k++) {
            checksum ^= data[k];
        }
        addr += sizeof(InputEEPROM);
    }
    return checksum;
}
```

**Validation on load:**
```cpp
uint8_t storedChecksum = header.reserved;
uint8_t calculatedChecksum = calculateConfigChecksum();

if (storedChecksum != calculatedChecksum) {
    Serial.println("ERROR: EEPROM checksum mismatch!");
    // Clear corrupted data and reset
}
```

### SystemConfig Checksum

**Algorithm:** XOR of all SystemConfig fields (excluding checksum itself)
```cpp
uint8_t calculateChecksum(SystemConfig* cfg) {
    uint8_t checksum = 0;
    byte* ptr = (byte*)cfg;

    // XOR all bytes except the checksum field
    for (size_t i = 0; i < offsetof(SystemConfig, checksum); i++) {
        checksum ^= ptr[i];
    }
    for (size_t i = offsetof(SystemConfig, checksum) + 1;
         i < sizeof(SystemConfig); i++) {
        checksum ^= ptr[i];
    }

    return checksum;
}
```

**Why Checksums?**
- **Power loss during write:** Partial write detected
- **EEPROM wear:** Bits flipping detected
- **Memory corruption:** Random bit errors caught

---

## Version Migration

### Boot-Time Version Check

**Sequence:**
1. Read EEPROM Header
2. Check magic number (0x4F454D53)
3. Compare version to `EEPROM_VERSION` constant
4. If mismatch → Reset to defaults

**Code Flow:**
```cpp
EEPROMHeader header;
EEPROM.get(0, header);

if (header.magic != EEPROM_MAGIC) {
    Serial.println("EEPROM uninitialized");
    return false;  // Will use defaults
}

if (header.version != EEPROM_VERSION) {
    Serial.print("EEPROM version mismatch (found ");
    Serial.print(header.version);
    Serial.print(", expected ");
    Serial.print(EEPROM_VERSION);
    Serial.println(") - ignoring");
    return false;  // Will use defaults
}

// Version matches, load config
loadInputConfig();
```

### SystemConfig Version Check

**Similar process:**
```cpp
SystemConfig temp;
EEPROM.get(SYSTEM_CONFIG_ADDRESS, temp);

if (temp.magic != SYSTEM_CONFIG_MAGIC ||
    temp.version != SYSTEM_CONFIG_VERSION) {
    Serial.println("SystemConfig version mismatch");
    resetSystemConfig();  // Load compile-time defaults
    saveSystemConfig();   // Write to EEPROM
}
```

### Why No Auto-Migration?

**Design decision:** Reset instead of migrate

**Reasons:**
1. **Simplicity:** Migration code complex and error-prone
2. **Safety:** Partial migration worse than clean slate
3. **Infrequency:** Firmware updates rare, re-config acceptable
4. **Clarity:** User knows config was reset, not silently altered

---

## EEPROM Wear Management

### Write Endurance

**Typical EEPROM specs:**
- 100,000 write cycles per cell (AVR)
- 10,000-100,000 cycles (ESP32 flash emulation)

### Write Patterns

**Low-frequency writes (safe):**
- User issues `SAVE` command (manual)
- Firmware update (version bump resets anyway)

**preOBD does NOT:**
- Auto-save on every change
- Write sensor readings to EEPROM
- Use EEPROM for data logging

### Best Practices

1. **Batch changes:** Configure all inputs, then `SAVE` once
2. **Use SD for logging:** EEPROM is for config only
3. **Avoid power cycling:** Complete `SAVE` before power-off

---

## Troubleshooting

### "EEPROM uninitialized"

**Cause:** First boot or EEPROM erased
**Solution:** Normal - configure via serial commands, then `SAVE`

### "EEPROM version mismatch"

**Cause:** Firmware updated with new EEPROM format
**Solution:** Re-configure via serial commands, then `SAVE`

### "EEPROM checksum mismatch"

**Cause:** Corruption (power loss during write, EEPROM wear)
**Solution:**
1. Run `RESET` to clear corrupted data
2. Re-configure via serial commands
3. `SAVE` to write fresh config

### Configuration Lost After Power Cycle

**Possible causes:**
1. Forgot to run `SAVE` command
2. Power lost during `SAVE`
3. EEPROM wear (rare)
4. Firmware upgrade bumped `EEPROM_VERSION` or `SYSTEM_CONFIG_VERSION` — affected block resets to defaults; re-`SAVE` once.
5. **Small-EEPROM platform truncation** — on Teensy 4.0 only the first `MAX_EEPROM_INPUTS` (~10) inputs persist. If `SAVE` printed `⚠ EEPROM holds N inputs; truncating M`, inputs past slot N are not written and disappear on reboot.

**Solution:**
1. Always run `SAVE` after configuring
2. Verify with `DUMP` command before power-off
3. On T4.0, keep input count ≤ `MAX_EEPROM_INPUTS` or accept that extras are RAM-only

### Output enable flags reset to defaults each boot

Symptom: `OUTPUT SERIAL ENABLE` + `SAVE` works for the session, but the output reverts to its default state on reboot.

This was a real bug fixed in `SYSTEM_CONFIG_VERSION` 9 and 10:

- **v9** (PR #159): hardcoded `SYSTEM_CONFIG_ADDRESS` overlapped the input block once ≥12 inputs were configured; saving inputs corrupted SystemConfig's magic number, and load fell back to defaults every boot.
- **v10** (PR #160): on Teensy 4.0 the hardcoded address was past `E2END` entirely, so SystemConfig writes silently failed. Anchoring at the end of EEPROM fixed both.

If you're seeing this on current firmware, run `SAVE` once after upgrade to migrate SystemConfig to its new (per-platform) address.

---

## See Also

- [Registry System](REGISTRY_SYSTEM.md) - Hash-based lookup architecture
- [Adding Sensors](../guides/configuration/ADDING_SENSORS.md) - Adding new sensor types
- [Serial Commands](../reference/SERIAL_COMMANDS.md) - SAVE, LOAD, RESET commands

