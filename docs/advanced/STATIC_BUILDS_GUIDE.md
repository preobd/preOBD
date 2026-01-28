# Static Builds Guide

**Using configure.py for compile-time sensor configuration**

---

## Overview

Static builds define your sensor configuration at compile time rather than through serial commands. This is done using the `configure.py` tool, which generates the necessary configuration files.

**Use static builds when:**
- Using Arduino Uno or other memory-constrained boards
- Configuration will never change in the field
- You want the smallest possible binary
- You need version-controlled, reproducible configurations

**Use default (serial command) configuration when:**
- Using boards with 8KB+ RAM (Mega, Teensy, ESP32)
- You want to change sensors without reflashing
- You're still developing or testing
- You want CONFIG/RUN mode features

---

## Quick Start

```bash
# Navigate to project root
cd /path/to/openEMS

# Run the configuration tool
python3 tools/configure.py

# Follow the interactive prompts to configure sensors

# Build and upload (use uno_static environment)
pio run -e uno_static -t upload
```

---

## Requirements

- Python 3.7 or higher
- No external dependencies (uses Python standard library only)

Verify installation:
```bash
python3 --version    # Should show 3.7 or higher
```

---

## Using configure.py

### Interactive Mode

Run without arguments for interactive configuration:

```bash
python3 tools/configure.py
```

The tool will:
1. Detect your target platform from `platformio.ini`
2. Prompt for each sensor configuration
3. Validate pin assignments
4. Offer custom calibration options
5. Generate `config.h` with your configuration
6. Optionally save to JSON for later reuse

**Example session:**
```
=== openEMS Static Configuration Tool v1.0.0 ===

Loading registries...
  ✓ 28 sensors from sensor_library.h (+ sensor_library/sensors/)
  ✓ 18 applications from application_presets.h
  ✓ 13 units from units_registry.h

Detected platform: megaatmega2560 (from platformio.ini)
Board limits: 54 digital pins, 16 analog pins

=== Adding Input 0 ===

Applications:
  0: CHT - Cylinder Head Temperature
  1: EGT - Exhaust Gas Temperature
  2: COOLANT_TEMP - Engine Coolant
  3: OIL_TEMP - Engine Oil
  4: OIL_PRESSURE - Oil Pressure
  ...
Select application [0]: 0

Compatible sensors for CHT:
  1: MAX6675 - K-Type Thermocouple (MAX6675)
  2: MAX31855 - K-Type Thermocouple (MAX31855)
Select sensor [1]: 1

Enter pin (digital for SPI CS): 6
  ✓ Pin 6 valid for MAX6675 (digital)

Add custom calibration? [y/N]: n

Configure another input? [y/N]: y

=== Adding Input 1 ===
...
```

### Loading Saved Configurations

Load a previously saved JSON configuration:

```bash
python3 tools/configure.py --load tools/saved-configs/my_config.json
```

You can then:
- Review the configuration
- Edit existing inputs
- Add new inputs
- Regenerate config.h

### Command-Line Options

```bash
# Interactive mode (default)
python3 tools/configure.py

# Load existing configuration
python3 tools/configure.py --load tools/saved-configs/my_config.json

# Override platform detection
python3 tools/configure.py --platform uno

# Specify project directory
python3 tools/configure.py --project-dir /path/to/openEMS

# Generate thin libraries (advanced memory optimization)
python3 tools/configure.py --generate-thin-libs
```

---

## JSON Configuration Files

The tool saves configurations as JSON files in `tools/saved-configs/`. This allows you to:

- Track configurations in version control
- Share configurations between team members
- Quickly restore known-good configurations
- Switch between different sensor setups

### JSON Format

```json
{
  "schemaVersion": 1,
  "mode": "static",
  "metadata": {
    "toolVersion": "1.0.0",
    "platform": "uno",
    "timestamp": 1704067200
  },
  "inputs": [
    {
      "idx": 0,
      "pin": "6",
      "application": "CHT",
      "applicationIndex": 0,
      "sensor": "MAX6675",
      "sensorIndex": 1
    },
    {
      "idx": 1,
      "pin": "A2",
      "application": "COOLANT_TEMP",
      "applicationIndex": 2,
      "sensor": "VDO_120C_TABLE",
      "sensorIndex": 3
    }
  ]
}
```

### Saving Configurations

At the end of configuration, the tool prompts:
```
Save this configuration to JSON? (y/n): y
Enter filename (e.g., my_config.json): land_rover_config.json

✓ Saved to tools/saved-configs/land_rover_config.json
```

---

## Custom Calibrations

For sensors not in the library, or to override defaults, configure.py supports custom calibrations:

### During Configuration

```
Add custom calibration? [y/N]: y

Calibration types:
  1: Thermistor (Steinhart-Hart)
  2: Pressure (Linear)
  3: Thermistor (Beta)
  4: Pressure (Polynomial)
  5: RPM
Select type [1]: 1

Enter bias resistor (ohms) [1000.0]: 470
Enter Steinhart-Hart A coefficient: 1.129e-03
Enter Steinhart-Hart B coefficient: 2.341e-04
Enter Steinhart-Hart C coefficient: 8.775e-08

✓ Custom calibration configured
```

### Calibration Types

| Type | Use Case | Parameters |
|------|----------|------------|
| Thermistor (Steinhart-Hart) | Custom NTC thermistors | bias_resistor, A, B, C coefficients |
| Thermistor (Beta) | NTC with known β value | bias_resistor, β, R₀, T₀ |
| Pressure (Linear) | 0.5-4.5V sensors | voltage_min/max, pressure_min/max |
| Pressure (Polynomial) | VDO-style sensors | bias_resistor, poly_a/b/c |
| RPM | Custom alternator | poles, pulley_ratio, timeout |

### Custom Calibration in JSON

```json
{
  "idx": 1,
  "pin": "A0",
  "application": "OIL_TEMP",
  "sensor": "VDO_150C_STEINHART",
  "calibration": {
    "type": "THERMISTOR_STEINHART",
    "source": "CUSTOM",
    "params": {
      "biasResistor": 470.0,
      "steinhartA": 1.129e-03,
      "steinhartB": 2.341e-04,
      "steinhartC": 8.775e-08
    }
  }
}
```

---

## Generated Files

configure.py generates/updates these files:

### src/config.h

The tool updates the static configuration block:

```cpp
#ifdef USE_STATIC_CONFIG

// ============================================================================
// STATIC SENSOR CONFIGURATION
// Generated by openEMS-config v1.0.0 on 2025-01-15 14:30:00
// Platform: uno
// DO NOT EDIT MANUALLY - Use tools/configure.py to regenerate
// ============================================================================

#define NUM_CONFIGURED_INPUTS 3

// ----- Input 0: CHT -----
#define INPUT_0_PIN           6
#define INPUT_0_APPLICATION   0            // CHT
#define INPUT_0_SENSOR        1            // MAX6675

// ----- Input 1: COOLANT_TEMP -----
#define INPUT_1_PIN           A2
#define INPUT_1_APPLICATION   2            // COOLANT_TEMP
#define INPUT_1_SENSOR        3            // VDO_120C_TABLE

// ----- Input 2: OIL_PRESSURE -----
#define INPUT_2_PIN           A3
#define INPUT_2_APPLICATION   4            // OIL_PRESSURE
#define INPUT_2_SENSOR        8            // VDO_5BAR_CURVE

#endif // USE_STATIC_CONFIG
```

### src/lib/generated/static_calibrations.h

If custom calibrations are defined:

```cpp
// Auto-generated by tools/configure.py v1.0.0
// DO NOT EDIT MANUALLY

// ----- Input 1: OIL_TEMP Custom Calibration -----
#define INPUT_1_CUSTOM_CALIBRATION
static const PROGMEM ThermistorSteinhartCalibration input_1_custom_cal = {
    .bias_resistor = 470.0,
    .steinhart_a = 1.129e-03,
    .steinhart_b = 2.341e-04,
    .steinhart_c = 8.775e-08
};
```

---

## Thin Library Generation

For maximum memory savings on Arduino Uno, use thin library generation:

```bash
python3 tools/configure.py --generate-thin-libs
```

This creates optimized versions of the sensor library and application presets containing only the sensors you're using, significantly reducing flash usage.

**Example savings:**
```
Full library:     ~8KB flash
Thin library:     ~2KB flash
Savings:          ~6KB (75%)
```

---

## Complete Workflow Example

### Arduino Uno Setup

```bash
# 1. Create configuration
python3 tools/configure.py --platform uno

# Configure 4 sensors:
# - Input 0: CHT with MAX6675 on pin 6
# - Input 1: Coolant with VDO_120C on A2
# - Input 2: Oil pressure with VDO_5BAR_CURVE on A3
# - Input 3: Battery voltage on A6

# Save as JSON
Save to JSON? y
Filename: uno_basic.json

# 2. Generate thin libraries for memory savings
python3 tools/configure.py --load tools/saved-configs/uno_basic.json --generate-thin-libs

# 3. Build and upload
pio run -e uno -t upload

# 4. Monitor
pio device monitor
```

### Modifying Configuration

```bash
# Load existing config
python3 tools/configure.py --load tools/saved-configs/uno_basic.json

# Select [e] to edit, [a] to add, [d] to delete
# Make changes
# Save back to JSON
# Rebuild and upload
```

---

## Memory Comparison

| Configuration | RAM | Flash | Notes |
|---------------|-----|-------|-------|
| Serial commands (default) | ~3KB | ~35KB | Full features |
| Static build | ~2KB | ~30KB | No serial config |
| Static + thin libs | ~1.5KB | ~25KB | Minimum size |

---

## What Gets Disabled

When using static builds (`USE_STATIC_CONFIG` defined in platformio.ini):

**Note**: The `uno_static` environment in platformio.ini automatically enables `USE_STATIC_CONFIG` and excludes the ArduinoJson library, saving 4-8KB of flash.

| Feature | Static Build | Default |
|---------|--------------|---------|
| Serial configuration commands | ❌ Disabled | ✅ Available |
| CONFIG/RUN mode switching | ❌ Disabled | ✅ Available |
| EEPROM sensor storage | ❌ Not used | ✅ Used |
| SAVE/LOAD commands | ❌ Disabled | ✅ Available |
| Runtime sensor changes | ❌ Must reflash | ✅ Via commands |
| LIST commands | ⚠️ Limited | ✅ Full |

---

## Troubleshooting

### Platform not detected

```
ERROR: Could not find platformio.ini
```

**Solution:** Run from project root directory, or use `--project-dir`:
```bash
python3 tools/configure.py --project-dir /path/to/openEMS
```

### Pin validation errors

```
Pin 13: ERROR - Reserved for SPI SCK
```

**Solution:** Choose a different pin. Pin 13 is used for SPI clock.

### Registry files not found

```
Error: Could not find sensor_library.h
```

**Solution:** Ensure you're in the project root and the source files exist. The sensor library uses a modular structure with `sensor_library.h` as the orchestrator and sensor definitions in `sensor_library/sensors/*.h`.

### JSON file not loading

```
ERROR: File not found: my_config.json
```

**Solution:** Use the full path:
```bash
python3 tools/configure.py --load tools/saved-configs/my_config.json
```

---

## Best Practices

1. **Save configurations to JSON** - Always save your configuration for reproducibility
2. **Use version control** - Commit JSON files to track configuration history
3. **Test incrementally** - Add one sensor at a time and verify
4. **Keep backups** - The tool creates `.bak` files automatically
5. **Document custom calibrations** - Note sensor part numbers and sources

---

## Related Documentation

- **[tools/README.md](../../tools/README.md)** - Complete configure.py documentation
- **[JSON Configuration Guide](../guides/configuration/JSON_CONFIGURATION_GUIDE.md)** - JSON format details
- **[Sensor Selection Guide](../guides/sensor-types/SENSOR_SELECTION_GUIDE.md)** - Sensor catalog
- **[Advanced Calibration Guide](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** - Calibration details

