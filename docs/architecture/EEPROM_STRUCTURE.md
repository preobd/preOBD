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

openEMS uses EEPROM (Electrically Erasable Programmable Read-Only Memory) to persist configuration across power cycles. The EEPROM structure has evolved through **three versions** to support new features while maintaining data integrity.

**Key Concepts:**
- **Versioned storage:** Each EEPROM write includes version number
- **Checksum validation:** Detects corruption from power loss or wear
- **Auto-migration:** Incompatible versions trigger clean reset
- **Dual configuration:** Input configs + System config stored separately

---

## EEPROM Layout

### Complete Memory Map

```
┌──────────────────────────────────────────────────────────┐
│ Address Range    │ Content                │ Size         │
├──────────────────┼────────────────────────┼──────────────┤
│ 0x0000 - 0x0007  │ Input Header           │ 8 bytes      │
│ 0x0008 - 0x03EF  │ Input Configs (10)     │ 1000 bytes   │
│ 0x03F0 - 0x041F  │ System Config          │ 48 bytes     │
├──────────────────┼────────────────────────┼──────────────┤
│ Total Used                                │ 1056 bytes   │
└──────────────────────────────────────────────────────────┘
```

**Platform EEPROM Sizes:**
- Arduino Uno: 1024 bytes (INPUT-ONLY, no SystemConfig)
- Arduino Mega: 4096 bytes (full support)
- Teensy 4.0/4.1: 1080 bytes (exact fit!)
- ESP32: Emulated (512-4096 bytes configurable)

---

## Version History

### Version Progression

| Version | Firmware | Key Changes | EEPROM Changes |
|---------|----------|-------------|----------------|
| **v1** | v0.3.x | Enum-based system | Stored sensor/app indices |
| **v2** | v0.4.0 | Registry architecture | Switched to hash-based storage |
| **v3** | v0.5.0 | Alarm refactor | Added OUTPUT_ALARM, expanded SystemConfig |

### Version 1 (v0.3.x - Deprecated)

**Characteristics:**
- Sensor and application stored as **enum indices**
- No hash-based lookups
- Fragile: Adding sensors mid-enum broke existing EEPROMs
- No SystemConfig structure

**InputEEPROM Structure:**
```cpp
struct InputEEPROM_v1 {
    uint8_t pin;
    uint8_t sensorIndex;      // Enum value (e.g., SENSOR_MAX6675 = 1)
    uint8_t applicationIndex; // Enum value (e.g., APP_CHT = 0)
    // ... other fields ...
};
```

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

### Version 2 (v0.4.0 - Current for Inputs)

**Introduced:** Registry architecture (hash-based storage)

**Key Changes:**
- Replaced enum indices with **16-bit hash values**
- Hash computed from sensor/application **name strings**
- Portable across firmware versions (hash stays constant)
- Added `unitsHash` for unit conversion

**InputEEPROM Structure:**
```cpp
struct InputEEPROM_v2 {
    uint8_t pin;
    char abbrName[8];
    char displayName[32];
    uint16_t applicationHash;  // djb2_hash("CHT") = 0xD984
    uint16_t sensorHash;       // djb2_hash("MAX6675") = 0x2A23
    uint16_t unitsHash;        // djb2_hash("CELSIUS") = 0x82DD
    float minValue;
    float maxValue;
    uint8_t obd2pid;
    uint8_t obd2length;
    uint8_t flagsByte;
    CalibrationType calibrationType;
    CalibrationOverride customCalibration;
};
```

**Size:** ~100 bytes per input

**Benefits:**
- Adding sensor doesn't break EEPROM (hash remains same)
- Can reorder registry without affecting saved configs
- Foundation for future JSON-based custom sensors

---

### Version 3 (v0.5.0-alpha - Current for SystemConfig)

**Introduced:** Alarm system refactor

**Key Changes:**
- Added `OUTPUT_ALARM` (5th output module)
- Incremented `NUM_OUTPUTS` from 4 to 5
- Expanded SystemConfig from 42 to 48 bytes
- Added alarm warmup/persistence to ApplicationPresets (PROGMEM only)

**SystemConfig Changes:**
```cpp
// v2: NUM_OUTPUTS = 4 (CAN, RealDash, Serial, SD)
// v3: NUM_OUTPUTS = 5 (CAN, RealDash, Serial, SD, Alarm)

uint8_t outputEnabled[NUM_OUTPUTS];    // 4 bytes → 5 bytes
uint16_t outputInterval[NUM_OUTPUTS];  // 8 bytes → 10 bytes
```

**Note:** Input EEPROM version remains v2 (only SystemConfig incremented to v3)

---

## Input Configuration Storage

### Input Header (8 bytes @ 0x0000)

```cpp
struct InputHeader {
    uint16_t magic;       // 0x4945 ("IE" - Input EEPROM)
    uint8_t version;      // 2 (current)
    uint8_t numInputs;    // 0-10 (MAX_INPUTS)
    uint8_t checksum;     // XOR of all InputEEPROM structs
    uint8_t reserved[3];  // Future use
};
```

**Purpose:**
- **Magic:** Detect uninitialized EEPROM (0xFFFF on new chips)
- **Version:** Trigger migration if mismatch
- **numInputs:** How many InputEEPROM structs follow
- **Checksum:** Detect corruption

### Input Slots (1000 bytes @ 0x0008)

**Layout:**
- 10 slots × 100 bytes = 1000 bytes
- Each slot stores one `InputEEPROM` struct
- Empty slots filled with 0xFF

**Example:**
```
Address  | Content
---------|---------------------------
0x0008   | InputEEPROM[0] (Pin A0)
0x006C   | InputEEPROM[1] (Pin 6)
0x00D0   | InputEEPROM[2] (Pin A2)
0x0134   | Empty (0xFF...)
...
0x03EC   | InputEEPROM[9] or Empty
```

### Calibration Override Storage

**Included in InputEEPROM:**
```cpp
union CalibrationOverride {
    ThermistorSteinhart steinhart;
    PressureLinear pressureLinear;
    PressurePolynomial pressurePolynomial;
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
    uint8_t version;             // 3 (current)
    uint8_t checksum;            // XOR checksum

    // Output Modules (12 bytes)
    uint8_t outputEnabled[5];    // CAN, RealDash, Serial, SD, Alarm
    uint16_t outputInterval[5];  // Intervals in ms

    // Display Settings (6 bytes)
    uint8_t displayEnabled;
    uint8_t displayType;         // LCD/OLED/None
    uint8_t lcdI2CAddress;       // Default 0x27
    uint8_t defaultTempUnits;    // Unit index
    uint8_t defaultPressUnits;
    uint8_t defaultElevUnits;

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

**Location:** 0x03F0 (after 10 input slots)

**Default Values:**
Set from `config.h` compile-time defines:
- `ENABLE_CAN`, `ENABLE_SERIAL_OUTPUT`, etc.
- `DEFAULT_TEMPERATURE_UNITS`, `DEFAULT_PRESSURE_UNITS`
- Pin assignments from `config.h`

---

## Checksum Validation

### Input Header Checksum

**Algorithm:** XOR of all InputEEPROM structs
```cpp
uint8_t calculateInputChecksum(InputEEPROM* inputs, uint8_t count) {
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < count; i++) {
        byte* ptr = (byte*)&inputs[i];
        for (size_t j = 0; j < sizeof(InputEEPROM); j++) {
            checksum ^= ptr[j];
        }
    }
    return checksum;
}
```

**Validation:**
```cpp
if (header.checksum != calculateInputChecksum(...)) {
    Serial.println("ERROR: Input config corrupted!");
    resetInputConfig();  // Clear EEPROM
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
1. Read Input Header from EEPROM
2. Check magic number (0x4945)
3. Compare version to `EEPROM_VERSION` constant
4. If mismatch → Reset to defaults

**Code Flow:**
```cpp
InputHeader header;
EEPROM.get(0, header);

if (header.magic != 0x4945) {
    Serial.println("EEPROM uninitialized");
    resetInputConfig();
    return;
}

if (header.version != EEPROM_VERSION) {
    Serial.print("EEPROM version mismatch! Found v");
    Serial.print(header.version);
    Serial.print(", expected v");
    Serial.println(EEPROM_VERSION);
    Serial.println("Resetting to defaults...");
    resetInputConfig();
    return;
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
4. **Testing burden:** Every migration path needs validation

**User Experience:**
```
========================================
  EEPROM VERSION MISMATCH
  Found v1, expected v2
  Configuration reset to defaults
  Please reconfigure via CONFIG mode
========================================
```

User must reconfigure sensors via serial commands or `configure.py`.

---

## EEPROM Wear Management

### EEPROM Endurance

**Typical ratings:**
- ATmega328 (Uno): 100,000 write cycles
- ATmega2560 (Mega): 100,000 write cycles
- Teensy 4.x: Emulated, wear leveling built-in
- ESP32: Flash-based, ~10,000 cycles (with wear leveling)

### Best Practices

**DO:**
- Configure once, save once
- Test in CONFIG mode before saving
- Save only after complete configuration session

**DON'T:**
- Save after every command
- Use SAVE in automated scripts
- Repeatedly save identical config

**Example Good Workflow:**
```
CONFIG
SET 6 CHT MAX6675
SET A0 OIL_TEMP VDO_150C
SET A2 OIL_PRESSURE VDO_5BAR
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100
DISPLAY UNITS TEMP F
SAVE   # ← Single save at end
RUN
```

**Example Bad Workflow:**
```
CONFIG
SET 6 CHT MAX6675
SAVE   # ← DON'T DO THIS
SET A0 OIL_TEMP VDO_150C
SAVE   # ← DON'T DO THIS
SET A2 OIL_PRESSURE VDO_5BAR
SAVE   # ← DON'T DO THIS
```

### Wear Calculation

**Scenario:** Configure 5 sensors, save daily
- 1 SAVE per day × 365 days/year = 365 writes/year
- 100,000 cycles ÷ 365 writes/year = **273 years lifespan**

**Conclusion:** EEPROM wear not a practical concern for normal use.

---

## Troubleshooting

### Problem: "EEPROM version mismatch" on boot

**Symptom:**
```
EEPROM version mismatch! Found v1, expected v2
Resetting to defaults...
```

**Cause:** Firmware upgrade changed EEPROM version

**Solution:** This is expected behavior. Reconfigure via serial:
```
CONFIG
# Re-enter your sensor configuration
SET 6 CHT MAX6675
SET A2 COOLANT_TEMP VDO_120C
SAVE
RUN
```

---

### Problem: Configuration lost after power cycle

**Symptom:** System boots to CONFIG mode every time

**Diagnosis:**
1. Check if SAVE was successful:
```
SAVE
# Should see: "Configuration saved to EEPROM"
```

2. Verify EEPROM after save:
```
DUMP
# Should show all configured inputs
```

**Causes:**
- SAVE command never issued
- Checksum validation failing (corrupted EEPROM)
- EEPROM hardware failure (rare)

**Solution:**
```
CONFIG
# Reconfigure
SAVE
# Verify: "Configuration saved to EEPROM"
VERSION
# Verify: Shows expected EEPROM version
RUN
```

---

### Problem: "Configuration corrupted" message

**Symptom:**
```
ERROR: Input config checksum mismatch!
Configuration may be corrupted.
Resetting to defaults...
```

**Causes:**
- Power loss during SAVE
- EEPROM bit errors (wear or radiation)
- Hardware defect

**Solution:**
1. Reconfigure system
2. Save configuration
3. If problem persists → suspect hardware issue

---

### Problem: Can't save configuration (EEPROM full)

**Symptom:**
```
ERROR: Too many inputs configured
Maximum: 10 inputs
```

**Platform:** Arduino Uno (1024 byte EEPROM)

**Solution:**
- Arduino Uno limited to ~8-9 inputs max
- Upgrade to Mega (4096 bytes) or Teensy for more inputs
- Use compile-time mode (`USE_STATIC_CONFIG`) for max efficiency

---

## Memory Layout by Platform

### Arduino Uno (1024 bytes)

```
┌────────────────────────────────────┐
│ 0x0000 - 0x0007  │ Input Header   │ ✓
│ 0x0008 - 0x02EF  │ 7 Inputs       │ ✓
│ 0x02F0 - 0x03EF  │ Unused         │
│ 0x03F0 - 0x03FF  │ SystemConfig*  │ ✗ (beyond 1KB)
└────────────────────────────────────┘
```
**Limited:** No SystemConfig support (too small)

### Arduino Mega (4096 bytes)

```
┌────────────────────────────────────┐
│ 0x0000 - 0x0007  │ Input Header   │ ✓
│ 0x0008 - 0x03EF  │ 10 Inputs      │ ✓
│ 0x03F0 - 0x041F  │ SystemConfig   │ ✓
│ 0x0420 - 0x0FFF  │ Available      │ 3KB free!
└────────────────────────────────────┘
```
**Full Support:** All features available

### Teensy 4.0/4.1 (1080 bytes emulated)

```
┌────────────────────────────────────┐
│ 0x0000 - 0x0007  │ Input Header   │ ✓
│ 0x0008 - 0x03EF  │ 10 Inputs      │ ✓
│ 0x03F0 - 0x041F  │ SystemConfig   │ ✓
│ 0x0420 - 0x0437  │ Available      │ 24 bytes
└────────────────────────────────────┘
```
**Exact Fit:** Design optimized for Teensy 4.x

---

## See Also

- [Registry System](REGISTRY_SYSTEM.md) - Hash-based sensor/application lookups
- [Config/Run Mode Guide](../guides/configuration/CONFIG_RUN_MODE_GUIDE.md) - Configuration workflow
- [Serial Command Reference](../reference/SERIAL_COMMANDS.md) - SAVE/LOAD commands
- [Input Manager Source](../../src/inputs/input_manager.cpp) - EEPROM persistence implementation
- [System Config Source](../../src/lib/system_config.cpp) - SystemConfig persistence

---

**Last Updated:** 2025-01-28
**Current EEPROM Version:** 2 (Inputs), 3 (SystemConfig)
**Firmware Version:** 0.5.0-alpha (Unreleased)
