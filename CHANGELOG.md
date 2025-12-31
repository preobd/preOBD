# Changelog

All notable changes to openEMS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Transport abstraction layer** - Unified multi-transport architecture for serial communications
- **MessageRouter system** - Centralized message routing with priority-based transport selection
- **ESP32 Bluetooth Classic support** - Native Bluetooth Serial transport for ESP32 platforms
- **UART Bluetooth module support** - HC-05/HM-10 Bluetooth module support for Teensy and AVR platforms
- **ESP32 native CAN support** - TWAI (Two-Wire Automotive Interface) implementation for ESP32
- **Multi-transport command routing** - Serial commands accepted from any registered transport (USB, UART, Bluetooth)
- **Multi-transport output routing** - RealDash and serial output to all connected transports simultaneously
- **TransportInterface API** - Abstract interface for adding new communication transports

### Changed
- **Serial command input** - Now uses MessageRouter to poll all transports instead of USB-only
- **RealDash output** - Broadcasts to all connected transports (USB, Bluetooth, Serial)
- **Serial output** - Broadcasts sensor data to all connected transports
- **Message routing** - Migrated from direct Serial calls to msg.control API with router

### Technical Details
- ESP32 CAN uses GPIO21 (TX) and GPIO22 (RX) with 500 kbps for OBDII compatibility
- ESP32 CAN requires external transceiver (MCP2551, TJA1050, SN65HVD230)
- ESP32 Bluetooth device name: "openEMS"
- Transport system supports USB Serial, Serial1, Serial2, and ESP32 Bluetooth
- All transports operate at 115200 baud (except Serial2 defaults to 9600 for HC-05/HM-10)

## [0.5.0-alpha] - 2025-12-22

### Added
- **Beta (β) equation thermistor calibration support** - Alternative calibration method for thermistors
- **Custom calibration support** - Runtime custom calibration commands for pressure and thermistor sensors
- **JSON configuration system** - Unified JSON schema support for custom calibrations and configuration import/export
- **JSON config backup/restore** - SD card support for configuration backup and restore
- **Registry-based architecture** - Name-based sensor and application registry with hash-based lookups
- **String-based unit system** - Eliminated hardcoded unit enums in favor of flexible string-based registry
- **Generic linear sensors** - Support for generic linear temperature and pressure sensors
- **AEM 30-2130-150 pressure sensor** - New pre-calibrated sensor support
- **BME280 dynamic initialization** - Converted to dynamic sensor initialization system
- **Pin type validation** - Enhanced pin validation for sensor compatibility with type requirements
- **Runtime display controls** - DISPLAY ENABLE/DISABLE commands with backlight control and button toggle
- **Runtime output configuration** - Serial commands for configuring outputs at runtime
- **Runtime system configuration** - SYSTEM configuration commands for global settings
- **Combined sensor config command** - Single command to configure multiple sensor parameters
- **Alarm capability validation** - Sensor capability validation for alarm range configuration
- **Python-based static configurator** - Tool for generating static configuration files with validation
- **Multiple I2C sensors** - Support for multiple I2C sensors with unique virtual pin assignments
- **Pulley ratio RPM calibration** - Added pulley ratio and calibration multiplier to RPM sensing
- **SET RPM command** - Runtime RPM configuration with optional calibration multiplier
- **Platform information display** - Enhanced system status output with platform details
- **JSON schema versioning** - Schema migration guide for configuration compatibility
- **I2C keyword** - Added 'I2C' keyword for cleaner I2C sensor configuration
- **RealDash setup guide** - XML channel description for RealDash integration

### Changed
- **Removed USE_INPUT_BASED_ARCHITECTURE define** - Cleanup of deprecated configuration option
- **Refactored alarm system** - Complete rewrite with AlarmContext, alarm_logic module, and output_alarm module
- **Registry-driven display** - LCD display now uses pattern matching and registry lookups
- **Data-driven unit conversion** - All unit conversions now use registry system
- **Serial parser simplification** - Simplified using registry-based lookups
- **EEPROM storage optimization** - Uses hashes instead of indices for smaller footprint
- **Input struct modernization** - Migrated to use registry indices instead of enums
- **Static configuration support** - Generated library files for memory-constrained deployments
- **SYSTEM_CONFIG_VERSION incremented to 3** - Added OUTPUT_ALARM configuration
- **Improved LCD display logic** - Better readability and configuration options
- **Simplified quick start checklist** - Improved comments and documentation in config.h
- **Renamed DEFAULT_BIAS_RESISTOR** - From VDO_BIAS_RESISTOR for clarity
- **Updated sensor value ranges** - Corrections for MAX6675 and VDO sensors
- **Enhanced initialization messages** - Better LCD and button handler feedback
- **I2C handling improvements** - Better BME280 sensor support and command syntax
- **Checksum calculation updates** - Read from EEPROM with proper null termination
- **Button press handling** - Improved long press detection logic
- **Display name length increased to 32** - More descriptive sensor names
- **Application preset structure** - Added abbreviation and warmup/persistence fields
- **Updated firmware version display** - Centralized version management

### Removed
- **Deprecated example configuration files** - Removed with updated documentation paths
- **Setup script** - Removed deprecated setup utilities
- **Legacy input definitions** - Cleaned up unused static configuration definitions
- **DisplayUnits enum** - Replaced with string-based registry
- **Application and Sensor enums** - Phase 11 registry refactor complete
- **Legacy conversion wrapper functions** - Streamlined registry architecture
- **POPULATE_INPUT macro system** - Replaced with simpler registry-based approach
- **BME280 sensor support comments** - Cleaned up from config.h
- **Duplicate SD library dependency** - Removed from EEPROM libraries
- **SYSTEM VDO_BIAS command** - Dead code removal

### Fixed
- **Compilation warnings for Arduino Mega** - Platform compatibility improvements
- **Button press false detection** - Proper INPUT_PULLUP configuration
- **MODE_BUTTON pin mode** - Correct functionality with INPUT_PULLUP
- **DUMP JSON command parsing** - Corrected parsing logic
- **SD card handling** - Improved JSON import/export and configuration backup
- **Display manager static config** - Added support for static configuration mode
- **Include paths for generated files** - Updated for generated library files
- **Sensor initialization logic** - Prevented premature sensorIndex assignment
- **PROGMEM string handling** - Corrected in INFO command and icon selection
- **DISPLAY_NAME command** - Fixed documentation for full name length
- **Battery abbreviation strings** - Consistency improvements
- **Unit string retrieval** - Proper casting for EEPROM compatibility
- **Pressure unit handling** - Fixed unknown pressure unit in serial output
- **Duplicate sensor initialization** - Avoided in combined SET command and runtime changes
- **OutputType enum duplication** - Use centralized OutputID
- **Sensor/application type mismatches** - Proper rejection instead of warnings
- **Alarm threshold validation** - Ensured min < max validation

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

[Unreleased]: https://github.com/preobd/openEMS/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/preobd/openEMS/compare/v0.4.0-alpha...v0.5.0
[0.4.0-alpha]: https://github.com/preobd/openEMS/releases/tag/v0.4.0-alpha