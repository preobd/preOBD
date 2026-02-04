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

### CAN Controller Abstraction

The CAN bus system uses a Hardware Abstraction Layer (HAL) to support multiple controller types:

**Controller Types:**
- **FlexCAN** (Teensy 3.6/4.0/4.1 native) - 2-3 CAN buses, high performance
- **TWAI** (ESP32/ESP32-S3 native) - 1 CAN bus, requires external transceiver
- **SPI** (External SPI-based controllers) - 1-2 buses, works on any platform with SPI
  - Currently supported: MCP2515
  - Future: MCP25625 (integrated transceiver), SJA1000
- **bxCAN** (STM32 native, planned) - 1-2 buses depending on variant

**Platform Capabilities:**
Controller selection happens at compile time via [src/hal/platform_caps.h](src/hal/platform_caps.h):
- `PLATFORM_CAN_CONTROLLER` - String identifier ("FlexCAN", "TWAI", "SPI", "bxCAN")
- `PLATFORM_HAS_NATIVE_CAN` - Boolean flag (1 = integrated peripheral, 0 = external)
- `PLATFORM_NEEDS_SPI_CAN` - Boolean flag (1 = requires SPI CAN pins in config.h)
- `PLATFORM_SUPPORTS_HYBRID` - Boolean flag (1 = hybrid mode enabled)

**Build Flags:**
- `USE_FLEXCAN_NATIVE` - Enables Teensy FlexCAN support (set in platformio.ini)
- ESP32: Automatically detected via `ESP32` processor define
- STM32: Auto-detect via `STM32F4xx` / `STM32F1xx` defines (future)
- SPI CAN: Default fallback for platforms without native CAN (auto-selected)

**Hybrid Mode:**
Hybrid mode allows mixing controller types on different buses (e.g., ESP32 TWAI + MCP2515):
```ini
[env:esp32s3_hybrid]
build_flags =
    -D ENABLE_CAN_HYBRID
    -D CAN_BUS_0_TYPE=CanControllerType::TWAI
    -D CAN_BUS_1_TYPE=CanControllerType::MCP2515
```

Pin configuration for SPI CAN controllers is in [src/config.h](src/config.h), not platformio.ini.

**HAL Interface** ([src/hal/hal_can.h](src/hal/hal_can.h)):
```cpp
namespace hal { namespace can {
    bool begin(uint32_t baudrate, uint8_t bus = 0);
    bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus);
    bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus);
    void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus);
}}
```

All CAN operations (input, output, bus manager) use this unified API regardless of controller type.

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

**Create a feature branch only when starting NEW work.** When planning work:

1. Create a branch from `main` with a descriptive name: `feature/`, `fix/`, `refactor/`, etc.
2. All implementation work happens on the feature branch
3. Merge back to `main` via pull request

**DO NOT create a new branch if:**
- Already working on an existing feature/fix branch
- Continuing work on the current branch
- Making additional commits to ongoing work

Example:
```bash
git checkout -b feature/hal-abstraction
git checkout -b fix/can-timeout-handling
git checkout -b refactor/sensor-registry
```

### Implementation Plans Must Include

When creating implementation plans for NEW work, always include these phases:

1. **Branch Creation** - Create feature branch ONLY if starting new work from `main`
2. **Implementation** - The actual code changes
3. **Build Verification** - Run `pio run` for affected platforms
4. **Documentation Updates** - Update relevant documentation:
   - `README.md` - If user-facing features change
   - Code comments - For non-obvious implementation details
5. **Commit/PR** - Commit with descriptive message, create PR if requested

### CHANGELOG.md Policy

**Update CHANGELOG.md only once per branch** with a short summary of the branch's purpose before merging to `main`. DO NOT update the changelog for every individual commit.

Follow [Keep a Changelog](https://keepachangelog.com/) format:
- **Added** - New features
- **Changed** - Changes to existing functionality
- **Fixed** - Bug fixes
- **Removed** - Removed features
- **Deprecated** - Features to be removed in future
