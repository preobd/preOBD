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

**Key Architecture Decision:**
Replaced enum-based system with registry architecture to enable:
- EEPROM portability across firmware versions
- Easier addition of new sensors/applications
- Smaller RAM footprint (all data in PROGMEM)

---

## Why Registry Architecture?

### Problem with Enum-Based System

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
3. **No runtime extensibility:** Cannot add sensors via serial/JSON

### Solution: Registry Architecture

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
3. **Extensible:** Future support for custom sensors via JSON
4. **Fast:** O(n) linear search acceptable for small registries (~20 items)

---

## Registry Components

openEMS has **three registries**, all stored in PROGMEM (flash):

### 1. Sensor Library
**File:** `src/lib/sensor_library.h` (orchestrator)
**Modular Files:** `src/lib/sensor_library/sensors/*.h`
**Size:** ~28 sensors
**Primary Key:** Sensor name (e.g., "MAX6675", "VDO_120C_TABLE")

**Structure:**
```
src/lib/sensor_library/
├── sensor_types.h        # SensorInfo struct, forward declarations
├── sensor_categories.h   # Category enum and helpers
├── sensor_helpers.h      # Lookup functions
└── sensors/              # Sensor definitions by type
    ├── none.h            # Placeholder
    ├── thermocouples.h   # MAX6675, MAX31855
    ├── thermistors.h     # VDO, generic NTC
    ├── pressure.h        # Linear, polynomial, table
    ├── voltage.h         # Voltage divider
    ├── frequency.h       # RPM, speed
    ├── environmental.h   # BME280
    └── digital.h         # Float switch
```

**Contains:**
- Read function pointer
- Init function pointer (optional)
- Measurement type (TEMPERATURE, PRESSURE, etc.)
- Calibration type and default calibration data
- Physical sensor limits (min/max values)
- Precomputed name hash

### 2. Application Presets
**File:** `src/lib/application_presets.h`
**Size:** ~16 applications
**Primary Key:** Application name (e.g., "CHT", "OIL_PRESSURE")

**Contains:**
- Default sensor hash
- Default display units hash
- OBD-II PID mapping
- Alarm thresholds (min/max)
- Warmup and persistence timing
- Precomputed name hash

### 3. Units Registry
**File:** `src/lib/units_registry.h`
**Size:** ~11 units
**Primary Key:** Unit name (e.g., "CELSIUS", "BAR")

**Contains:**
- Display symbol
- Conversion factor and offset
- Measurement type
- Name hash and alias hash

---

## Hash-Based Lookups

### The DJB2 Algorithm

openEMS uses the **djb2 hash algorithm** (Daniel J. Bernstein):

```cpp
uint16_t djb2_hash(const char* str) {
    uint16_t hash = 5381;
    char c;
    while ((c = *str++)) {
        c = toupper(c);  // Case-insensitive
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}
```

**Properties:**
- 16-bit output (0x0000 - 0xFFFF)
- Case-insensitive (all converted to uppercase)
- Fast computation
- Good distribution for short strings

### Why Hashes Instead of Indices?

**Index-based (fragile):**
```cpp
// EEPROM stores: sensorIndex = 5
// If we insert a new sensor at position 3...
// Index 5 now points to WRONG sensor!
```

**Hash-based (robust):**
```cpp
// EEPROM stores: sensorHash = 0x2A23 (hash of "MAX6675")
// We can add, remove, reorder sensors freely
// Hash 0x2A23 still finds "MAX6675"
```

---

## PROGMEM Storage

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
SET A0 OIL_PRESSURE VDO_5BAR_CURVE
```

**1. Parse command** (`src/inputs/serial_config.cpp`)
```cpp
const char* pinStr = "A0";
const char* appName = "OIL_PRESSURE";
const char* sensName = "VDO_5BAR_CURVE";
```

**2. Compute hashes** (`src/lib/hash.h`)
```cpp
uint16_t appHash = djb2_hash("OIL_PRESSURE");   // 0x2361
uint16_t sensHash = djb2_hash("VDO_5BAR_CURVE");      // 0xC3F7
```

**3. Lookup application** (`src/lib/application_presets.h`)
```cpp
uint8_t appIndex = getApplicationIndexByHash(appHash);
// Search PROGMEM for hash 0x2361
// Returns index 6 (OIL_PRESSURE is 7th entry, 0-indexed)
```

**4. Lookup sensor** (`src/lib/sensor_library.h`)
```cpp
uint8_t sensIndex = getSensorIndexByHash(sensHash);
// Search PROGMEM for hash 0xC3F7
// Returns index 11 (VDO_5BAR_CURVE is 12th entry)
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
input->unitsIndex = getUnitsIndexByHash(...);      // BAR
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
uint8_t appIndex = getApplicationIndexByHash(eepromInput.applicationHash);
uint8_t sensIndex = getSensorIndexByHash(eepromInput.sensorHash);
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
    .measurementType = MEASURE_TEMPERATURE,
    .calibrationType = CAL_NONE,
    .defaultCalibration = nullptr,
    .minReadInterval = 100,
    .minValue = -40.0,
    .maxValue = 150.0,
    .nameHash = 0xABCD,  // From step 1
    .pinTypeRequirement = PIN_ANALOG
}
```

**4. Validate** (recommended)
```bash
python3 tools/validate_registries.py
# Checks for hash collisions, duplicate names, etc.
```

**Result:** New sensor immediately available via `SET <pin> <app> MY_NEW_SENSOR`

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
| Sensors | `src/lib/sensor_library.h` + `sensor_library/sensors/*.h` | ~3KB | ~28 |
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
uint8_t getSensorIndexByName(const char* name);
uint8_t getSensorIndexByHash(uint16_t hash);
const SensorInfo* getSensorByIndex(uint8_t index);
```

**Application lookups:**
```cpp
uint8_t getApplicationIndexByName(const char* name);
uint8_t getApplicationIndexByHash(uint16_t hash);
const ApplicationPreset* getApplicationByIndex(uint8_t index);
```

**Units lookups:**
```cpp
uint8_t getUnitsIndexByName(const char* name);
uint8_t getUnitsIndexByHash(uint16_t hash);
const UnitsInfo* getUnitsByIndex(uint8_t index);
```

### EEPROM Format

**Old format (v1 - deprecated):**
```cpp
struct InputEEPROM_v1 {
    uint8_t sensorIndex;      // Breaks if registry order changes!
    uint8_t applicationIndex;
};
```

**Current format (v2+):**
```cpp
struct InputEEPROM {
    uint16_t sensorHash;      // Portable across firmware versions
    uint16_t applicationHash;
    uint16_t unitsHash;
    // ... other fields ...
};
```

**Version migration:**
When EEPROM version mismatch detected, firmware resets EEPROM to defaults.

### Benefits Summary

| Feature | Enum System | Registry System |
|---------|-------------|-----------------|
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

