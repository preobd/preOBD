# Changelog

All notable changes to openEMS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- None

### Changed
- None

### Fixed
- None

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