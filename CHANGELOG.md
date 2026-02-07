# Changelog

All notable changes to preOBD will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.7.0-beta] - 2026-02-07

### Added
- Per-input output routing to selectively route inputs to CAN, RealDash, Serial, or SD
- Pin allocation status reporting via SYSTEM PINS command
- CAN sensor import from external ECUs (OBD-II, J1939, custom protocols)
- CAN controller hardware abstraction layer (FlexCAN, TWAI, MCP2515)
- Hybrid CAN controller mode for mixing native and SPI controllers
- Per-bus CAN baud rate configuration
- CAN input listen-only mode for passive bus monitoring
- Platform-specific watchdog HAL drivers
- RGB LED status indicator with priority system and configurable effects

### Changed
- Watchdog and CAN bus refactored to use HAL with platform-specific drivers
- Teensy 3.6 migrated from external MCP2515 to native FlexCAN

### Fixed
- Shared CAN bus validation when input/output use the same physical bus

## [0.6.5-beta] - 2025-01-27

### Added
- Enhanced logging system with levels and subsystem tags
- Vehicle speed sensing with hall effect sensor support
- OBD-II request/response for ELM327/Torque compatibility
- Hierarchical help system organized by category
- Relay output control with threshold-based automation and hysteresis
- Test mode serial command
- ESP32-S3 BLE transport support
- Bus configuration system with pin registry
- Modular category-based sensor library

### Changed
- CLI library migrated from microrl to embedded-cli
- CAN output supports both broadcast and request/response modes
- Serial output migrated to message bus API

### Fixed
- Static build compatibility for pinTypeRequirement field
- SD card initialization centralized to prevent reboot on save
- HELP display corruption on AVR platform
- Various pin registry and serial port configuration fixes

## [0.6.0-beta] - 2025-12-31

### Added
- Transport abstraction layer with MessageRouter for multi-transport communication
- ESP32 Bluetooth Classic and UART Bluetooth module support
- ESP32 native CAN (TWAI) implementation

### Changed
- Serial commands and output routing migrated to MessageRouter API

## [0.5.0-alpha] - 2025-12-22

### Added
- Beta equation thermistor calibration
- Runtime custom calibration for pressure and thermistor sensors
- JSON configuration system with SD card backup/restore
- Registry-based architecture with hash-based sensor/application lookups
- String-based unit system replacing hardcoded enums
- Runtime display, output, and system configuration commands
- RPM pulley ratio calibration

### Changed
- Alarm system rewritten with AlarmContext state machine
- EEPROM storage uses hashes instead of indices for portability
- LCD display driven by registry pattern matching

### Removed
- Legacy enum-based sensor/application system
- Deprecated example configs and setup scripts

### Fixed
- Compilation warnings on Arduino Mega
- Button press false detection with proper INPUT_PULLUP
- PROGMEM string handling in INFO command
- Alarm threshold min/max validation

## [0.4.0-alpha] - 2025-12-03

### Added
- First public release
- 17+ sensor types with 30+ pre-calibrated configurations
- Multiple outputs: LCD, CAN bus (OBD-II), serial CSV, SD logging, RealDash
- Runtime serial configuration with EEPROM persistence
- Static configuration mode for memory-constrained boards
- Test mode with predefined scenarios

### Fixed
- Watchdog boot loop in CONFIG mode
- EEPROM checksum calculation order
- LCD garbage display on checksum mismatch

## [0.3.4-alpha] - 2025-11-26
## [0.3.2-alpha] - 2025-11-15
## [0.3.0-alpha] - 2025-11-05
## [0.2.0-alpha] - 2025-10-28

Previous development releases. See git history for details.

---

[Unreleased]: https://github.com/preobd/preOBD/compare/v0.7.0-beta...HEAD
[0.7.0-beta]: https://github.com/preobd/preOBD/compare/v0.6.5-beta...v0.7.0-beta
[0.6.5-beta]: https://github.com/preobd/preOBD/compare/v0.6.0-beta...v0.6.5-beta
[0.6.0-beta]: https://github.com/preobd/preOBD/compare/v0.5.0-alpha...v0.6.0-beta
[0.5.0-alpha]: https://github.com/preobd/preOBD/compare/v0.4.0-alpha...v0.5.0-alpha
[0.4.0-alpha]: https://github.com/preobd/preOBD/releases/tag/v0.4.0-alpha
