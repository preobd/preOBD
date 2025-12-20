# Registry Architecture

**Technical guide to openEMS's hash-based registry system**

---

## Table of Contents

1. [Overview](#overview)
2. [Why Registry Architecture?](#why-registry-architecture)
3. [Registry Components](#registry-components)
4. [Hash-Based Lookups](#hash-based-lookups)
5. [PROGMEM Storage](#progmem-storage)
6. [How It Works](#how-it-works)
7. [Adding New Entries](#adding-new-entries)
8. [Hash Collision Handling](#hash-collision-handling)
9. [Implementation Details](#implementation-details)

---

## Overview

The openEMS registry system provides a **flexible, memory-efficient** way to manage sensors, applications, and units. Instead of fixed enum values, the system uses **hash-based string lookups** with all data stored in flash memory (PROGMEM).

**Key Architecture Decision (v0.4.0):**
Replaced enum-based system with registry architecture to enable:
- Runtime extensibility
- Smaller RAM footprint
- EEPROM portability
- Easier addition of new sensors/applications

---

## Why Registry Architecture?

### Problem with Enum-Based System (v0.3.x and earlier)

**Old approach:**
```cpp
enum Sensor {
    SENSOR_NONE = 0,
    SENSOR_MAX6675 = 1,
    SENSOR_VDO_120C = 2,
    // ...
};

// EEPROM stored enum index
struct InputEEPROM {
    uint8_t sensorIndex;  // Stores 1, 2, 3, etc.
};
```

**Problems:**
1. **Rigid:** Adding sensor requires recompiling all code
2. **EEPROM fragility:** Inserting sensor mid-enum breaks existing EEPROMs
3. **Memory waste:** Full enum compiled even if only using 3 sensors
4. **No runtime extensibility:** Cannot add sensors via serial/JSON

### Solution: Registry Architecture (v0.4.0+)

**New approach:**
```cpp
struct SensorInfo {
    const char* name;         // "MAX6675" (PRIMARY KEY)
    uint16_t nameHash;        // 0x2A23 (precomputed djb2_hash)
    // ... sensor data ...
};

// Array of sensors in PROGMEM
const SensorInfo SENSOR_LIBRARY[] PROGMEM = { /* ... */ };

// EEPROM stores hash instead of index
struct InputEEPROM {
    uint16_t sensorHash;      // Stores 0x2A23
};
```

**Benefits:**
1. **Flexible:** New sensors added without breaking EEPROM
2. **Portable:** Hash remains constant regardless of array position
3. **Memory-efficient:** Only used registries loaded
4. **Extensible:** Future support for custom sensors via JSON
5. **Fast:** O(n) linear search acceptable for small registries (~20 items)

---

## Registry Components

openEMS has **three registries**, all stored in PROGMEM (flash):

### 1. Sensor Library
**File:** `src/lib/sensor_library.h`
**Size:** ~20 sensors
**Primary Key:** Sensor name (e.g., "MAX6675", "VDO_120C_LOOKUP")

**Contains:**
- Read function pointer
- Init function pointer (optional)
- Measurement type (TEMPERATURE, PRESSURE, etc.)
- Calibration type and default calibration data
- Physical min/max limits
- Minimum read interval
- Pin type requirement (ANALOG, DIGITAL, I2C)

**Example:**
```cpp
{
    .name = PSTR_MAX6675,                // "MAX6675"
    .label = PSTR_MAX6675_LABEL,         // "K-Type Thermocouple (MAX6675)"
    .readFunction = readMAX6675,
    .initFunction = initThermocoupleCS,
    .measurementType = MEASUREMENT_TEMPERATURE,
    .calibrationType = CALIBRATION_NONE,
    .defaultCalibration = nullptr,
    .minReadInterval = 250,              // 250ms between reads
    .minValue = 0.0,                     // 0°C
    .maxValue = 1024.0,                  // 1024°C
    .nameHash = 0x2A23,                  // djb2_hash("MAX6675")
    .pinTypeRequirement = PIN_DIGITAL    // Requires digital pin for SPI CS
}
```

### 2. Application Presets
**File:** `src/lib/application_presets.h`
**Size:** ~16 applications
**Primary Key:** Application name (e.g., "CHT", "OIL_PRESSURE")

**Contains:**
- Abbreviation and label
- Default sensor recommendation
- Default display units
- Default alarm thresholds (in STANDARD units)
- OBD-II PID mapping
- Warmup time and persistence time for alarms
- OBD conversion function

**Example:**
```cpp
{
    .name = PSTR_CHT,                    // "CHT"
    .abbr = PSTR_CHT_ABBR,               // "CHT"
    .label = PSTR_CHT_LABEL,             // "Cylinder Head Temperature"
    .defaultSensorName = PSTR_MAX6675,   // "MAX6675"
    .defaultUnitsName = PSTR_CELSIUS,    // "CELSIUS"
    .minValue = 50.0,                    // 50°C alarm minimum
    .maxValue = 400.0,                   // 400°C alarm maximum
    .obd2pid = 0x05,                     // PID 0x05 (Engine Coolant Temp)
    .obd2length = 1,
    .warmupTime_ms = 30000,              // 30-second alarm warmup
    .persistTime_ms = 2000,              // 2-second fault persistence
    .nameHash = 0xD984                   // djb2_hash("CHT")
}
```

### 3. Units Registry
**File:** `src/lib/units_registry.h`
**Size:** ~11 units
**Primary Key:** Unit name (e.g., "CELSIUS", "BAR", "PSI")

**Contains:**
- Full name and alias (e.g., "CELSIUS" / "C")
- Display symbol
- Measurement type
- Conversion factor and offset
- Hash values for both name and alias

**Example:**
```cpp
{
    .name = PSTR_CELSIUS,                // "CELSIUS"
    .alias = PSTR_CELSIUS_ALIAS,         // "C"
    .symbol = PSTR_CELSIUS_SYMBOL,       // "°C"
    .measurementType = MEASUREMENT_TEMPERATURE,
    .conversionFactor = 1.0,             // Standard unit (no conversion)
    .conversionOffset = 0.0,
    .nameHash = 0x82DD,                  // djb2_hash("CELSIUS")
    .aliasHash = 0xB5E8                  // djb2_hash("C")
}
```

---

## Hash-Based Lookups

### DJB2 Hash Algorithm

openEMS uses the **DJB2 hash function**, a simple and effective hash for embedded systems:

```cpp
uint16_t djb2_hash(const char* str) {
    uint32_t hash = 5381;  // Magic number
    char c;

    while ((c = *str++)) {
        c = toupper(c);    // Case-insensitive
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    return (uint16_t)(hash & 0xFFFF);  // 16-bit output
}
```

**Properties:**
- **16-bit output:** 0-65535 (2 bytes per hash)
- **Case-insensitive:** "MAX6675" and "max6675" produce same hash
- **Fast:** Simple arithmetic, no table lookups
- **Good distribution:** Low collision probability for small datasets

**Hash Calculation Examples:**
```
djb2_hash("MAX6675")     → 0x2A23
djb2_hash("VDO_5BAR")    → 0xC3F7
djb2_hash("CHT")         → 0xD984
djb2_hash("CELSIUS")     → 0x82DD
djb2_hash("C")           → 0xB5E8
```

### Lookup Process

**1. User command:**
```
SET 6 CHT MAX6675
```

**2. Hash computation:**
```cpp
uint16_t appHash  = djb2_hash("CHT");       // 0xD984
uint16_t sensHash = djb2_hash("MAX6675");   // 0x2A23
```

**3. Registry search:**
```cpp
// Find application by hash
for (uint8_t i = 0; i < NUM_APPLICATIONS; i++) {
    uint16_t hash = pgm_read_word(&APPLICATION_PRESETS[i].nameHash);
    if (hash == appHash) {
        // Found! Index = i
        break;
    }
}
```

**4. EEPROM storage:**
```cpp
// EEPROM stores hash (portable across firmware versions)
eepromInput.applicationHash = 0xD984;
eepromInput.sensorHash = 0x2A23;
```

**Performance:**
- **Worst case:** O(n) linear search through registry
- **Acceptable:** n ≈ 20 sensors, 16 applications, 11 units
- **Fast enough:** Search completes in <1ms on AVR

---

## PROGMEM Storage

All registries are stored in **PROGMEM (flash memory)**, not RAM.

### Why PROGMEM?

**RAM is scarce on embedded systems:**
- Arduino Uno: 2KB RAM
- Arduino Mega: 8KB RAM
- Teensy 4.0: 1MB RAM (but still good practice)

**Registry data is large:**
- Sensor library: ~3KB
- Application presets: ~2KB
- Units registry: ~500 bytes
- **Total: ~5.5KB** (would consume most of Uno's RAM!)

**Solution:** Store in flash (32KB-1MB available)

### PROGMEM Usage

**Declaration:**
```cpp
static const char PSTR_MAX6675[] PROGMEM = "MAX6675";

const SensorInfo SENSOR_LIBRARY[] PROGMEM = {
    {
        .name = PSTR_MAX6675,
        .nameHash = 0x2A23,
        // ...
    }
};
```

**Reading:**
```cpp
// Read from flash (not RAM)
uint16_t hash = pgm_read_word(&SENSOR_LIBRARY[i].nameHash);
const char* name = (const char*)pgm_read_ptr(&SENSOR_LIBRARY[i].name);
```

**Result:**
- **Flash usage:** ~5.5KB (plenty of room)
- **RAM savings:** ~5.5KB freed for runtime data!

---

## How It Works

### Example: SET Command Flow

**Command:**
```
SET A0 OIL_PRESSURE VDO_5BAR
```

**1. Parse command** (`src/inputs/serial_config.cpp`)
```cpp
const char* pinStr = "A0";
const char* appName = "OIL_PRESSURE";
const char* sensName = "VDO_5BAR";
```

**2. Compute hashes** (`src/lib/hash.h`)
```cpp
uint16_t appHash = djb2_hash("OIL_PRESSURE");   // 0x2361
uint16_t sensHash = djb2_hash("VDO_5BAR");      // 0xC3F7
```

**3. Lookup application** (`src/lib/application_presets.h`)
```cpp
uint8_t appIndex = findApplicationByHash(appHash);
// Search PROGMEM for hash 0x2361
// Returns index 6 (OIL_PRESSURE is 7th entry, 0-indexed)
```

**4. Lookup sensor** (`src/lib/sensor_library.h`)
```cpp
uint8_t sensIndex = findSensorByHash(sensHash);
// Search PROGMEM for hash 0xC3F7
// Returns index 11 (VDO_5BAR is 12th entry)
```

**5. Configure input** (`src/inputs/input_manager.cpp`)
```cpp
Input* input = getInputByPin(A0);
input->applicationIndex = appIndex;
input->sensorIndex = sensIndex;

// Load application defaults from PROGMEM
const ApplicationPreset* app = &APPLICATION_PRESETS[appIndex];
input->minValue = pgm_read_float(&app->minValue);  // 0.5 bar
input->maxValue = pgm_read_float(&app->maxValue);  // 10.0 bar
input->unitsIndex = findUnitsByName(...);          // BAR
```

**6. EEPROM persistence** (`src/inputs/input_manager.cpp`)
```cpp
// Save hash to EEPROM (not index!)
eepromInput.applicationHash = appHash;  // 0x2361
eepromInput.sensorHash = sensHash;      // 0xC3F7

EEPROM.put(address, eepromInput);
```

**7. Later: Load from EEPROM**
```cpp
// Load hash from EEPROM
InputEEPROM eepromInput;
EEPROM.get(address, eepromInput);

// Re-lookup by hash (hash remains valid even if registry order changed!)
uint8_t appIndex = findApplicationByHash(eepromInput.applicationHash);
uint8_t sensIndex = findSensorByHash(eepromInput.sensorHash);
```

---

## Adding New Entries

### Adding a New Sensor

**1. Compute hash:**
```bash
python3 -c "h=5381; s='MY_NEW_SENSOR'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
# Output: 0xABCD
```

**2. Add PROGMEM strings** (`src/lib/sensor_library.h`)
```cpp
static const char PSTR_MY_NEW_SENSOR[] PROGMEM = "MY_NEW_SENSOR";
static const char PSTR_MY_NEW_SENSOR_LABEL[] PROGMEM = "My New Sensor Description";
```

**3. Add registry entry:**
```cpp
{
    .name = PSTR_MY_NEW_SENSOR,
    .label = PSTR_MY_NEW_SENSOR_LABEL,
    .readFunction = readMyNewSensor,
    .initFunction = nullptr,  // or initMyNewSensor
    .measurementType = MEASUREMENT_TEMPERATURE,
    .calibrationType = CALIBRATION_NONE,
    .defaultCalibration = nullptr,
    .minReadInterval = 100,
    .minValue = -40.0,
    .maxValue = 150.0,
    .nameHash = 0xABCD,  // From step 1
    .pinTypeRequirement = PIN_ANALOG
}
```

**4. Update count:**
```cpp
#define SENSOR_LIBRARY_SIZE 21  // Was 20, now 21
```

**5. Validate** (optional but recommended)
```bash
python3 tools/validate_registries.py
# Checks for hash collisions, duplicate names, etc.
```

**Result:** New sensor immediately available via `SET <pin> APPLICATION MY_NEW_SENSOR`

---

## Hash Collision Handling

### What is a Hash Collision?

When two different strings produce the same hash:
```
djb2_hash("STRING_A") = 0x1234
djb2_hash("STRING_B") = 0x1234  // COLLISION!
```

### Probability

**16-bit hash space:** 65,536 possible values

**Birthday paradox:**
- With 20 sensors: ~0.3% collision probability
- With 50 items total: ~1.8% collision probability
- **Acceptable for embedded use**

### Detection

**Manual check during development:**
```bash
python3 tools/validate_registries.py
```

Output:
```
Validating sensor_library.h...
  ✓ No hash collisions detected
  ✓ All hashes match djb2 algorithm

Validating application_presets.h...
  ✓ No hash collisions detected

Validating units_registry.h...
  ✓ No hash collisions detected
```

**If collision detected:**
```
ERROR: Sensor hash collision detected
  MY_SENSOR_A and MY_SENSOR_B both hash to 0x1234
```

### Resolution

**Option 1:** Rename one of the strings
```
MY_SENSOR_A  →  MY_SENSOR_TYPE_A
```

**Option 2:** Add underscore/prefix
```
MY_SENSOR_B  →  MY_SENSOR_B_V2
```

**Recalculate hash** and update registry entry.

---

## Implementation Details

### File Locations

| Registry | Header File | Size | Count |
|----------|-------------|------|-------|
| Sensors | `src/lib/sensor_library.h` | ~3KB | ~20 |
| Applications | `src/lib/application_presets.h` | ~2KB | ~16 |
| Units | `src/lib/units_registry.h` | ~500B | ~11 |
| Hash Functions | `src/lib/hash.h` | ~2KB | N/A |

### Key Functions

**Hash computation:**
```cpp
uint16_t djb2_hash(const char* str);       // RAM string
uint16_t djb2_hash_P(const char* str);     // PROGMEM string
```

**Sensor lookups:**
```cpp
uint8_t findSensorByName(const char* name);
uint8_t findSensorByHash(uint16_t hash);
const SensorInfo* getSensorByIndex(uint8_t index);
```

**Application lookups:**
```cpp
uint8_t findApplicationByName(const char* name);
uint8_t findApplicationByHash(uint16_t hash);
const ApplicationPreset* getApplicationByIndex(uint8_t index);
```

**Units lookups:**
```cpp
uint8_t findUnitsByName(const char* name);
uint8_t findUnitsByHash(uint16_t hash);
const UnitsInfo* getUnitsByIndex(uint8_t index);
```

### EEPROM Format (v2+)

**Old format (v1):**
```cpp
struct InputEEPROM_v1 {
    uint8_t sensorIndex;      // Breaks if registry order changes!
    uint8_t applicationIndex;
};
```

**New format (v2+):**
```cpp
struct InputEEPROM_v2 {
    uint16_t sensorHash;      // Portable across firmware versions
    uint16_t applicationHash;
    uint16_t unitsHash;
};
```

**Version migration:**
When EEPROM version mismatch detected, firmware resets EEPROM to defaults.

---

## Benefits Summary

| Feature | Enum System | Registry System |
|---------|-------------|-----------------|
| **RAM Usage** | High (~5.5KB for full registry) | Low (~200 bytes runtime) |
| **Flash Usage** | Same | Same |
| **EEPROM Portability** | Fragile (breaks on reorder) | Robust (hash-based) |
| **Extensibility** | Requires recompile | Runtime-friendly |
| **Lookup Speed** | O(1) array index | O(n) linear search |
| **Development** | Rigid | Flexible |
| **Collision Risk** | None | ~1.8% (50 items) |

---

## See Also

- [EEPROM Structure](EEPROM_STRUCTURE.md) - EEPROM layout and versioning
- [Adding Sensors Guide](../guides/configuration/ADDING_SENSORS.md) - Step-by-step sensor addition
- [Sensor Library Header](../../src/lib/sensor_library.h) - Sensor registry source code
- [Application Presets Header](../../src/lib/application_presets.h) - Application registry source code
- [Units Registry Header](../../src/lib/units_registry.h) - Units registry source code
- [Hash Functions Header](../../src/lib/hash.h) - DJB2 hash implementation

---

**Last Updated:** 2025-01-28
**Firmware Version:** 0.5.0-alpha (Unreleased)
**EEPROM Version:** 2+ (Registry-based)
