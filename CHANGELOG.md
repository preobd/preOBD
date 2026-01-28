# Changelog

All notable changes to openEMS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.6.5-beta] - 2025-01-27

### Added
- **Enhanced logging system** - Comprehensive logging with levels (DEBUG, INFO, WARNING, ERROR) and subsystem tags
- **LOG command** - Runtime log level filtering and configuration via serial commands
- **LOG EEPROM persistence** - Log filter configuration saved to EEPROM
- **Vehicle speed sensing** - Hall effect speed sensor support with configurable calibration
- **SET SPEED command** - Configure speed sensor parameters (pulses per revolution, tire circumference)
- **Speed display units** - Configurable speed units (MPH/KPH) for display and output
- **OBD-II request/response support** - ELM327 Bluetooth adapters can now query sensor data
- **Mode 01 PID 00 implementation** - Supported PIDs bitmap automatically generated from active inputs
- **Hybrid CAN mode** - Simultaneous broadcast (RealDash) and request/response (Torque/scanners)
- **Functional and physical addressing** - Responds to both 0x7DF (broadcast) and 0x7E0 (ECU 0) requests
- **Automatic PID lookup table** - Built from active inputs at startup for O(1) PID queries
- **ISO 14229 negative responses** - Proper error handling for unsupported modes and PIDs
- **Hierarchical help system** - Organized help by category (LIST, SET, CALIBRATION, CONTROL, OUTPUT, RELAY, DISPLAY, TRANSPORT, SYSTEM, CONFIG)
- **HELP <category> command** - Detailed help for specific command categories
- **HELP QUICK command** - Compact command reference for experienced users
- **Help category overview** - HELP command shows all available categories with descriptions
- **Relay output control** - Automatic threshold-based relay outputs with hysteresis
- **RELAY commands** - Serial commands for configuring relay outputs (PIN, INPUT, THRESHOLD, MODE)
- **Relay control modes** - AUTO_HIGH, AUTO_LOW, and manual override (ON/OFF)
- **Dual relay support** - 2 independent relay channels with separate configurations
- **Relay EEPROM persistence** - Relay configurations saved to EEPROM
- **TEST command** - Serial command for test mode control
- **ESP32-S3 BLE support** - Bluetooth Low Energy support for ESP32-S3 alongside Bluetooth Classic for ESP32
- **Bus configuration system** - Pin registry and configurable bus infrastructure (CAN, Serial, I2C)
- **BUS commands** - Serial commands for bus configuration and status
- **BUS SERIAL command** - Configure serial port parameters (baud rate, enable/disable)
- **Serial port configuration** - Runtime control of serial ports with JSON export
- **Modular sensor library** - Category-based sensor library organization (Environmental, Pressure, Temperature, Speed)
- **MPX5700AP pressure sensor** - Support for Freescale/NXP MPX5700AP absolute pressure sensor
- **Pressure lookup tables** - TABLE command for non-linear pressure sensor calibration
- **Two-layer sensor selection** - Hierarchical sensor selection by category and type
- **3-layer versioning model** - Major.minor.patch versioning for OTA update support
- **Pin registry validation** - Enhanced pin conflict detection and validation
- **JSON pin registry export** - Consolidated pin configuration in JSON exports
- **Message bus API** - Tagged logging API for consistent message formatting

### Changed
- **CLI library** - Migrated from microrl to embedded-cli library
- **CAN output** - Now supports both broadcast and on-demand request/response modes
- **updateCAN()** - Processes incoming OBD-II requests on all platforms (FlexCAN, TWAI, MCP2515)
- **initCAN()** - Configures RX filters for 0x7DF/0x7E0 and builds PID lookup table
- **HELP command** - Now shows category overview instead of flat 104-line command list
- **Help organization** - Commands grouped by function for better discoverability
- **Serial output** - Migrated to message bus API with tagged logging
- **Sensor library organization** - Restructured into modular category files
- **NTC sensor naming** - Updated to generic naming convention
- **LOOKUP command** - Renamed to TABLE for clarity
- **Bus configuration model** - Simplified to "pick one" CAN bus model
- **Serial command arguments** - Now case-insensitive
- **BOOST_PRESSURE abbreviation** - Changed from BOOST to BST
- **SD library** - Migrated from SdFat to Arduino SD library for SDIO support
- **Factory reset** - Now properly saves system configuration
- **Sensor category naming** - Renamed I2C category to Environmental

### Fixed
- **Static builds** - Excluded pinTypeRequirement field to enable static configuration builds
- **Include guards** - Added missing include guards in multiple modules
- **SD logging preprocessor** - Corrected preprocessor directives
- **JSON config import** - Resolved validation issues with debug logging
- **Alarm validation** - Allow both min and max alarm values to be 0 for disabled alarms
- **SD card initialization** - Centralized to prevent reboot on save
- **SD card file paths** - Corrected path generation for configuration backup
- **Watchdog timeout** - Extended timeout for SD initialization
- **Duplicate pin registration** - Removed duplicate buzzer pin registration
- **Serial port baud rate** - Use saved configuration value when enabling ports
- **Pin registry integration** - Integrated validation with input manager
- **HELP display** - Fixed corruption on AVR (Mega2560) platform
- **HELP subcommand parsing** - Corrected parsing for HELP CONFIG, HELP QUICK, etc.
- **JSON relay output** - Added missing "relay" entry to outputNames arrays
- **Input manager include** - Added missing input_manager.h include
- **OBD-II unused variable** - Removed unused length variable in output_can.cpp
- **Teensy macro conflicts** - Renamed PinUsageType enum values
- **HELP display on Teensy/ESP32** - Platform-specific fixes
- **SYSTEM_CONFIG_VERSION** - Rebaselined for configuration compatibility
- **Serial status display** - Removed extra space in output
- **Transport names** - Extended names in transport status display

### Removed
- **ESP32 environment** - Removed to focus on ESP32-S3
- **VDO-specific documentation** - Replaced with generic sensor guides

### Documentation
- **OBD2_SCANNER_GUIDE.md** - Complete setup guide for ELM327/Torque
- **OBD2_PID_REFERENCE.md** - Comprehensive PID catalog with examples
- **CAN_TRANSCEIVER_GUIDE.md** - Hardware guide for CAN transceivers
- **HALL_SPEED_GUIDE.md** - Generic hall effect speed sensor guide
- **Relay control documentation** - Comprehensive relay control guide
- **BUS command documentation** - Documentation for bus configuration
- **Modular sensor library docs** - Updated for category-based structure
- **Thermistor guide** - Generic thermistor calibration guide (replaced VDO-specific)
- Updated README.md to clarify OBD-II request/response capability
- Updated docs/README.md with OBD-II scanner section and output guides
- Added Output Guides section to documentation index
- Updated multi-function button description

## [0.6.0-beta] - 2025-12-31

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

[Unreleased]: https://github.com/preobd/openEMS/compare/v0.6.5-beta...HEAD
[0.6.5-beta]: https://github.com/preobd/openEMS/compare/v0.6.0-beta...v0.6.5-beta
[0.6.0-beta]: https://github.com/preobd/openEMS/compare/v0.5.0-alpha...v0.6.0-beta
[0.5.0-alpha]: https://github.com/preobd/openEMS/compare/v0.4.0-alpha...v0.5.0-alpha
[0.4.0-alpha]: https://github.com/preobd/openEMS/releases/tag/v0.4.0-alpha