# JSON Configuration Guide

**Version:** 0.5.0-alpha (Unreleased)
**Last Updated:** 2025-12-19

This guide covers JSON configuration in openEMS: creating configurations with `configure.py`, importing/exporting JSON, SD card backup/restore, custom calibrations, and maintaining backward compatibility when evolving firmware.

## Table of Contents
- [Overview](#overview)
- [JSON Schema Format](#json-schema-format)
- [Creating Configurations with configure.py](#creating-configurations-with-configurepy)
- [SD Card Backup and Restore](#sd-card-backup-and-restore)
- [Custom Calibrations in JSON](#custom-calibrations-in-json)
- [Schema Versioning](#schema-versioning)
- [Adding Fields (Developer Guide)](#adding-fields-developer-guide)
- [Removing Fields (Developer Guide)](#removing-fields-developer-guide)
- [Migration Checklist](#migration-checklist)
- [Examples](#examples)

---

## Overview

The JSON configuration system in openEMS serves multiple purposes:

1. **Static Configuration** - Generate compile-time configs with `configure.py` (saves EEPROM wear, enables optimizations)
2. **Runtime Backup/Restore** - Save and load complete system state to/from SD card
3. **Configuration Transfer** - Share configs between devices or archive proven setups
4. **Development** - Maintain backward compatibility as firmware evolves

### Key Principles

1. **Always maintain backward compatibility** - Old JSON files should work with new firmware
2. **Use default values** - New fields should have sensible defaults
3. **Version your schema** - Track changes with schema version numbers
4. **Test round-trips** - Verify DUMP → SAVE → LOAD → DUMP produces identical output

### Use Cases

| Use Case | Tool | Workflow |
|----------|------|----------|
| **Initial Setup** | `configure.py` | Generate static config from scratch |
| **Backup Configuration** | Serial commands | `CONFIG SAVE backup.json` to SD card |
| **Restore Configuration** | Serial commands | `CONFIG LOAD backup.json` from SD card |
| **Share Configurations** | Both | Export JSON, edit, import on another device |
| **Custom Calibrations** | `configure.py` | Interactive prompts for calibration parameters |

---

## JSON Schema Format

### Schema v1 Structure

```json
{
  "schemaVersion": 1,
  "mode": "static",
  "metadata": {
    "toolVersion": "1.0.0",
    "platform": "megaatmega2560",
    "timestamp": 1704067200
  },
  "inputs": [
    {
      "idx": 0,
      "pin": "6",
      "application": "CHT",
      "sensor": "MAX6675",
      "calibration": null
    },
    {
      "idx": 1,
      "pin": "A0",
      "application": "OIL_TEMP",
      "sensor": "VDO_150C_STEINHART",
      "calibration": {
        "type": "THERMISTOR_STEINHART",
        "parameters": {
          "a": 0.00112764,
          "b": 0.000234282,
          "c": 8.52635e-08,
          "r25": 796.4,
          "seriesR": 1000.0
        }
      }
    }
  ]
}
```

### Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `schemaVersion` | number | Yes | JSON schema version (currently 1) |
| `mode` | string | Yes | Configuration mode: "static" or "runtime" |
| `metadata.toolVersion` | string | No | Version of configure.py that generated this |
| `metadata.platform` | string | Yes | Target platform (e.g., "megaatmega2560") |
| `metadata.timestamp` | number | No | Unix timestamp of generation |
| `inputs` | array | Yes | Array of input configurations (0-10 elements) |
| `inputs[].idx` | number | Yes | Input index (0-9) |
| `inputs[].pin` | string | Yes | Pin assignment (e.g., "A0", "6", "I2C") |
| `inputs[].application` | string | Yes | Application name (e.g., "CHT", "OIL_TEMP") |
| `inputs[].sensor` | string | Yes | Sensor name (e.g., "MAX6675", "VDO_150C") |
| `inputs[].calibration` | object/null | No | Custom calibration (null = use sensor default) |

---

## Creating Configurations with configure.py

The `configure.py` tool generates static configurations and optionally saves them as JSON files. This is the recommended way to create new configurations.

### Basic Workflow

```bash
# Navigate to project root
cd /path/to/openEMS

# Run configuration tool
python3 tools/configure.py

# Follow interactive prompts:
# 1. Select application (CHT, OIL_TEMP, etc.)
# 2. Select sensor (MAX6675, VDO_150C, etc.)
# 3. Enter pin assignment (6, A0, I2C, etc.)
# 4. Configure custom calibration (if needed)
# 5. Repeat for additional inputs
```

### JSON Output Options

`configure.py` can save configurations as JSON files:

```bash
# Generate config and save JSON to tools/saved-configs/
python3 tools/configure.py

# At the end, you'll be prompted:
Save this configuration to JSON? (y/n): y
Enter filename (e.g., my_config.json): dual_cht_config.json

# File saved to: tools/saved-configs/dual_cht_config.json
```

### Loading Saved Configurations

```bash
# Load a previously saved JSON configuration
python3 tools/configure.py --load tools/saved-configs/dual_cht_config.json

# Review and edit the configuration
# Save changes back to JSON or generate new config.h
```

### For detailed configure.py documentation, see:
- **[tools/README.md](../../../tools/README.md)** - Complete configure.py usage guide

---

## SD Card Backup and Restore

openEMS supports runtime backup and restore of the complete system configuration via SD card. This includes:
- All input configurations (pin, application, sensor, calibration)
- System settings (output modules, display, timing, pins)
- Alarm thresholds and warmup times

### Prerequisites

1. **SD Card Module** - Wired to SPI bus (CS pin configurable via `systemConfig.sdCSPin`)
2. **Firmware Compiled with SD Support** - SD output module enabled
3. **Formatted SD Card** - FAT16/FAT32 format

### Backup Configuration to SD Card

```bash
# Export current configuration to SD card
> CONFIG SAVE my_backup.json

# Response:
Configuration saved to: my_backup.json
```

**What gets saved:**
- Schema version (for migration support)
- Firmware version (for reference)
- Platform information
- All input configurations
- System configuration (outputs, display, timing, pins)
- Alarm settings (per-input warmup, persistence)

### Restore Configuration from SD Card

```bash
# Load configuration from SD card
> CONFIG LOAD my_backup.json

# Response:
Configuration loaded from: my_backup.json
Inputs restored: 5
System config restored: OK

# IMPORTANT: Changes are in RAM only!
# Persist to EEPROM:
> SAVE
```

**What gets restored:**
- Input configurations (pins, apps, sensors, calibrations)
- System settings (outputs, display, timing)
- Alarm thresholds and warmup times

**Note:** After `CONFIG LOAD`, the configuration is active but **not persisted to EEPROM** until you run `SAVE`.

### Workflow: Complete Backup/Restore

```bash
# === Backup Workflow ===
# 1. Configure system via serial commands
> SET 6 CHT MAX6675
> SET A0 OIL_TEMP VDO_150C_STEINHART
> OUTPUT CAN ENABLE
> OUTPUT CAN INTERVAL 100

# 2. Persist to EEPROM
> SAVE

# 3. Backup to SD card
> CONFIG SAVE production_config.json
Configuration saved to: production_config.json

# === Restore Workflow (on same or different device) ===
# 1. Load from SD card
> CONFIG LOAD production_config.json

# 2. Review loaded configuration
> SHOW INPUTS
> SHOW OUTPUTS

# 3. Persist to EEPROM
> SAVE

# 4. Verify with DUMP
> DUMP JSON
[Should match production_config.json contents]
```

### Use Cases for SD Card Backup/Restore

1. **Device Replacement** - Quickly configure replacement hardware with proven settings
2. **Multiple Profiles** - Swap between different configurations (e.g., "dyno_tune.json", "street_config.json")
3. **Configuration Archival** - Keep timestamped backups of known-good configs
4. **Rollback** - Restore previous configuration after experimental changes
5. **Fleet Deployment** - Distribute identical configs to multiple vehicles

---

## Custom Calibrations in JSON

openEMS supports **four types** of custom calibrations that can be specified in JSON configs. These override sensor defaults with user-specific parameters.

### Calibration Type 1: THERMISTOR_STEINHART

**For:** Generic thermistors not in the VDO library

**Parameters:**
- `a`, `b`, `c` - Steinhart-Hart coefficients
- `r25` - Resistance at 25°C (Ω)
- `seriesR` - Series resistor value (Ω)

**JSON Example:**
```json
{
  "idx": 0,
  "pin": "A0",
  "application": "CUSTOM_TEMP",
  "sensor": "THERMISTOR_STEINHART",
  "calibration": {
    "type": "THERMISTOR_STEINHART",
    "parameters": {
      "a": 0.00112764,
      "b": 0.000234282,
      "c": 8.52635e-08,
      "r25": 796.4,
      "seriesR": 1000.0
    }
  }
}
```

**configure.py prompts:**
```bash
Sensor: THERMISTOR_STEINHART
Use default calibration? (y/n): n

Enter custom Steinhart-Hart calibration:
  Coefficient A: 0.00112764
  Coefficient B: 0.000234282
  Coefficient C: 8.52635e-08
  R25 (Ω): 796.4
  Series resistor (Ω): 1000.0
```

### Calibration Type 2: PRESSURE_LINEAR

**For:** Linear pressure sensors (e.g., 0.5V = 0 bar, 4.5V = 5 bar)

**Parameters:**
- `voltageMin` - Voltage at minimum pressure (V)
- `voltageMax` - Voltage at maximum pressure (V)
- `pressureMin` - Minimum pressure (bar)
- `pressureMax` - Maximum pressure (bar)

**JSON Example:**
```json
{
  "idx": 1,
  "pin": "A1",
  "application": "OIL_PRESSURE",
  "sensor": "GENERIC_PRESSURE",
  "calibration": {
    "type": "PRESSURE_LINEAR",
    "parameters": {
      "voltageMin": 0.5,
      "voltageMax": 4.5,
      "pressureMin": 0.0,
      "pressureMax": 10.0
    }
  }
}
```

**configure.py prompts:**
```bash
Sensor: GENERIC_PRESSURE
Use default calibration? (y/n): n

Enter linear pressure calibration:
  Voltage at min pressure (V): 0.5
  Voltage at max pressure (V): 4.5
  Minimum pressure (bar): 0.0
  Maximum pressure (bar): 10.0
```

### Calibration Type 3: PRESSURE_POLY

**For:** Non-linear pressure sensors requiring polynomial correction

**Parameters:**
- `c0`, `c1`, `c2`, `c3` - Polynomial coefficients (pressure = c0 + c1*V + c2*V² + c3*V³)

**JSON Example:**
```json
{
  "idx": 2,
  "pin": "A2",
  "application": "BOOST",
  "sensor": "GENERIC_BOOST",
  "calibration": {
    "type": "PRESSURE_POLY",
    "parameters": {
      "c0": -1.2,
      "c1": 2.5,
      "c2": -0.15,
      "c3": 0.005
    }
  }
}
```

**configure.py prompts:**
```bash
Sensor: GENERIC_BOOST
Use default calibration? (y/n): n

Enter polynomial pressure calibration (pressure = c0 + c1*V + c2*V² + c3*V³):
  Coefficient c0: -1.2
  Coefficient c1: 2.5
  Coefficient c2: -0.15
  Coefficient c3: 0.005
```

### Calibration Type 4: RPM

**For:** Custom RPM sensors with non-standard pulses per revolution

**Parameters:**
- `pulsesPerRev` - Number of pulses per revolution (e.g., 3 for W-phase alternator)

**JSON Example:**
```json
{
  "idx": 3,
  "pin": "2",
  "application": "ENGINE_RPM",
  "sensor": "W_PHASE_RPM",
  "calibration": {
    "type": "RPM",
    "parameters": {
      "pulsesPerRev": 3.0
    }
  }
}
```

**configure.py prompts:**
```bash
Sensor: W_PHASE_RPM
Use default calibration? (y/n): n

Enter RPM calibration:
  Pulses per revolution: 3.0
```

### Using Custom Calibrations

**In configure.py:**
```bash
# During sensor configuration, you'll be prompted:
Use default calibration for VDO_150C_STEINHART? (y/n): n

# Then answer prompts for the specific calibration type
# The JSON file will include the calibration parameters
```

**Manual JSON editing:**
```bash
# 1. Generate base config
python3 tools/configure.py

# 2. Save to JSON
Save this configuration to JSON? y
Filename: my_config.json

# 3. Edit JSON file to add/modify calibration
nano tools/saved-configs/my_config.json

# 4. Reload and regenerate
python3 tools/configure.py --load tools/saved-configs/my_config.json
```

---

## Schema Versioning

### Current Schema Version: 1

The JSON schema version is independent of the firmware version. It only increments when the JSON structure changes in a backward-incompatible way.

**Schema Version Location:**
```json
{
  "schemaVersion": 1,
  "firmware": {
    "version": "0.4.1-alpha",
    ...
  }
}
```

**When to Increment Schema Version:**
- ❌ **DO NOT** increment for adding optional fields (backward compatible)
- ❌ **DO NOT** increment for adding new registry entries (sensors/apps/units)
- ✅ **DO** increment when removing required fields
- ✅ **DO** increment when changing field types (string → number)
- ✅ **DO** increment when renaming fields

---

## Adding Fields (Developer Guide)

### To SystemConfig Struct

**Example:** Adding `uint16_t displayBrightness;`

#### 1. Update the Struct
**File:** `src/lib/system_config.h`
```cpp
struct SystemConfig {
    // ... existing fields ...

    uint16_t displayBrightness;  // NEW: LCD brightness 0-255

    // Update reserved bytes accordingly
    uint8_t reserved[4];  // Reduced from 6 to 4
};
```

#### 2. Set Default Value
**File:** `src/lib/system_config.cpp`
```cpp
void resetSystemConfig() {
    // ... existing defaults ...

    systemConfig.displayBrightness = 200;  // Default to 200/255
}
```

#### 3. Update EEPROM Version
**File:** `src/lib/system_config.h`
```cpp
#define SYSTEM_CONFIG_VERSION 3  // Increment from 2 to 3
```

#### 4. Export to JSON
**File:** `src/lib/json_config.cpp` → `exportSystemConfigToJSON()`
```cpp
JsonObject display = systemObj["display"].to<JsonObject>();
display["enabled"] = (bool)systemConfig.displayEnabled;
display["type"] = displayTypeStr;
display["brightness"] = systemConfig.displayBrightness;  // NEW
```

#### 5. Import from JSON (with default fallback)
**File:** `src/lib/json_config.cpp` → `importSystemConfigFromJSON()`
```cpp
if (systemObj["display"].isNull() == false) {
    JsonObject display = systemObj["display"];
    systemConfig.displayEnabled = display["enabled"];

    // NEW: Use default if not present (backward compatibility)
    systemConfig.displayBrightness = display["brightness"] | 200;
}
```

#### 6. Add Serial Command (Optional)
**File:** `src/inputs/serial_config.cpp`
```cpp
if (streq(cmd, "DISPLAY") && secondToken) {
    if (streq(secondToken, "BRIGHTNESS")) {
        // Parse and set brightness
    }
}
```

#### 7. Test
```bash
> DUMP JSON              # Should show "brightness": 200
> CONFIG SAVE test.json
> RESET
> CONFIG LOAD test.json  # Should restore brightness
> DUMP JSON              # Should match original
```

---

### To Input Struct

**Example:** Adding `uint8_t priority;` for alarm priority

#### 1. Update Input Struct
**File:** `src/inputs/input.h`
```cpp
struct Input {
    // ... existing fields ...

    uint8_t priority;  // NEW: Alarm priority (0=lowest, 255=highest)

    // ... rest of fields ...
};
```

#### 2. Update InputEEPROM Struct
**File:** `src/inputs/input_manager.cpp`
```cpp
struct InputEEPROM {
    // ... existing fields ...

    uint8_t priority;  // NEW: Match Input struct

    uint8_t reserved[N];  // Adjust size
};
```

#### 3. Update EEPROM Save/Load
**File:** `src/inputs/input_manager.cpp`

In `saveInputConfig()`:
```cpp
eeprom.priority = inputs[i].priority;  // NEW
```

In `loadInputConfig()`:
```cpp
inputs[i].priority = eeprom.priority;  // NEW
```

#### 4. Update EEPROM Version
**File:** `src/inputs/input_manager.cpp`
```cpp
#define EEPROM_VERSION 3  // Increment
```

#### 5. Export to JSON
**File:** `src/lib/json_config.cpp` → `exportInputToJSON()`
```cpp
inputObj["priority"] = input->priority;  // NEW
```

#### 6. Import from JSON (with default)
**File:** `src/lib/json_config.cpp` → `importInputFromJSON()`
```cpp
// NEW: Default to medium priority if not specified
input->priority = inputObj["priority"] | 128;
```

---

### To CalibrationOverride Union

**Example:** Adding custom lookup table

#### 1. Add to Union
**File:** `src/inputs/input.h`
```cpp
union CalibrationOverride {
    // ... existing calibrations ...

    // NEW: Custom lookup table (16 bytes)
    struct {
        float table[4];  // 4 calibration points
    } customLookup;

    byte raw[16];
};
```

#### 2. Add Calibration Type Enum
**File:** `src/lib/sensor_types.h`
```cpp
enum CalibrationType {
    // ... existing types ...
    CAL_CUSTOM_LOOKUP,  // NEW
};
```

#### 3. Update getCalibrationType()
**File:** `src/lib/json_config.cpp`
```cpp
static const char* getCalibrationType(const Input* input) {
    switch (input->calibrationType) {
        // ... existing cases ...
        case CAL_CUSTOM_LOOKUP: return "CUSTOM_LOOKUP";  // NEW
        default: return "UNKNOWN";
    }
}
```

#### 4. Update exportCalibration()
**File:** `src/lib/json_config.cpp`
```cpp
switch (input->calibrationType) {
    // ... existing cases ...

    case CAL_CUSTOM_LOOKUP:  // NEW
        for (int i = 0; i < 4; i++) {
            char key[8];
            snprintf(key, sizeof(key), "point%d", i);
            params[key] = input->customCalibration.customLookup.table[i];
        }
        break;
}
```

#### 5. Update importCalibration()
**File:** `src/lib/json_config.cpp`
```cpp
// Parse calibration type from JSON and apply
// (Currently stubbed - implement as needed)
```

---

## Removing Fields (Developer Guide)

### Breaking Change - Requires Migration

When removing fields, you **must** increment the schema version and provide a migration path.

**Example:** Removing deprecated `systemConfig.reserved1`

#### 1. Mark as Deprecated (Release N)
```cpp
// @deprecated Will be removed in v0.5.0
uint16_t reserved1;
```

Document in changelog:
```markdown
## v0.4.2 - Deprecation Notice
- systemConfig.reserved1 is deprecated and will be removed in v0.5.0
```

#### 2. Remove Field (Release N+1)
**File:** `src/lib/system_config.h`
```cpp
struct SystemConfig {
    // ... fields ...
    // REMOVED: reserved1 (was deprecated in v0.4.2)

    uint8_t reserved[10];  // Increased
};
```

#### 3. Increment Schema Version
```cpp
// In json_config.cpp
firmware["schemaVersion"] = 2;  // Was 1
```

#### 4. Handle Old Schema on Import
**File:** `src/lib/json_config.cpp` → `loadConfigFromJSON()`
```cpp
// Check schema version
uint8_t schemaVer = doc["schemaVersion"] | 1;  // Default to 1 if missing

if (schemaVer == 1) {
    // Old schema - skip reserved1 field if present
    // All other fields still work
}
```

#### 5. Document Migration
Update `docs/CHANGELOG.md`:
```markdown
## v0.5.0 - Breaking Changes
- **JSON Schema v2**: Removed systemConfig.reserved1
- Old JSON configs (schema v1) still supported
- No user action required
```

---

## Migration Checklist

Use this checklist when modifying configuration structures:

### Adding Field to SystemConfig
- [ ] Add field to `SystemConfig` struct in `system_config.h`
- [ ] Set default value in `resetSystemConfig()` in `system_config.cpp`
- [ ] Increment `SYSTEM_CONFIG_VERSION` in `system_config.h`
- [ ] Add export line in `exportSystemConfigToJSON()` in `json_config.cpp`
- [ ] Add import line with default fallback in `importSystemConfigFromJSON()`
- [ ] Add serial command if user-configurable (optional)
- [ ] Update `docs/CHANGELOG.md`
- [ ] Test: `DUMP JSON` shows new field with correct value
- [ ] Test: `CONFIG LOAD` from old JSON uses default value
- [ ] Test: Round-trip DUMP → SAVE → LOAD → DUMP matches

### Adding Field to Input
- [ ] Add field to `Input` struct in `input.h`
- [ ] Add field to `InputEEPROM` struct in `input_manager.cpp`
- [ ] Update `saveInputConfig()` to serialize new field
- [ ] Update `loadInputConfig()` to deserialize new field
- [ ] Increment `EEPROM_VERSION` in `input_manager.cpp`
- [ ] Add export line in `exportInputToJSON()` in `json_config.cpp`
- [ ] Add import line with default in `importInputFromJSON()`
- [ ] Update `docs/CHANGELOG.md`
- [ ] Test: `DUMP JSON` includes new field for all inputs
- [ ] Test: `CONFIG LOAD` from old JSON uses default
- [ ] Test: Round-trip preserves new field value

### Adding Calibration Type
- [ ] Add to `CalibrationType` enum in `sensor_types.h`
- [ ] Add struct to `CalibrationOverride` union in `input.h`
- [ ] Add case to `getCalibrationType()` in `json_config.cpp`
- [ ] Add case to `exportCalibration()` in `json_config.cpp`
- [ ] Add parsing to `importCalibration()` in `json_config.cpp`
- [ ] Add read function and wire up in sensor library
- [ ] Update `docs/CHANGELOG.md`
- [ ] Test: Export/import custom calibration parameters

### Adding Registry Entry (Sensor/Application/Unit)
- [ ] Add sensor to appropriate file in `sensor_library/sensors/*.h` using X-macro pattern
- [ ] Add PROGMEM string literals in the same file
- [ ] Compute and add DJB2 hash
- [ ] Add read function if new sensor
- [ ] Add calibration data to `sensor_calibration_data/<manufacturer>/` if needed
- [ ] Update `docs/CHANGELOG.md`
- [ ] Test: JSON export uses new registry name
- [ ] Test: JSON import finds new registry entry
- [ ] **No JSON code changes needed!** (Registry architecture handles it)

### Removing Field (Breaking Change)
- [ ] Mark as `@deprecated` in previous release
- [ ] Document deprecation in `docs/CHANGELOG.md`
- [ ] Remove field in next major release
- [ ] Increment schema version in `json_config.cpp`
- [ ] Add migration code in `loadConfigFromJSON()` if needed
- [ ] Update all export/import functions
- [ ] Document breaking change in `docs/CHANGELOG.md`
- [ ] Test: Old JSON configs still load (skip removed field)
- [ ] Test: New JSON configs omit removed field

---

## Examples

### Example 1: Adding Display Brightness

**Scenario:** Add user-configurable LCD brightness

**Changes:**
```cpp
// system_config.h
struct SystemConfig {
    uint8_t displayBrightness;  // 0-255
    uint8_t reserved[5];  // Was 6
};
#define SYSTEM_CONFIG_VERSION 3  // Was 2

// system_config.cpp
void resetSystemConfig() {
    systemConfig.displayBrightness = 200;  // Default
}

// json_config.cpp - Export
display["brightness"] = systemConfig.displayBrightness;

// json_config.cpp - Import (with default)
systemConfig.displayBrightness = display["brightness"] | 200;
```

**Result:**
- Old JSON: Missing "brightness" field → Uses default 200
- New JSON: Includes "brightness" field → Uses saved value

---

### Example 2: Adding Alarm Priority

**Scenario:** Add priority levels to inputs for alarm handling

**Changes:**
```cpp
// input.h
struct Input {
    uint8_t priority;  // 0=low, 255=critical
};

// input_manager.cpp
struct InputEEPROM {
    uint8_t priority;
};
#define EEPROM_VERSION 4  // Was 3

// Save/Load updated accordingly

// json_config.cpp - Export
inputObj["priority"] = input->priority;

// json_config.cpp - Import (with default)
input->priority = inputObj["priority"] | 128;  // Default to medium
```

**Result:**
- Old JSON: Missing "priority" → Defaults to 128 (medium)
- New JSON: Includes "priority" → Uses specified value

---

### Example 3: Removing Deprecated Field

**Scenario:** Remove `systemConfig.reserved1` after deprecation period

**v0.4.2 (Deprecation):**
```cpp
// system_config.h
struct SystemConfig {
    uint16_t reserved1;  // @deprecated - Remove in v0.5.0
};
```

**v0.5.0 (Removal):**
```cpp
// system_config.h
struct SystemConfig {
    // REMOVED: reserved1
    uint8_t reserved[8];  // Increased from 6
};

// json_config.cpp
firmware["schemaVersion"] = 2;  // Increment from 1

// loadConfigFromJSON() - Handle old schema
uint8_t schemaVer = doc["schemaVersion"] | 1;
if (schemaVer == 1) {
    // Old schema - just ignore reserved1 if present
}
```

**Result:**
- Schema v1 JSON (old): Loads successfully, ignores reserved1
- Schema v2 JSON (new): Cleaner format without reserved1

---

## Testing Strategy

### Manual Round-Trip Test
```bash
# 1. Export current config
> DUMP JSON
[Copy output to file: config_v1.json]

# 2. Save to SD card
> CONFIG SAVE backup.json

# 3. Make changes (add sensors, modify settings)
> SET A2 OIL_TEMP VDO_150C_STEINHART

# 4. Export again
> DUMP JSON
[Should show changes]

# 5. Restore original
> CONFIG LOAD backup.json
> DUMP JSON
[Should match step 1 output]

# 6. Persist
> SAVE
```

### Automated Testing (Future)
```cpp
// test_json_migration.cpp
void testBackwardCompatibility() {
    // Load old schema v1 JSON
    loadConfigFromJSON(oldSchemaJSON);

    // Verify defaults applied
    assert(systemConfig.displayBrightness == 200);

    // Export and verify schema v2
    String exported = dumpToString();
    assert(exported.contains("\"schemaVersion\": 2"));
}
```

---

## Common Pitfalls

### ❌ Don't: Change Field Types
```cpp
// BAD - Breaking change
uint16_t sensorInterval;  // Was uint8_t
```
**Fix:** Add new field, deprecate old one

### ❌ Don't: Rename Fields
```cpp
// BAD - Old JSON won't work
display["lcdBrightness"]  // Was "brightness"
```
**Fix:** Keep old name or add migration code

### ❌ Don't: Remove Required Fields
```cpp
// BAD - Old JSON will fail
// Removed: inputObj["pin"]
```
**Fix:** Mark deprecated first, remove in major version

### ✅ Do: Use Default Values
```cpp
// GOOD - Backward compatible
input->priority = inputObj["priority"] | 128;
```

### ✅ Do: Check for Null
```cpp
// GOOD - Safe for old JSON
if (inputObj["priority"].isNull() == false) {
    input->priority = inputObj["priority"];
}
```

### ✅ Do: Increment Versions
```cpp
// GOOD - Track changes
#define SYSTEM_CONFIG_VERSION 3  // Was 2
```

---

## Schema Version History

### Schema v1 (Initial Release - v0.4.1)
- Firmware info: version, platform, timestamp
- System config: outputs, display, timing, pins, constants
- Inputs: pin, app, sensor, units, alarm, flags, calibration

### Schema v2 (Planned - v0.5.0)
- **Added:** `systemConfig.displayBrightness`
- **Added:** `input.priority`
- **Removed:** `systemConfig.reserved1` (deprecated in v0.4.2)

---

## Migration Support

If you need help migrating old configurations:

1. Check firmware changelog for breaking changes
2. Use `DUMP JSON` to export before upgrading
3. Upgrade firmware
4. Use `CONFIG LOAD` to restore (will apply defaults for new fields)
5. Review with `DUMP JSON` and adjust as needed
6. Use `SAVE` to persist

For complex migrations, consider writing a Python script to transform old JSON to new format.

---

## See Also

- **[tools/README.md](../../../tools/README.md)** - Complete configure.py documentation
- **[JSON_QUICK_REFERENCE.md](JSON_QUICK_REFERENCE.md)** - Quick cheat sheet for JSON changes
- **[SERIAL_COMMANDS.md](../../reference/SERIAL_COMMANDS.md)** - CONFIG SAVE/LOAD commands
- **[EEPROM_STRUCTURE.md](../../architecture/EEPROM_STRUCTURE.md)** - How configurations are stored
- **[REGISTRY_SYSTEM.md](../../architecture/REGISTRY_SYSTEM.md)** - Understanding sensor/app registries
- **[CHANGELOG.md](../../../CHANGELOG.md)** - Version-specific changes and breaking changes

## Questions?

- Check `docs/CHANGELOG.md` for version-specific changes
- Review this guide for structural changes
- Test with `DUMP JSON → CONFIG SAVE → CONFIG LOAD → DUMP JSON` cycle
- For custom calibrations, see configure.py documentation in tools/README.md

---

**End of Guide**
