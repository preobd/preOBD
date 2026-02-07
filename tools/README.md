# preOBD Configuration Toolchain

**Python-based tools for static configuration generation and validation**

---

## Table of Contents

1. [Overview](#overview)
2. [Why configure.py?](#why-configurepy)
3. [configure.py - Configuration Generator](#configurepy---configuration-generator)
4. [JSON Configuration Format](#json-configuration-format)
5. [Custom Calibrations](#custom-calibrations)
6. [Thin Library Generation](#thin-library-generation)
7. [Platform Detection](#platform-detection)
8. [Pin Type Validation](#pin-type-validation)
9. [validate_registries.py](#validate_registriespy)
10. [Complete Workflows](#complete-workflows)

---

## Overview

The preOBD toolchain provides Python scripts for generating compile-time configurations (`USE_STATIC_CONFIG` mode). These tools replace the old `setup.sh` script with more robust validation, platform awareness, and JSON-based configuration management.

**Key Tools:**
- **configure.py** - Interactive configuration generator
- **validate_registries.py** - Registry validation for CI/CD

**Supported Workflows:**
- Interactive sensor configuration
- JSON-based configuration save/load
- Custom calibration definition
- Thin library generation (memory optimization)
- Platform-aware pin validation

---

## Why configure.py?

### Replaces setup.sh

**Old approach (setup.sh - DEPRECATED):**
- Shell script for basic config generation
- Limited validation
- No platform awareness
- No custom calibration support
- Removed in recent commits

**New approach (configure.py):**
- Python-based with rich validation
- Platform-aware (reads `platformio.ini`)
- JSON save/load for configuration portability
- Custom calibration support
- Pin type validation (ANALOG/DIGITAL/I2C)
- Thin library generation for memory optimization

### When to Use configure.py

**Use configure.py for:**
- **Arduino Uno** - Minimal RAM, requires compile-time config
- **Stable setups** - Configuration rarely changes
- **Memory optimization** - Thin libraries reduce flash usage
- **Version control** - JSON configs tracked in git
- **Team collaboration** - Share configurations via JSON

**Use runtime mode for:**
- **Teensy/Mega** - Plenty of RAM for EEPROM mode
- **Development** - Frequent configuration changes
- **Field updates** - Change config without reflashing

---

## configure.py - Configuration Generator

### Installation

**Requirements:**
- Python 3.7+
- No external dependencies (uses stdlib only)

**Verify installation:**
```bash
python3 --version  # Should show 3.7 or higher
python3 tools/configure.py --help
```

### Basic Usage

#### Interactive Mode (Recommended)

**Start configuration:**
```bash
python3 tools/configure.py
```

**Interactive prompts:**
```
========================================
  preOBD Configuration Generator
========================================

Detected platform: megaatmega2560 (from platformio.ini)
Board limits: 54 digital pins, 16 analog pins

How many inputs to configure? (1-10): 3

=== Input 1 ===
Pin (e.g., A0, 6, I2C): 6
Available applications:
  1. CHT (Cylinder Head Temperature)
  2. EGT (Exhaust Gas Temperature)
  3. COOLANT_TEMP (Engine Coolant)
  ...
Select application: 1

Recommended sensor for CHT: MAX6675
Available sensors for TEMPERATURE:
  1. MAX6675 (K-Type Thermocouple, 0-1024°C) [RECOMMENDED]
  2. MAX31855 (K-Type Thermocouple, -200-1350°C)
  3. VDO_120C_LOOKUP (VDO Thermistor, -40-120°C)
  ...
Select sensor: 1

Use custom calibration? (y/N): N

=== Input 2 ===
...
```

**Output:**
- Generates `src/config.h` with sensor definitions
- Optionally saves JSON to `tools/saved-configs/`

#### Load Existing Configuration

**Load and edit:**
```bash
python3 tools/configure.py --load tools/saved-configs/my_vehicle.json
```

**Workflow:**
1. Loads existing input configuration
2. Allows interactive editing (add/remove/modify inputs)
3. Regenerates `config.h`
4. Updates JSON file

#### Generate Thin Libraries

**Optimize flash usage:**
```bash
python3 tools/configure.py --generate-thin-libs
```

**What it does:**
- Analyzes your configuration
- Extracts only used sensors and applications
- Generates `src/lib/generated/sensor_library_static.h`
- Generates `src/lib/generated/application_presets_static.h`
- Significantly reduces flash usage

**Flash savings:**
- Full libraries: ~5.5KB
- Thin libraries (3 sensors): ~1.5KB
- **Savings: ~4KB** (critical for Arduino Uno!)

#### Platform Override

**Specify platform explicitly:**
```bash
python3 tools/configure.py --platform teensy40
```

**Supported platforms:**
- `megaatmega2560` (Arduino Mega)
- `uno` (Arduino Uno)
- `teensy40` (Teensy 4.0)
- `teensy41` (Teensy 4.1)
- `teensy36` (Teensy 3.6)

---

## JSON Configuration Format

### Schema Version 1

**Structure:**
```json
{
  "schemaVersion": 1,
  "mode": "static",
  "metadata": {
    "toolVersion": "1.0.0",
    "platform": "megaatmega2560",
    "timestamp": 1704067200,
    "description": "My 1975 Land Cruiser"
  },
  "inputs": [
    {
      "idx": 0,
      "pin": "6",
      "application": "CHT",
      "applicationIndex": 1,
      "sensor": "MAX6675",
      "sensorIndex": 1,
      "calibration": null
    },
    {
      "idx": 1,
      "pin": "A0",
      "application": "OIL_TEMP",
      "applicationIndex": 4,
      "sensor": "VDO_150C_LOOKUP",
      "sensorIndex": 3,
      "calibration": {
        "type": "THERMISTOR_STEINHART",
        "source": "CUSTOM",
        "params": {
          "bias_resistor": 2200,
          "a": 1.129e-3,
          "b": 2.341e-4,
          "c": 8.775e-8
        }
      }
    }
  ]
}
```

### Field Descriptions

**Top-level:**
- `schemaVersion`: JSON format version (currently 1)
- `mode`: "static" for compile-time config
- `metadata`: Configuration metadata

**Metadata:**
- `toolVersion`: configure.py version that created JSON
- `platform`: Target board (from platformio.ini)
- `timestamp`: Unix timestamp of creation
- `description`: User-provided description (optional)

**Input:**
- `idx`: Input index (0-9)
- `pin`: Physical pin ("6", "A0", "I2C")
- `application`: Application name ("CHT", "OIL_PRESSURE")
- `applicationIndex`: Index in APPLICATION_PRESETS array
- `sensor`: Sensor name ("MAX6675", "VDO_5BAR")
- `sensorIndex`: Index in SENSOR_LIBRARY array
- `calibration`: Custom calibration (null if using preset)

**Calibration:**
- `type`: Calibration type enum
- `source`: "CUSTOM" or "PRESET"
- `params`: Type-specific parameters (see below)

---

## Custom Calibrations

### Overview

Custom calibrations allow you to use sensors not in the default library or fine-tune existing sensors.

**Calibration Types:**
1. **THERMISTOR_STEINHART** - Steinhart-Hart equation for NTC thermistors
2. **PRESSURE_LINEAR** - Linear voltage-to-pressure mapping
3. **PRESSURE_POLY** - VDO polynomial pressure sensors
4. **RPM** - W-phase alternator RPM sensing

### Thermistor Steinhart-Hart

**Use case:** Custom NTC thermistor with known coefficients

**Parameters:**
- `bias_resistor`: Pull-down resistor (Ω)
- `a`, `b`, `c`: Steinhart-Hart coefficients

**Equation:**
```
1/T(K) = a + b*ln(R) + c*ln(R)³
```

**Example - Interactive:**
```
Use custom calibration? (y/N): y

Calibration types:
  1. THERMISTOR_STEINHART
  2. PRESSURE_LINEAR
  3. PRESSURE_POLY
  4. RPM
Select type: 1

Bias resistor (Ω): 10000
Steinhart-Hart coefficient A: 1.129e-3
Steinhart-Hart coefficient B: 2.341e-4
Steinhart-Hart coefficient C: 8.775e-8
```

**Example - JSON:**
```json
{
  "calibration": {
    "type": "THERMISTOR_STEINHART",
    "source": "CUSTOM",
    "params": {
      "bias_resistor": 10000,
      "a": 1.129e-3,
      "b": 2.341e-4,
      "c": 8.775e-8
    }
  }
}
```

**How to get coefficients:**
1. Check sensor datasheet for Steinhart-Hart coefficients
2. Or use online calculators with 3 temp/resistance points
3. Or measure R at 3 temps and calculate:
   - [SH Calculator](https://www.thinksrs.com/downloads/programs/therm%20calc/ntccalibrator/ntccalculator.html)

### Pressure Linear

**Use case:** Generic linear pressure sensors (0.5-4.5V output)

**Parameters:**
- `voltage_min`: Minimum voltage (V)
- `voltage_max`: Maximum voltage (V)
- `pressure_min`: Pressure at Vmin (bar)
- `pressure_max`: Pressure at Vmax (bar)

**Equation:**
```
P = Pmin + (V - Vmin) * (Pmax - Pmin) / (Vmax - Vmin)
```

**Example - Interactive:**
```
Select type: 2

Minimum voltage (V): 0.5
Maximum voltage (V): 4.5
Pressure at Vmin (bar): 0.0
Pressure at Vmax (bar): 7.0
```

**Example - JSON:**
```json
{
  "calibration": {
    "type": "PRESSURE_LINEAR",
    "source": "CUSTOM",
    "params": {
      "voltage_min": 0.5,
      "voltage_max": 4.5,
      "pressure_min": 0.0,
      "pressure_max": 7.0
    }
  }
}
```

### Pressure Polynomial (VDO-style)

**Use case:** VDO pressure sensors with polynomial calibration

**Parameters:**
- `bias_resistor`: Pull-down resistor (Ω)
- `poly_a`, `poly_b`, `poly_c`: Polynomial coefficients

**Equation:**
```
P = a*R + b*R² + c*R³
```

**Example - JSON:**
```json
{
  "calibration": {
    "type": "PRESSURE_POLY",
    "source": "CUSTOM",
    "params": {
      "bias_resistor": 184,
      "poly_a": -6.75e-4,
      "poly_b": 2.54e-6,
      "poly_c": 1.87e-9
    }
  }
}
```

### RPM Calibration

**Use case:** Custom alternator specs or pulley ratios

**Parameters:**
- `poles`: Alternator pole count (8, 10, 12, 14, 16, 18)
- `pulley_ratio`: Crank/alternator pulley ratio (typically 2.0-3.5)
- `calibration_mult`: Fine-tuning multiplier (default 1.0)
- `timeout_ms`: Zero RPM timeout (100-10000ms)
- `min_rpm`: Minimum valid RPM (reject noise)
- `max_rpm`: Maximum valid RPM (reject spikes)

**RPM Calculation:**
```
RPM = (frequency * 60) / (poles / 2) / pulley_ratio * calibration_mult
```

**Example - Interactive:**
```
Select type: 4

Alternator poles: 12
Pulley ratio (crank/alt): 3.0
Calibration multiplier (1.0 = no adjustment): 1.02
Timeout (ms): 2000
Minimum RPM: 100
Maximum RPM: 8000
```

**Example - JSON:**
```json
{
  "calibration": {
    "type": "RPM",
    "source": "CUSTOM",
    "params": {
      "poles": 12,
      "pulley_ratio": 3.0,
      "calibration_mult": 1.02,
      "timeout_ms": 2000,
      "min_rpm": 100,
      "max_rpm": 8000
    }
  }
}
```

**Determining pulley ratio:**
```
Ratio = Crank_Pulley_Diameter / Alternator_Pulley_Diameter

Example:
  Crank pulley: 6 inches
  Alt pulley: 2 inches
  Ratio = 6 / 2 = 3.0
```

---

## Thin Library Generation

### What Are Thin Libraries?

**Problem:** Full sensor and application registries consume ~5.5KB of flash memory, even if you only use 3 sensors.

**Solution:** Generate minimal libraries containing only the sensors/applications you've configured.

### How It Works

**1. Analyze configuration:**
```bash
python3 tools/configure.py --load my_config.json --generate-thin-libs
```

**2. Extract used items:**
```
Analyzing configuration...
  Using 3 sensors: MAX6675, VDO_150C_LOOKUP, VDO_5BAR
  Using 3 applications: CHT, OIL_TEMP, OIL_PRESSURE
```

**3. Generate thin libraries:**
```
Generating thin libraries...
  Created: src/lib/generated/sensor_library_static.h (1.2KB)
  Created: src/lib/generated/application_presets_static.h (800B)
  Total savings: ~3.5KB
```

**4. Update config.h:**
```cpp
#define USE_STATIC_CONFIG
#define USE_THIN_LIBRARIES  // Generated automatically
```

### Flash Savings

| Configuration | Full Libraries | Thin Libraries | Savings |
|---------------|----------------|----------------|---------|
| 3 sensors | 5.5KB | 2.0KB | 3.5KB |
| 5 sensors | 5.5KB | 2.8KB | 2.7KB |
| 10 sensors | 5.5KB | 4.2KB | 1.3KB |

**Critical for Arduino Uno:**
- Uno has 32KB flash total
- Typical firmware: ~28KB without thin libs
- With thin libs: ~24KB (4KB savings!)
- Difference between fitting or not fitting!

### Limitations

**Thin libraries are static:**
- Cannot add sensors without regenerating
- Runtime mode incompatible (needs full registry)
- Use only for stable, compile-time configurations

---

## Platform Detection

### Automatic Detection

**How it works:**
1. Reads `platformio.ini` from project root
2. Parses `[env:...]` sections
3. Extracts `board = ...` value
4. Maps to hardware constraints

**Example platformio.ini:**
```ini
[env:megaatmega2560]
platform = atmelavr
board = megaatmega2560
framework = arduino
```

**Detection output:**
```
Detected platform: megaatmega2560
  Digital pins: 0-53
  Analog pins: A0-A15
  Maximum inputs: 10
```

### Platform Constraints

**Arduino Uno:**
- Digital pins: 0-13
- Analog pins: A0-A5
- Max inputs: 6 (EEPROM limit)
- EEPROM: 1024 bytes

**Arduino Mega:**
- Digital pins: 0-53
- Analog pins: A0-A15
- Max inputs: 10
- EEPROM: 4096 bytes

**Teensy 4.0:**
- Digital pins: 0-39
- Analog pins: A0-A13 (but actually digital 14-27)
- Max inputs: 10
- EEPROM: 1080 bytes (emulated)

**Teensy 4.1:**
- Digital pins: 0-54
- Analog pins: A0-A17 (but actually digital)
- Max inputs: 10
- EEPROM: 1080 bytes (emulated)

### Pin Conflict Detection

**Reserved pins:**
- **SPI:** 10, 11, 12, 13 (Mega), 10, 11, 12, 13 (Uno)
- **I2C:** 20, 21 (Mega), A4, A5 (Uno)
- **Serial:** 0, 1 (all platforms)

**Validation:**
```
Pin 13 (Input 2): WARNING - Pin 13 is SPI SCK
  Shared with MAX6675 sensors, may cause conflicts
  Continue? (y/N):
```

---

## Pin Type Validation

### Pin Type Requirements

Each sensor requires a specific pin type:

**PIN_ANALOG:**
- VDO thermistors (resistance measurement)
- VDO pressure sensors (resistance measurement)
- Linear pressure sensors (voltage measurement)
- Voltage dividers (battery monitoring)

**PIN_DIGITAL:**
- MAX6675 (SPI chip select)
- MAX31855 (SPI chip select)
- Float switches (digital HIGH/LOW)

**PIN_I2C:**
- BME280 (temperature/pressure/humidity)
- Use pseudo-pin "I2C" (not a physical pin number)

### Validation Rules

**1. Analog sensors require analog pins:**
```
Pin 6 (Input 1): ERROR
  Sensor VDO_150C_LOOKUP requires ANALOG pin
  Pin 6 is DIGITAL
  Suggestion: Use A0, A1, A2, etc.
```

**2. Digital sensors require digital pins:**
```
Pin A0 (Input 1): ERROR
  Sensor MAX6675 requires DIGITAL pin
  Pin A0 is ANALOG (but can work in digital mode)
  Suggestion: Use pin 6, 7, 8, etc. for clarity
```

**3. I2C sensors use "I2C" pseudo-pin:**
```
Pin I2C (Input 1): OK
  Sensor BME280_TEMP uses I2C bus
  I2C pins: SDA=20, SCL=21 (Mega)
```

### Common Pin Assignment Mistakes

**❌ Wrong: CHT on analog pin**
```cpp
#define INPUT_0_PIN A0       // Analog pin
#define INPUT_0_SENSOR MAX6675  // Requires DIGITAL for SPI CS!
```

**✅ Correct: CHT on digital pin**
```cpp
#define INPUT_0_PIN 6        // Digital pin for SPI chip select
#define INPUT_0_SENSOR MAX6675
```

**Why this changed in v0.4.0:**
- Old examples showed `A0, A1` for CHT (WRONG)
- New validation enforces PIN_DIGITAL for MAX6675/MAX31855
- Updated examples use `6, 7` for CHT

---

## validate_registries.py

### Purpose

Validates C registry files (`sensor_library.h`, `application_presets.h`, `units_registry.h`) for:
- Hash collisions
- Duplicate names
- Invalid cross-references
- Incorrect hash values

### Usage

**Run validation:**
```bash
python3 tools/validate_registries.py
```

**Output (success):**
```
Validating sensor_library.h...
  ✓ No hash collisions detected (20 sensors)
  ✓ All hashes match djb2 algorithm
  ✓ All default sensor references valid

Validating application_presets.h...
  ✓ No hash collisions detected (16 applications)
  ✓ All hashes match djb2 algorithm
  ✓ All default sensor references exist

Validating units_registry.h...
  ✓ No hash collisions detected (11 units)
  ✓ All hashes match djb2 algorithm

All validations passed!
```

**Output (errors):**
```
Validating sensor_library.h...
  ✗ Hash collision detected!
    MY_SENSOR_A and MY_SENSOR_B both hash to 0x1234
  ✗ Hash mismatch for MAX6675
    Expected: 0x2A23, Found: 0x1234

VALIDATION FAILED!
```

### When to Run

**Required:**
- Before committing registry changes
- After adding new sensors/applications
- In CI/CD pipelines

**Recommended:**
- After manual registry edits
- When debugging sensor lookup issues

---

## Complete Workflows

### Workflow 1: New Vehicle Configuration

**Scenario:** Configure sensors for a 1975 Toyota Land Cruiser

**Steps:**
```bash
# 1. Run interactive configuration
python3 tools/configure.py

  # Input 1: CHT (Cylinder Head Temp)
  Pin: 6
  Application: CHT
  Sensor: MAX6675
  Custom calibration: No

  # Input 2: EGT (Exhaust Gas Temp)
  Pin: 7
  Application: EGT
  Sensor: MAX31855
  Custom calibration: No

  # Input 3: Oil Temperature
  Pin: A0
  Application: OIL_TEMP
  Sensor: VDO_150C_LOOKUP
  Custom calibration: No

  # Input 4: Oil Pressure
  Pin: A2
  Application: OIL_PRESSURE
  Sensor: VDO_5BAR
  Custom calibration: No

# 2. Save configuration
  Save configuration? (Y/n): Y
  Filename: 1975_land_cruiser.json
  Description: My FJ40 Land Cruiser

# 3. Verify config.h generated
ls src/config.h  # Should exist with your sensor definitions

# 4. Compile and upload
pio run -e megaatmega2560
pio run -t upload -e megaatmega2560
```

**Result:**
- `config.h` configured with 4 sensors
- Configuration saved to `tools/saved-configs/1975_land_cruiser.json`
- Ready to flash to board

---

### Workflow 2: Edit Existing Configuration

**Scenario:** Add boost pressure sensor to existing config

**Steps:**
```bash
# 1. Load existing configuration
python3 tools/configure.py --load tools/saved-configs/1975_land_cruiser.json

# 2. Configuration loaded, add new input
  Current inputs: 4
  Add more inputs? (Y/n): Y

  How many additional inputs: 1

  # Input 5: Boost Pressure
  Pin: A3
  Application: BOOST_PRESSURE
  Sensor: GENERIC_BOOST
  Custom calibration? (y/N): y

    # Custom linear calibration
    Type: PRESSURE_LINEAR
    Vmin: 0.5
    Vmax: 4.5
    Pmin: 0.0
    Pmax: 3.0

# 3. Save updated configuration
  Overwrite 1975_land_cruiser.json? (Y/n): Y

# 4. Recompile and flash
pio run -t upload
```

---

### Workflow 3: Optimize for Arduino Uno

**Scenario:** Reduce flash usage for Uno deployment

**Steps:**
```bash
# 1. Create minimal configuration (3 sensors only)
python3 tools/configure.py

  # Configure 3 essential sensors
  # Input 1: CHT
  # Input 2: Oil Temp
  # Input 3: Oil Pressure

  Save: arduino_uno_minimal.json

# 2. Generate thin libraries
python3 tools/configure.py --load arduino_uno_minimal.json --generate-thin-libs

  Analyzing configuration...
    Using 3 sensors
    Using 3 applications
  Generating thin libraries...
    sensor_library_static.h: 1.2KB
    application_presets_static.h: 800B
    Savings: 3.5KB

# 3. Compile for Uno
pio run -e uno

  Flash usage: 24,832 bytes (76% of 32KB)  # Success!
  RAM usage: 1,456 bytes (71% of 2KB)      # Fits!
```

---

### Workflow 4: Share Configuration with Team

**Scenario:** Multiple team members working on same vehicle

**Steps:**
```bash
# Developer A: Create initial config
python3 tools/configure.py
  # Configure sensors...
  Save: team_vehicle.json

# Commit to git
git add tools/saved-configs/team_vehicle.json
git commit -m "Add sensor configuration for test vehicle"
git push

# Developer B: Pull and use
git pull
python3 tools/configure.py --load tools/saved-configs/team_vehicle.json
  # Modify if needed
  # Regenerate config.h
pio run -t upload
```

---

## Troubleshooting

### Problem: Platform not detected

**Symptom:**
```
ERROR: Could not find platformio.ini
```

**Solution:**
Run from project root:
```bash
cd /path/to/preOBD
python3 tools/configure.py
```

---

### Problem: Pin validation errors

**Symptom:**
```
Pin 13: ERROR - Reserved for SPI SCK
```

**Solution:**
- Use different pin
- Or override with caution (conflicts may occur)

---

### Problem: JSON file not found

**Symptom:**
```
ERROR: File not found: my_config.json
```

**Solution:**
Use full path or relative path from project root:
```bash
python3 tools/configure.py --load tools/saved-configs/my_config.json
```

---

## See Also

- [Registry System](../docs/architecture/REGISTRY_SYSTEM.md) - Hash-based architecture
- [Pin Requirements Guide](../docs/guides/hardware/PIN_REQUIREMENTS_GUIDE.md) - Pin type details
- [Adding Sensors Guide](../docs/guides/configuration/ADDING_SENSORS.md) - Extend sensor library
- [Advanced Config Guide](README_ADVANCED_CONFIG.md) - Advanced calibration topics

---

**Last Updated:** 2025-01-28
**Tool Version:** 1.0.0
**Supports:** preOBD v0.5.0-alpha+
