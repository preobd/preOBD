# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

openEMS is an embedded engine monitoring system for classic cars without modern ECUs. It monitors temperature, pressure, voltage, RPM, and speed sensors, outputting data via LCD, CAN bus (OBD-II compatible), serial, and SD logging. Written in C++ for Arduino/PlatformIO.

## Build Commands

```bash
pio run -e teensy41              # Build for Teensy 4.1 (recommended)
pio run -e teensy41 -t upload    # Build and upload
pio device monitor               # Serial monitor (115200 baud)
pio run -e debug                 # Build with debug symbols (-Og -g)
pio run -e mega2560              # Build for Arduino Mega
pio run -e uno_static            # Build for Arduino Uno (static config only)
```

Build environments: `teensy41`, `teensy40`, `teensy36`, `esp32s3dev`, `mega2560`, `uno_static`, `debug`

## Build Configuration

Feature flags are defined in `platformio.ini` (not in source code):
- `ENABLE_CAN`, `ENABLE_LCD`, `ENABLE_SD_LOGGING`, `ENABLE_SERIAL_OUTPUT`
- `ENABLE_ALARMS`, `ENABLE_RELAY_OUTPUT`, `ENABLE_BME280`, `ENABLE_TEST_MODE`
- `USE_STATIC_CONFIG` - Compile-time config for memory-constrained boards (Uno)

Hardware pins and timing are configured in `src/config.h`.

## Architecture

### Input-Based Design

The core abstraction is the **Input** - a configured sensor on a physical pin:

```
Physical Pin → Input → Output Modules
                │
                ├── Application (what you measure: CHT, OIL_PRESSURE, COOLANT_TEMP)
                ├── Sensor (hardware: MAX6675, VDO_120C, MPX4250)
                └── Calibration (conversion to engineering units)
```

**Input struct** (`src/inputs/input.h`): ~100 bytes each, stored in RAM. Calibration data points to PROGMEM.

### Key Directories

- `src/inputs/` - Input management, sensor reading, serial commands, alarm logic
- `src/lib/sensor_library/` - Sensor definitions organized by type (thermocouples, thermistors, pressure, etc.)
- `src/outputs/` - Output modules (CAN/OBD-II, serial CSV, SD logging, alarms, relays)
- `src/displays/` - LCD display driver
- `src/test/` - Test mode (sensor simulation without hardware)

### Important Files

- `src/inputs/command_table.cpp` - Serial command processor (large file, ~107KB)
- `src/inputs/sensor_read.cpp` - Central sensor reading dispatcher
- `src/lib/bus_manager.cpp` - CAN bus management (dual-bus architecture)
- `src/lib/sensor_types.h` - Calibration structs (Steinhart-Hart, linear, polynomial)
- `src/lib/system_config.h` - System-wide EEPROM config struct

### Registry System

Hash-based lookup system for string→sensor/application mapping. Enables EEPROM portability across firmware versions. Auto-generated files:
- `tools/generate_registry_enums.py` - Generates registry enums
- `tools/validate_registries.py` - Validates registry consistency

### Memory Considerations

- All sensor calibration data stored in PROGMEM (flash), not RAM
- Arduino Uno: 2KB RAM limits to ~10 inputs, requires `USE_STATIC_CONFIG`
- Teensy 4.1: 8MB flash, can support 20+ inputs
- Platform voltage differences: 3.3V (Teensy, ESP32) vs 5V (Mega, Uno)

### Configuration Modes

1. **Runtime Mode** (default): Configured via serial commands, persisted to EEPROM
2. **Static Mode** (`USE_STATIC_CONFIG`): Compile-time only, no EEPROM overhead

## Test Mode

Not unit tests - embedded sensor simulation for testing outputs without hardware:
- Activated via `ENABLE_TEST_MODE` flag + `TEST_MODE_TRIGGER_PIN`
- 5 pre-defined scenarios in `src/test/test_scenarios.h`
- Uses function pointer substitution (non-invasive)
- Adds ~4KB flash when enabled

## Tools

- `tools/configure.py` - Interactive static config generator
- `tools/generate_registry_enums.py` - Auto-generates registry enums
- `tools/validate_registries.py` - Validates sensor/application registries
- `scripts/version_inject.py` - PlatformIO pre-build hook (injects build number, git hash)

## CAN Bus

Dual-bus architecture with separate input/output buses:
- Output: OBD-II request/response (ELM327/Torque compatible) + broadcast mode (RealDash)
- Input: CAN sensor import (OBD-II, J1939, custom protocols)
- Native FlexCAN on Teensy, MCP2515 SPI on Mega/Uno, TWAI on ESP32

## Development Workflow

### Branching Strategy

**Always create a feature branch before implementing changes.** When planning work:

1. Create a branch from `main` with a descriptive name: `feature/`, `fix/`, `refactor/`, etc.
2. All implementation work happens on the feature branch
3. Merge back to `main` via pull request

Example:
```bash
git checkout -b feature/hal-abstraction
git checkout -b fix/can-timeout-handling
git checkout -b refactor/sensor-registry
```

### Implementation Plans Must Include

When creating implementation plans, always include these phases:

1. **Branch Creation** - Create feature branch before any code changes
2. **Implementation** - The actual code changes
3. **Build Verification** - Run `pio run` for affected platforms
4. **Documentation Updates** - Update relevant documentation:
   - `CHANGELOG.md` - Add entry under appropriate section (Added/Changed/Fixed)
   - `README.md` - If user-facing features change
   - Code comments - For non-obvious implementation details
5. **Commit/PR** - Commit with descriptive message, create PR if requested

### CHANGELOG.md Format

Follow [Keep a Changelog](https://keepachangelog.com/) format:
- **Added** - New features
- **Changed** - Changes to existing functionality
- **Fixed** - Bug fixes
- **Removed** - Removed features
- **Deprecated** - Features to be removed in future
