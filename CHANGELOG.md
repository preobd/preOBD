# Changelog

All notable changes to preOBD will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `SET <pin> DISPLAY ENABLE/DISABLE` serial command to toggle per-input LCD display flag (#142)

### Changed
- `CalibrationOverride` union members `rpm`, `speed`, and `can` now use `RPMCalibration`, `SpeedCalibration`, and `CANSensorCalibration` typedefs directly, eliminating duplicate field definitions and padding drift risk; EEPROM version bumped to 5 to invalidate stale AVR layouts (#175, #72)
- Restructured `command_table.cpp` (3,453 → 1,419 lines) around a generic subcommand dispatch primitive; `cmd_set`, `cmd_bus`, `cmd_system` split into focused per-command files with PROGMEM-resident dispatch tables; adds `SELFTEST` command for runtime dispatch-table validation (#118, #186, #191)

### Fixed
- Pre-existing bugs in `cmd_set` surfaced during the command_table refactor: RPM/SPEED arity check no longer accepts arbitrary excess parameters; `SET CAN <pid>` rejects non-numeric arguments instead of allocating phantom CAN sensors; BIAS bounds inlined to avoid file-scope macro leak (#187, #188, #190)
- Missing trailing newline added to `src/lib/sensor_types.h` (#178)
- `allocateInputSlot` now zeroes the slot before use, eliminating stale flags, calibration data, and alarm thresholds from previously-cleared inputs (#180)
- Input slots are now freed on partial-failure during configuration (e.g. `setInputSensor` failure after slot allocation), preventing orphaned slots (#179)
- SPI and I2C buses now default to NONE and only initialize when explicitly selected via `BUS SPI/I2C <N>`; fixes spurious pin reservation conflicts at boot (e.g. RGB LED blocked by unconfigured SPI0) (#168)
- CAN HAL `write()` clamps `len` to 8 before `memcpy` to prevent buffer overflow in frame construction (#112)
- RPM ISR reads on AVR are now atomic, and timeout check uses `micros()` consistently with the ISR (#111, #116)
- CAN-imported sensors now support a configurable per-sensor stale-data timeout via `SET <pin> CAN_TIMEOUT <ms>` (100–30000ms), replacing a hardcoded 2000ms for all sensors (#173)

## [0.8.0-beta] - 2026-04-30

### Added
- Web Bluetooth webapp for browser-based device configuration and diagnostics, installable as a PWA via GitHub Pages
- BLE GATT profile specification (Tier 1 text command service + Tier 2 binary protocol spec) with shared UUID definitions for firmware and clients
- HM-10 BLE module support in webapp with auto-detection alongside preOBD GATT and Nordic UART services
- `AT <port> <command>` serial command for sending raw AT commands to attached BLE/serial modules
- 8 new classic-car monitoring application presets
- Disconnect detection for linear sensors: out-of-range voltages return NAN, optional pin pull-up for low-Z signal-conditioned sensors, and per-input `divider_ratio` (set via `SET <pin> DIVIDER <ratio>`) for running 5V sensors on 3.3V ADCs through a voltage divider (#155, #157)

### Changed
- Board profiles (`src/profiles/`) now own all feature flags and hardware pin assignments; `config.h` is application constants only; system pin fields removed from SystemConfig/EEPROM; JSON config decoupled from SD logging via new `SUPPORTS_SD` hardware capability flag (#163, #165, #166)
- Watchdog kick batched across `WatchdogKickingPrint` writes and kick interval raised; watchdog now fed during JSON serialization to survive slow BLE UART bridges
- Documentation accuracy pass: fixed broken intra-doc links, refreshed README and docs index to cover BLE, ESP32-S3, and recently-added guides

### Fixed
- `esp32s3_hybrid` and `teensy41_hybrid` build envs now compile cleanly (#146)
- Correctness and safety fixes in sensor interpolation and resistance calculation (#110, #113, #114)
- SystemConfig persistence on platforms with small EEPROM (Teensy 4.0): config address is now derived per-platform and decoupled from MAX_INPUTS

## [0.7.6-beta] - 2026-04-24

### Changed
- Replaced scattered per-platform `#ifdef` sizing constants with per-env profile headers (`src/profiles/`)

### Removed
- Dropped `USE_STATIC_CONFIG` build flag, `uno_static` env, and `tools/configure.py` (#148)

## [0.7.5-beta] - 2026-04-22

### Added
- Added Smiths, Stewart-Warner, AC Delco, and Bosch NTC sensors to the sensor library

## [0.7.4-beta] - 2026-04-16

### Added
- JSON IMPORT command for streaming bulk sensor configuration over serial CLI
- JSON export of firmware static catalogs via `LIST ... JSON` and `SYSTEM DUMP REGISTRY JSON`

### Fixed
- Control-plane responses now return to the transport that sent the command; unsolicited messages (alarms, etc.) still multi-cast to all configured transports

## [0.7.3-beta] - 2026-04-13

### Added
- ELM327 AT command emulator output module for direct BLE OBD-II app connectivity without hardware adapter
- Added Jeep 4.0L sensor family (gauge temp sender, Renix ECU CTS, oil pressure sender)
- Extended OBD-II PID discovery chain (0x00→0x20→0x40…) so apps find all configured PIDs automatically
- TRANSPORT command SECONDARY keyword for simultaneous multi-port control (e.g. USB + Bluetooth)

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

[Unreleased]: https://github.com/preobd/preOBD/compare/v0.8.0-beta...HEAD
[0.8.0-beta]: https://github.com/preobd/preOBD/compare/v0.7.6-beta...v0.8.0-beta
[0.7.6-beta]: https://github.com/preobd/preOBD/compare/v0.7.5-beta...v0.7.6-beta
[0.7.5-beta]: https://github.com/preobd/preOBD/compare/v0.7.4-beta...v0.7.5-beta
[0.7.4-beta]: https://github.com/preobd/preOBD/compare/v0.7.3-beta...v0.7.4-beta
[0.7.3-beta]: https://github.com/preobd/preOBD/compare/v0.7.0-beta...v0.7.3-beta
[0.7.0-beta]: https://github.com/preobd/preOBD/compare/v0.6.5-beta...v0.7.0-beta
[0.6.5-beta]: https://github.com/preobd/preOBD/compare/v0.6.0-beta...v0.6.5-beta
[0.6.0-beta]: https://github.com/preobd/preOBD/compare/v0.5.0-alpha...v0.6.0-beta
[0.5.0-alpha]: https://github.com/preobd/preOBD/compare/v0.4.0-alpha...v0.5.0-alpha
[0.4.0-alpha]: https://github.com/preobd/preOBD/releases/tag/v0.4.0-alpha
