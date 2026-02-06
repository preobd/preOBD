# JSON Configuration Quick Reference

A cheat sheet for maintaining JSON backward compatibility in preOBD.

## When Do I Need to Update JSON Code?

| Change | JSON Update? | Files to Modify | Schema Version? |
|--------|-------------|-----------------|-----------------|
| Add field to `SystemConfig` | ✅ Yes | `json_config.cpp` export/import | ❌ No (backward compatible) |
| Add field to `Input` | ✅ Yes | `json_config.cpp` export/import | ❌ No (backward compatible) |
| Add calibration type | ✅ Yes | `json_config.cpp` export/import | ❌ No (backward compatible) |
| Add sensor/app/unit | ❌ No | Nothing! Registry handles it | ❌ No |
| Remove field | ✅ Yes | Update export/import + migration | ✅ Yes (breaking change) |
| Rename field | ✅ Yes | Update export/import + migration | ✅ Yes (breaking change) |
| Change field type | ✅ Yes | Update export/import + migration | ✅ Yes (breaking change) |

## Quick Checklist: Adding a Field

### SystemConfig Field
```cpp
// 1. system_config.h - Add to struct
uint16_t newField;

// 2. system_config.cpp - Set default
systemConfig.newField = 123;

// 3. system_config.h - Increment version
#define SYSTEM_CONFIG_VERSION 3  // Was 2

// 4. json_config.cpp - Export
systemObj["newField"] = systemConfig.newField;

// 5. json_config.cpp - Import (with default!)
systemConfig.newField = systemObj["newField"] | 123;
```

### Input Field
```cpp
// 1. input.h - Add to Input struct
uint8_t newField;

// 2. input_manager.cpp - Add to InputEEPROM
uint8_t newField;

// 3. input_manager.cpp - Save/Load
eeprom.newField = input.newField;  // Save
input.newField = eeprom.newField;  // Load

// 4. input_manager.cpp - Increment version
#define EEPROM_VERSION 4  // Was 3

// 5. json_config.cpp - Export
inputObj["newField"] = input->newField;

// 6. json_config.cpp - Import (with default!)
input->newField = inputObj["newField"] | 0;
```

## Default Value Patterns

### Always Use Defaults on Import!
```cpp
// ✅ GOOD - Backward compatible
input->priority = inputObj["priority"] | 128;
config.brightness = display["brightness"] | 200;

// ❌ BAD - Old JSON will crash
input->priority = inputObj["priority"];  // Missing field = crash!
```

### Check for Null Before Using
```cpp
// ✅ GOOD - Safe for old JSON
if (inputObj["priority"].isNull() == false) {
    input->priority = inputObj["priority"];
} else {
    input->priority = 128;  // Default
}

// ✅ EVEN BETTER - One-liner with default
input->priority = inputObj["priority"] | 128;
```

## Schema Version Management

### When to Increment

```cpp
// json_config.cpp
#define JSON_SCHEMA_VERSION 2  // Increment from 1

// Only increment for BREAKING changes:
// - Removing fields
// - Renaming fields
// - Changing field types
// - Changing enum values

// DO NOT increment for:
// - Adding optional fields (use defaults!)
// - Adding registry entries
// - Internal refactoring
```

### Version Check on Import

The system automatically handles version checking:
- Old schema (v1) loading into new firmware (v2) → Migration applied
- New schema (v2) loading into old firmware (v1) → Warning shown
- Same version → Normal load

```cpp
// Automatic - already implemented!
uint8_t schemaVer = doc["schemaVersion"] | 1;
if (schemaVer < JSON_SCHEMA_VERSION) {
    // Apply migration logic here
}
```

## Testing Your Changes

```bash
# 1. Before changes - export baseline
> DUMP JSON
[Save output as baseline.json]

# 2. After changes - verify new field appears
> DUMP JSON
[Should show new field with default value]

# 3. Test import of OLD config (without new field)
> CONFIG LOAD baseline.json
[Should use default for new field]

# 4. Verify round-trip
> DUMP JSON
[Should show old values + new default]
```

## Common Mistakes

### ❌ Forgetting Default Values
```cpp
// BAD
input->priority = inputObj["priority"];  // Crash if missing!

// GOOD
input->priority = inputObj["priority"] | 128;
```

### ❌ Not Incrementing EEPROM Version
```cpp
// BAD - Old EEPROM layout conflicts with new struct
struct Input {
    uint8_t newField;  // Added but version not incremented!
};

// GOOD
#define EEPROM_VERSION 4  // Was 3
```

### ❌ Breaking Changes Without Migration
```cpp
// BAD - Schema v2 but no migration code
#define JSON_SCHEMA_VERSION 2
// Removed field, but loadConfigFromJSON() has no migration logic!

// GOOD
#define JSON_SCHEMA_VERSION 2
// In loadConfigFromJSON():
if (schemaVer == 1) {
    // Handle old schema - skip removed field
}
```

## File Quick Reference

| File | What to Update | When |
|------|----------------|------|
| `src/lib/system_config.h` | Add field to struct, increment version | SystemConfig change |
| `src/lib/system_config.cpp` | Set default value | SystemConfig change |
| `src/inputs/input.h` | Add field to Input struct | Input change |
| `src/inputs/input_manager.cpp` | InputEEPROM, save/load, version | Input change |
| `src/lib/json_config.cpp` | Export/import functions | Any struct change |
| `src/lib/json_config.cpp` | `JSON_SCHEMA_VERSION` | Breaking change only |
| `src/lib/json_config.h` | Version history comment | Schema version change |
| `docs/guides/configuration/JSON_CONFIGURATION_GUIDE.md` | Document change | Any change |
| `docs/CHANGELOG.md` | User-facing notes | Any change |

## Example: Full Workflow

**Task:** Add `displayBrightness` to SystemConfig

```bash
# Step 1: Branch
git checkout -b feature/display-brightness

# Step 2: Update struct
# Edit: src/lib/system_config.h
struct SystemConfig {
    uint16_t displayBrightness;
    uint8_t reserved[4];  // Was 6
};
#define SYSTEM_CONFIG_VERSION 3  // Was 2

# Step 3: Set default
# Edit: src/lib/system_config.cpp
void resetSystemConfig() {
    systemConfig.displayBrightness = 200;
}

# Step 4: Export
# Edit: src/lib/json_config.cpp → exportSystemConfigToJSON()
display["brightness"] = systemConfig.displayBrightness;

# Step 5: Import with default
# Edit: src/lib/json_config.cpp → importSystemConfigFromJSON()
systemConfig.displayBrightness = display["brightness"] | 200;

# Step 6: Test
pio run -e megaatmega2560  # Compile
# Upload and test:
> DUMP JSON  # Should show "brightness": 200
> CONFIG SAVE test.json
> RESET
> CONFIG LOAD test.json
> DUMP JSON  # Should match

# Step 7: Document
# Edit: docs/CHANGELOG.md
## v0.5.0
- Added display brightness control (0-255)

# Step 8: Commit
git commit -m "feat: Add display brightness control"
```

## Tips

1. **Always test backward compatibility** - Load old JSON into new firmware
2. **Use meaningful defaults** - 0 is often wrong (prefer 128 for priority, 200 for brightness)
3. **Document breaking changes** - Users need migration guides
4. **Version everything** - EEPROM version, schema version, firmware version
5. **Keep it simple** - Don't remove fields unless absolutely necessary

## Need Help?

- See full guide: `docs/guides/configuration/JSON_CONFIGURATION_GUIDE.md`
- Check examples in configuration guide
- Review recent commits for patterns
- Test with `DUMP JSON` → `CONFIG LOAD` cycle
- For configure.py usage: `tools/README.md`

---

**Last Updated:** 2025-12-19
**Current Schema Version:** 1
**Current Firmware Version:** 0.5.0-alpha (Unreleased)
