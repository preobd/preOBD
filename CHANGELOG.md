# Changelog

All notable changes to openEMS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Runtime Output Configuration**: Enable/disable output modules (CAN, RealDash, Serial CSV, SD logging) via serial commands
- **Runtime Display Configuration**: Switch display type (LCD/OLED/None), I2C address, and default units without recompiling
- **Runtime System Configuration**: Configure VDO bias resistor, sea level pressure, and timing intervals via serial
- **Combined Sensor Command**: New `SET <pin> <app> <sensor>` syntax for faster configuration
- **Enhanced DUMP Command**: Now shows complete system state including outputs, display, and system config
- New serial commands:
  - `OUTPUT LIST`, `OUTPUT <name> ENABLE/DISABLE/INTERVAL`
  - `DISPLAY STATUS`, `DISPLAY TYPE`, `DISPLAY LCD ADDRESS`, `DISPLAY UNITS`
  - `SYSTEM STATUS`, `SYSTEM VDO_BIAS`, `SYSTEM SEA_LEVEL`, `SYSTEM INTERVAL`
- Comprehensive serial command reference documentation
- SystemConfig structure with EEPROM persistence (48 bytes)

### Changed
- Output modules now always compiled, controlled by runtime flags instead of `#ifdef`
- SAVE/LOAD/RESET commands now persist both input and system configuration
- EEPROM layout expanded: SystemConfig at address 0x03F0 (48 bytes)
- EEPROM version incremented to 2
- Output intervals now configurable at runtime (10-60000ms)
- Compile-time `#define ENABLE_*` flags now serve as factory defaults only

### Fixed
- Removed duplicate `OutputType` enum, now uses centralized `OutputID`
- Avoided duplicate sensor initialization in combined SET command
- Used correct `Units` enum type in SystemConfig for display defaults

### Backward Compatibility
- All existing serial commands work unchanged
- Legacy `SET <pin> APPLICATION/SENSOR` syntax still fully supported
- Old EEPROM configs (version 1) will trigger auto-reset to version 2 defaults

## [0.4.0-alpha] - 2025-12-03

### Added
- First public release
- Support for 17+ sensor types with 30+ pre-calibrated configurations
- Multiple output modes: LCD display (20x4), CAN bus (OBDII PIDs), serial CSV, SD card logging, RealDash protocol
- Runtime configuration via serial commands with EEPROM storage
- Compile-time configuration mode for memory-constrained boards
- CONFIG/RUN mode separation for safe configuration changes
- Platform-abstracted watchdog timer (AVR, Teensy, ESP32)
- EEPROM checksum validation
- Configuration validation (pin conflicts, threshold sanity checks)
- Comprehensive documentation structure
- Per-sensor read timing optimization
- Time-sliced main loop architecture
- Custom LCD icons for sensor types
- Test mode with predefined scenarios

### Changed
- Refactored output handling with data-driven architecture
- Improved sensor initialization with per-sensor init functions

### Fixed
- Watchdog timer boot loop in CONFIG mode
- EEPROM checksum calculation order
- Garbage display on checksum mismatch
- LCD display updates in CONFIG mode
- RELOAD command freeze
- MODE_BUTTON false detection (floating pin)

## [0.3.4-alpha] - 2025-11-26
## [0.3.2-alpha] - 2025-11-15
## [0.3.0-alpha] - 2025-11-05
## [0.2.0-alpha] - 2025-10-28

Previous development releases. See git history for details.

---

[Unreleased]: https://github.com/preobd/openEMS/compare/v0.4.0-alpha...HEAD
[0.4.0-alpha]: https://github.com/preobd/openEMS/releases/tag/v0.4.0-alpha