# Directory Setup Guide - preOBD

**Understanding the project structure and where everything lives**

---

## Complete Directory Structure

```
preOBD/
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project overview, quick start
├── LICENSE                     # MIT + NonCommercial License
├── DISCLAIMER.md               # Safety and warranty disclaimer
│
├── src/                        # All source code
│   ├── main.cpp               # Main program loop
│   ├── config.h               # ⭐ APPLICATION CONSTANTS (timing, thresholds, units)
│   ├── version.h              # Firmware version
│   │
│   ├── profiles/              # ⭐ BOARD DEFINITIONS (feature flags + pin assignments)
│   │   ├── profile_teensy41.h       # Teensy 4.1 (recommended)
│   │   ├── profile_teensy40.h       # Teensy 4.0
│   │   ├── profile_teensy36.h       # Teensy 3.6
│   │   ├── profile_mega2560.h       # Arduino Mega 2560
│   │   ├── profile_esp32s3.h        # ESP32-S3
│   │   ├── profile_teensy41_hybrid.h  # Teensy 4.1 + SPI CAN hybrid
│   │   └── profile_esp32s3_hybrid.h   # ESP32-S3 TWAI + MCP2515 hybrid
│   │
│   ├── hal/                   # Hardware abstraction layer
│   │   ├── hal_can.h          # CAN bus HAL interface
│   │   ├── hal_watchdog.h     # Watchdog HAL interface
│   │   ├── platform_caps.h    # Platform capability flags
│   │   └── platforms/         # Platform-specific HAL implementations
│   │
│   ├── inputs/                # Input and sensor management
│   │   ├── input.h            # Input structure definitions
│   │   ├── input_manager.cpp  # Input configuration management
│   │   ├── input_manager.h    # Input manager exports
│   │   ├── input_can.cpp      # CAN sensor input handling
│   │   ├── sensor_read.cpp    # Central sensor reading dispatcher
│   │   ├── sensors/           # Modular sensor reading implementations
│   │   │   ├── thermistors/   # Thermistor sensors (lookup, Steinhart, beta, linear)
│   │   │   ├── thermocouples/ # Thermocouple sensors (MAX6675, MAX31855)
│   │   │   ├── pressure/      # Pressure sensors (linear, polynomial, table)
│   │   │   ├── voltage/       # Voltage sensors (direct, divider)
│   │   │   ├── linear/        # Generic linear sensors
│   │   │   ├── rpm/           # RPM sensors (W-phase)
│   │   │   ├── speed/         # Speed sensors (Hall effect)
│   │   │   ├── digital/       # Digital sensors (float switch)
│   │   │   ├── level/         # Level sensors (table-based)
│   │   │   ├── environmental/ # Environmental sensors (BME280)
│   │   │   ├── can/           # CAN sensor reading (frame cache, scan)
│   │   │   └── sensor_utils.* # Shared utility functions
│   │   ├── command_table.cpp  # Serial command processor (large file)
│   │   ├── command_table.h    # Command table exports
│   │   ├── command_helpers.cpp # Command helper functions
│   │   ├── serial_config.h    # Serial command interface
│   │   ├── serial_config.cpp  # Serial input handling
│   │   └── alarm_logic.cpp    # Alarm state machine
│   │
│   ├── lib/                   # Library components
│   │   ├── platform.h         # Platform auto-detection
│   │   ├── sensor_types.h     # Data structures
│   │   ├── sensor_library.h   # Sensor library orchestrator
│   │   ├── sensor_library/    # Modular sensor definitions
│   │   │   ├── sensor_types.h       # SensorInfo struct, enums
│   │   │   ├── sensor_categories.h  # Category enum, helpers
│   │   │   ├── sensor_helpers.h     # Lookup functions
│   │   │   └── sensors/             # Sensor entries by type
│   │   │       ├── thermocouples.h  # MAX6675, MAX31855
│   │   │       ├── thermistors.h    # VDO, generic NTC
│   │   │       ├── pressure.h       # Linear, polynomial, table
│   │   │       ├── voltage.h        # Voltage divider
│   │   │       ├── frequency.h      # RPM, speed sensors
│   │   │       ├── environmental.h  # BME280 sensors
│   │   │       ├── digital.h        # Float switch
│   │   │       ├── level.h          # Level sensors
│   │   │       ├── can.h            # CAN sensors
│   │   │       └── none.h           # Unassigned/placeholder
│   │   ├── sensor_calibration_data.h  # Calibration orchestrator
│   │   ├── sensor_calibration_data/   # Calibration database (by manufacturer)
│   │   │   ├── vdo/                 # VDO thermistors & pressure sensors
│   │   │   ├── aem/                 # AEM performance sensors
│   │   │   ├── nxp/                 # NXP/Freescale sensors
│   │   │   ├── bosch/               # Bosch sensors
│   │   │   ├── acdelco/             # AC Delco sensors
│   │   │   ├── smiths/              # Smiths sensors
│   │   │   ├── stewart_warner/      # Stewart Warner sensors
│   │   │   ├── jeep/                # Jeep OEM sensors
│   │   │   ├── generic/             # Generic/aftermarket sensors
│   │   │   └── system/              # System default calibrations
│   │   ├── system_config.h        # System-wide EEPROM config struct
│   │   ├── bus_manager.cpp        # CAN bus management (dual-bus)
│   │   ├── generated/             # Auto-generated files (registry enums)
│   │   └── application_presets.h  # Application configurations
│   │
│   ├── outputs/               # Output module directory
│   │   ├── output_base.h      # Output interface
│   │   ├── output_manager.cpp # Output coordinator
│   │   ├── output_can.cpp     # CAN bus (OBDII)
│   │   ├── output_elm327.cpp  # ELM327 emulation
│   │   ├── output_realdash.cpp # RealDash protocol
│   │   ├── output_serial.cpp  # Serial CSV output
│   │   ├── output_sdlog.cpp   # SD card logging
│   │   ├── output_relay.cpp   # Relay output control
│   │   └── output_alarm.cpp   # Alarm hardware control
│   │
│   ├── displays/              # Display module directory
│   │   └── display_lcd.cpp    # 20x4 LCD display
│   │
│   └── test/                  # Test mode system
│       ├── README.md          # Test mode documentation
│       ├── test_mode.h        # Test mode interface
│       ├── test_mode.cpp      # Test mode implementation
│       ├── test_scenarios.h   # Pre-defined test scenarios
│       └── test_value_generator.cpp  # Test value generators
│
└── docs/                       # Documentation
    ├── README.md                       # Documentation hub
    ├── getting-started/
    │   ├── QUICK_REFERENCE.md          # Quick lookup guide
    │   └── DIRECTORY_SETUP.md          # This file
    ├── guides/
    │   ├── sensor-types/               # Sensor-specific guides
    │   │   ├── SENSOR_SELECTION_GUIDE.md
    │   │   ├── PRESSURE_SENSOR_GUIDE.md
    │   │   ├── VOLTAGE_SENSOR_GUIDE.md
    │   │   ├── DIGITAL_SENSOR_GUIDE.md
    │   │   ├── W_PHASE_RPM_GUIDE.md
    │   │   ├── HALL_SPEED_GUIDE.md
    │   │   ├── THERMISTOR_GUIDE.md
    │   │   ├── THERMOCOUPLE_GUIDE.md
    │   │   ├── BME280_GUIDE.md
    │   │   └── CAN_SENSOR_IMPORT_GUIDE.md
    │   ├── configuration/              # Configuration guides
    │   │   ├── CONFIG_RUN_MODE_GUIDE.md
    │   │   ├── BUILD_CONFIGURATION_GUIDE.md
    │   │   ├── ADDING_SENSORS.md
    │   │   ├── ADVANCED_CALIBRATION_GUIDE.md
    │   │   ├── ALARM_SYSTEM_GUIDE.md
    │   │   ├── JSON_CONFIGURATION_GUIDE.md
    │   │   └── JSON_QUICK_REFERENCE.md
    │   ├── hardware/
    │   │   ├── BIAS_RESISTOR_GUIDE.md
    │   │   ├── PIN_REQUIREMENTS_GUIDE.md
    │   │   ├── CAN_TRANSCEIVER_GUIDE.md
    │   │   ├── LED_INDICATOR_GUIDE.md
    │   │   └── BLUETOOTH_HARDWARE_GUIDE.md
    │   └── outputs/
    │       ├── OBD2_SCANNER_GUIDE.md
    │       ├── REALDASH_SETUP_GUIDE.md
    │       ├── RELAY_CONTROL.md
    │       └── DIRECT_BLE_OBD_GUIDE.md
    ├── reference/
    │   ├── SERIAL_COMMANDS.md
    │   ├── OBD2_PID_REFERENCE.md
    │   └── README.md
    └── architecture/                   # Developer documentation
        ├── REGISTRY_SYSTEM.md
        ├── EEPROM_STRUCTURE.md
        ├── CAN_HAL_ARCHITECTURE.md
        ├── TRANSPORT_ARCHITECTURE.md
        └── BLE_GATT_PROFILE.md
```

---

## File-by-File Guide

### Root Directory

**platformio.ini**
- PlatformIO build configuration
- Defines environments for each supported board
- Library dependencies
- **Edit:** SOMETIMES (to add new board support or libraries)

**README.md**
- Project overview and quick start
- Links to all documentation
- **Edit:** SOMETIMES (for major feature updates)

**LICENSE**
- MIT + NonCommercial License terms
- **Edit:** NEVER

**DISCLAIMER.md**
- Safety information and limitations
- Beta status warning
- **Edit:** RARELY (legal/safety updates only)

---

## src/ - Source Code

### Core Files

**main.cpp**
- Main program loop
- Initialization sequence
- Calls input reading, output sending, alarm checking
- **Edit:** RARELY (only for major system changes)

**config.h**
- Application constants: timing intervals, alarm thresholds, calibration defaults, default units
- Does **not** contain pin assignments or feature flags — those live in the board profile
- **Edit:** RARELY (only to change global timing or threshold defaults)

**src/profiles/profile_*.h** ⭐
- Board definitions: feature flags AND hardware pin assignments for a specific board
- One profile per board, selected via `-include` in `platformio.ini`
- **Edit:** YES - This is where you configure your hardware pins and enabled features

```cpp
// Feature flags
#define ENABLE_CAN             1
#define ENABLE_LCD             1
#define ENABLE_ALARMS          1

// Hardware Pin Assignments
#define MODE_BUTTON_PIN        5    // Multi-function button
#define ALARMS_PIN             3    // Alarm buzzer
#define CAN_CS_PIN             9    // MCP2515 chip select
#define CAN_INT_PIN            2    // MCP2515 interrupt
```

---

## src/inputs/ - Input and Sensor Management

This directory contains the input-based architecture for sensor configuration.

**input.h**
- Defines the Input structure (core of the system)
- Application presets (CHT, OIL_PRESSURE, etc.)
- Sensor types (MAX6675, VDO_120C_TABLE, etc.)
- Calibration override union
- Alarm context structure
- **Edit:** NO - Core architecture definition

**Key concepts:**
- **Application** = What you're measuring (e.g., OIL_PRESSURE)
- **Sensor** = Hardware device (e.g., VDO_5BAR_CURVE)
- **Input** = Physical pin with assigned application and sensor

**input_manager.cpp / input_manager.h**
- Manages input configuration
- EEPROM save/load functionality
- Input initialization and validation
- **Edit:** NO - Core system management

**sensors/** (directory)
- Modular sensor reading implementations organized by sensor type
- Each sensor type in its own subdirectory with focused implementation files
- **thermistors/** - NTC thermistor reading (lookup tables, Steinhart-Hart, beta equation)
- **thermocouples/** - Thermocouple sensors (MAX6675, MAX31855, shared utilities)
- **pressure/** - Pressure sensors (linear voltage-based, polynomial resistance-based)
- **voltage/** - Voltage measurement (direct ADC, voltage divider)
- **linear/** - Generic linear sensors
- **rpm/** - RPM sensing (W-phase alternator)
- **digital/** - Digital inputs (float switches)
- **environmental/** - Environmental sensors (BME280)
- **sensor_utils.*** - Shared utility functions (unit conversions, range validation)
- **Edit:** YES (add new sensor implementations to appropriate subdirectory)

**command_table.cpp / command_table.h**
- Serial command processor — the main implementation of all CLI commands
- **Edit:** RARELY (when adding new commands)

**command_helpers.cpp / command_helpers.h**
- Shared helper functions used by the command processor
- **Edit:** RARELY

**serial_config.h / serial_config.cpp**
- Serial input buffering and dispatch (feeds input to the command processor)
- **Edit:** RARELY

**alarm_logic.cpp**
- Alarm state machine implementation
- Warmup, persistence, fault detection
- **Edit:** RARELY (alarm behavior is configurable)

---

## src/lib/ - Library Components

**platform.h**
- Auto-detects microcontroller platform
- Sets ADC resolution, voltage references, pin mappings
- **Edit:** SOMETIMES (when adding new board support)

**sensor_types.h**
- Core data structures for calibration
- Measurement types (temperature, pressure, etc.)
- Calibration type enums
- **Edit:** RARELY (when adding new calibration methods)

**sensor_library.h** (orchestrator)
- Assembles sensor registry from modular files
- Uses X-macro pattern for registry assembly
- **Edit:** RARELY (sensor definitions live in sensor_library/sensors/)

**sensor_library/** (directory)
- Modular sensor definitions using X-macro pattern
- **sensor_types.h** - SensorInfo struct, PinTypeRequirement enum
- **sensor_categories.h** - Category enum and helper functions
- **sensor_helpers.h** - Lookup functions (getSensorInfo, etc.)
- **sensors/*.h** - Sensor entries organized by type:
  - thermocouples.h, thermistors.h, pressure.h, voltage.h
  - frequency.h (RPM + speed), environmental.h, digital.h
- **Edit:** YES (add sensors to appropriate type file in sensors/)

**sensor_calibration_data.h**
- Main orchestrator that includes all manufacturer calibration files
- **Edit:** RARELY (only when adding new manufacturers)

**sensor_calibration_data/** (directory)
- Modular calibration files organized by manufacturer
- All calibrations stored in PROGMEM (flash memory)
- **vdo/** - VDO thermistors (lookup tables, Steinhart-Hart) & pressure sensors
- **aem/** - AEM performance sensors
- **nxp/** - NXP/Freescale sensors (MPX series)
- **bosch/** - Bosch sensors
- **acdelco/** - AC Delco sensors
- **smiths/** - Smiths sensors
- **stewart_warner/** - Stewart Warner sensors
- **jeep/** - Jeep OEM sensors
- **generic/** - Generic/aftermarket sensors
- **system/** - System defaults (RPM calibrations, voltage dividers)
- **Edit:** YES (add calibrations to appropriate manufacturer file)

**application_presets.h**
- Pre-configured application settings
- Default sensor assignments per application
- Default alarm thresholds
- OBD-II PID mappings
- **Edit:** SOMETIMES (when adding applications)

---

## src/outputs/ - Output Modules

**output_base.h**
- Output module interface
- Common output functions
- **Edit:** RARELY

**output_manager.cpp**
- Coordinates all output modules
- Schedules output updates
- **Edit:** RARELY (when adding new output types)

**output_can.cpp**
- CAN bus output (OBDII PIDs)
- MCP2515 or FlexCAN support
- **Edit:** RARELY

**output_elm327.cpp**
- ELM327 protocol emulation (for Bluetooth OBD adapters)
- **Edit:** RARELY

**output_realdash.cpp**
- RealDash mobile app protocol
- **Edit:** RARELY

**output_relay.cpp**
- Relay output control (e.g., cooling fan based on coolant temp)
- **Edit:** RARELY

**output_serial.cpp**
- Serial CSV output
- **Edit:** SOMETIMES (to customize format)

**output_sdlog.cpp**
- SD card data logging
- **Edit:** SOMETIMES (to customize format)

**output_alarm.cpp**
- Alarm hardware control (buzzer, LED)
- Reads alarm states from inputs
- **Edit:** RARELY

---

## src/displays/ - Display Modules

**display_lcd.cpp**
- 20x4 character LCD display driver
- I2C interface
- Automatic sensor value rotation
- Status display
- **Edit:** SOMETIMES (to customize display layout)

**Future displays:**
- OLED (stub exists, not implemented)
- TFT
- LED matrix
- Custom displays

---

## src/test/ - Test Mode System

This directory contains the comprehensive test mode system for testing outputs without physical sensors.

**README.md**
- Test mode documentation
- How to use test mode
- Available test scenarios
- **Edit:** WHEN UPDATING TEST DOCUMENTATION

**test_mode.h / test_mode.cpp**
- Test mode interface and implementation
- Manages test scenarios
- Generates synthetic sensor data
- **Edit:** WHEN ADDING NEW TEST FEATURES

**test_scenarios.h**
- Pre-defined test scenarios
- Normal operation tests
- Alarm condition tests
- Edge case tests
- **Edit:** YES (when adding new test scenarios)

**test_value_generator.cpp**
- Generates realistic sensor values
- Simulates temperature changes, pressure variations
- RPM simulation, voltage fluctuations
- **Edit:** SOMETIMES (to improve test realism)

---

## docs/ - Documentation

### Core Documentation

**docs/README.md**
- Complete documentation hub
- Links to all guides
- Quick reference tables
- **Edit:** SOMETIMES (when adding new documentation)

### Getting Started

**docs/getting-started/QUICK_REFERENCE.md**
- Fast lookup for common tasks
- Sensor catalog summary
- Pin assignments
- Command reference
- **Edit:** SOMETIMES (when adding new quick references)

**docs/getting-started/DIRECTORY_SETUP.md**
- This file - explains project structure
- **Edit:** WHEN PROJECT STRUCTURE CHANGES

### Sensor Type Guides

**docs/guides/sensor-types/SENSOR_SELECTION_GUIDE.md**
- How to pick the right sensor
- Sensor catalog with examples
- Lookup vs Steinhart comparison
- Complete configuration examples
- **Edit:** WHEN ADDING NEW SENSORS

**docs/guides/sensor-types/PRESSURE_SENSOR_GUIDE.md**
- Everything about pressure sensors
- VDO vs generic sensors
- Wiring and calibration
- **Edit:** WHEN ADDING PRESSURE SENSORS

**docs/guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md**
- Battery and voltage monitoring
- Platform auto-configuration
- Voltage divider setup
- **Edit:** FOR VOLTAGE-RELATED UPDATES

**docs/guides/sensor-types/W_PHASE_RPM_GUIDE.md**
- RPM sensing for classics without electronic ignition
- Voltage protection circuits (CRITICAL for 3.3V boards!)
- **Edit:** FOR RPM-RELATED UPDATES

**docs/guides/sensor-types/DIGITAL_SENSOR_GUIDE.md**
- Float switches and digital inputs
- Wiring and configuration
- **Edit:** FOR DIGITAL INPUT UPDATES

### Configuration Guides

**docs/guides/configuration/CONFIG_RUN_MODE_GUIDE.md**
- CONFIG vs RUN mode operation
- Safe configuration workflow
- Command reference by mode
- **Edit:** WHEN COMMAND BEHAVIOR CHANGES

**docs/guides/configuration/ALARM_SYSTEM_GUIDE.md**
- 5-state alarm state machine
- Warmup and persistence
- Troubleshooting false alarms
- **Edit:** WHEN ALARM SYSTEM CHANGES

**docs/guides/configuration/ADDING_SENSORS.md**
- How to add new sensor types to the library
- Registry architecture
- **Edit:** FOR CONTRIBUTORS

**docs/guides/configuration/ADVANCED_CALIBRATION_GUIDE.md**
- Custom sensor calibrations
- Steinhart-Hart coefficients
- Custom pressure curves
- **Edit:** WHEN ADDING CALIBRATION METHODS

---

## Memory Management

**Flash (PROGMEM):**
- Sensor calibration tables
- Steinhart-Hart coefficients
- String constants
- Lookup tables

**RAM:**
- Input array
- Current sensor readings
- Output buffers
- Display buffers

**EEPROM:**
- Configuration header
- Input configurations
- System configuration
- Persists across power cycles

---

## Data Flow

```
1. Sensor → ADC reading
2. ADC → Calibration function → Engineering units (standard)
3. Engineering units → Display conversion → Display units
4. Engineering units → OBD conversion → CAN bus
5. Engineering units → Serial output
6. Engineering units → SD card log
```

---

## File Organization Philosophy

**Why organized this way:**

1. **Separation of concerns** - Each directory has a specific purpose
2. **Modular architecture** - Easy to add new sensors, outputs, and displays
3. **User-friendly** - Configuration separate from implementation code
4. **Memory efficient** - Calibrations in flash (lib/), not RAM
5. **Community-friendly** - Clear structure makes contributing easier
6. **Testable** - Dedicated test/ directory for testing without hardware

**Directory purposes:**
- **src/** - Main program files (main.cpp, config.h, version.h)
- **src/inputs/** - All input and sensor reading functionality
- **src/lib/** - Reusable library components and sensor definitions
- **src/outputs/** - Output module implementations (CAN, serial, SD, etc.)
- **src/displays/** - Display driver implementations (LCD, future OLED/TFT)
- **src/test/** - Test mode system for bench testing without sensors
- **docs/** - All user and developer documentation

**Best practices:**
- User edits their board profile `src/profiles/profile_<board>.h` for pin assignments and feature flags
- User edits `src/config.h` only for application-level constants (timing, thresholds, units)
- Core code in inputs/ and lib/ rarely needs modification
- New sensors added to:
  - lib/sensor_library/sensors/<type>.h (sensor entry using X-macro)
  - lib/sensor_calibration_data/<manufacturer>/ (calibration data)
- New manufacturers: Create new directory under lib/sensor_calibration_data/
- Output and display modules are self-contained in their directories
- Test scenarios added to test/test_scenarios.h
- Documentation stays synchronized with code changes

---

## Getting Help

**File organization questions:**
- Check this document first
- See [docs/README.md](../README.md) for system overview
- Ask in GitHub Discussions

**Configuration questions:**
- See [docs/getting-started/QUICK_REFERENCE.md](QUICK_REFERENCE.md)
- Check relevant sensor guide
- Post in GitHub Discussions with your config

**Bug reports:**
- GitHub Issues
- Include file versions (git commit hash)
- State which files you modified
