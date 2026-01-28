# EEPROM Structure and Versioning

**Technical guide to openEMS EEPROM layout and version management**

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

openEMS uses EEPROM (Electrically Erasable Programmable Read-Only Memory) to persist configuration across power cycles. The EEPROM structure uses **hash-based storage** for portability across firmware versions.

**Key Concepts:**
- **Versioned storage:** Each EEPROM write includes version number
- **Hash-based:** Sensor/application names stored as hashes, not indices
- **Checksum validation:** Detects corruption from power loss or wear
- **Auto-reset:** Incompatible versions trigger clean reset
- **Dual configuration:** Input configs + System config stored separately

---

## EEPROM Layout

### Complete Memory Map

```
┌──────────────────────────────────────────────────────────┐
│ Address Range    │ Content                │ Size         │
├──────────────────┼────────────────────────┼──────────────┤
│ 0x0000 - 0x0007  │ EEPROM Header          │ 8 bytes      │
│ 0x0008 - 0x02C7  │ Input Configs (10)     │ ~700 bytes   │
│ 0x03F0 - 0x041F  │ System Config          │ 48 bytes     │
├──────────────────┼────────────────────────┼──────────────┤
│ Total Used                                │ ~760 bytes   │
└──────────────────────────────────────────────────────────┘
```

**Platform EEPROM Sizes:**
- Arduino Uno: 1024 bytes (full support)
- Arduino Mega: 4096 bytes (full support)
- Teensy 4.0/4.1: 1080 bytes (full support)
- ESP32: Emulated (512-4096 bytes configurable)

---

## Version History

### Version Progression

| Version | Firmware | Key Changes | EEPROM Changes |
|---------|----------|-------------|----------------|
| **v1** | v0.3.x | Enum-based system | Stored sensor/app indices |
| **v2** | v0.4.0+ | Registry architecture | Switched to hash-based storage |

### Version 1 (v0.3.x - Deprecated)

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
    uint16_t version;     // EEPROM_VERSION (currently 2)
    uint8_t numInputs;    // 0-10 (MAX_INPUTS)
    uint8_t reserved;     // Stores XOR checksum
};
```

**Purpose:**
- **Magic:** Detect uninitialized EEPROM (0xFFFFFFFF on new chips)
- **Version:** Trigger reset if mismatch
- **numInputs:** How many InputEEPROM structs follow
- **reserved:** Stores checksum for corruption detection

### InputEEPROM Structure (~70 bytes each)

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

**Size:** ~70 bytes per input

### Input Slots Layout

```
Address  | Content
---------|---------------------------
0x0008   | InputEEPROM[0]
0x0052   | InputEEPROM[1]
0x009C   | InputEEPROM[2]
...      | ...
0x02C7   | InputEEPROM[9] (last slot)
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

### SystemConfig Structure (48 bytes @ 0x03F0)

```cpp
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 ("SC")
    uint8_t version;             // SYSTEM_CONFIG_VERSION (currently 3)
    uint8_t checksum;            // XOR checksum

    // Output Modules (15 bytes)
    uint8_t outputEnabled[5];    // CAN, RealDash, Serial, SD, Alarm
    uint16_t outputInterval[5];  // Intervals in ms

    // Display Settings (7 bytes)
    uint8_t displayEnabled;
    uint8_t displayType;         // LCD/OLED/None
    uint8_t lcdI2CAddress;       // Default 0x27
    uint8_t defaultTempUnits;    // Unit index
    uint8_t defaultPressUnits;
    uint8_t defaultElevUnits;
    uint8_t defaultSpeedUnits;

    // Timing Intervals (8 bytes)
    uint16_t sensorReadInterval;
    uint16_t alarmCheckInterval;
    uint16_t lcdUpdateInterval;
    uint16_t reserved1;

    // Hardware Pins (8 bytes)
    uint8_t modeButtonPin;
    uint8_t buzzerPin;
    uint8_t canCSPin;
    uint8_t canIntPin;
    uint8_t sdCSPin;
    uint8_t testModePin;
    uint16_t reserved2;

    // Physical Constants (4 bytes)
    float seaLevelPressure;      // hPa

    uint8_t reserved[6];         // Future expansion
};
```

**Location:** 0x03F0 (well after input slots)

**Default Values:**
Set from `config.h` compile-time defines:
- `ENABLE_CAN`, `ENABLE_SERIAL_OUTPUT`, etc.
- `DEFAULT_TEMPERATURE_UNITS`, `DEFAULT_PRESSURE_UNITS`
- Pin assignments from `config.h`

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

**openEMS does NOT:**
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

**Solution:**
1. Always run `SAVE` after configuring
2. Verify with `DUMP` command before power-off

### Arduino Uno Memory Constraints

**Uno has 1024 bytes EEPROM:**
- Header: 8 bytes
- 10 inputs × ~70 bytes = 700 bytes
- SystemConfig: 48 bytes @ 0x03F0
- **Total:** ~760 bytes (fits!)

**If issues occur:**
- Reduce MAX_INPUTS in config.h
- Use static builds (no EEPROM needed)

---

## See Also

- [Registry System](REGISTRY_SYSTEM.md) - Hash-based lookup architecture
- [Adding Sensors](../guides/configuration/ADDING_SENSORS.md) - Adding new sensor types
- [Serial Commands](../reference/SERIAL_COMMANDS.md) - SAVE, LOAD, RESET commands

