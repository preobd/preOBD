# Directory Setup Guide - openEMS

**Understanding the project structure and where everything lives**

---

## Complete Directory Structure

```
openEMS/
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project overview, quick start
├── LICENSE                     # MIT + NonCommercial License
├── DISCLAIMER.md               # Safety and warranty disclaimer
│
├── src/                        # All source code
│   ├── main.cpp               # Main program loop
│   ├── config.h               # ⭐ USER CONFIGURATION FILE
│   ├── advanced_config.h      # Advanced features configuration
│   ├── alarm.cpp              # Alarm system
│   │
│   ├── inputs/                # Input and sensor management
│   │   ├── input.h            # Input structure definitions
│   │   ├── input_manager.cpp  # Input configuration management
│   │   ├── input_manager.h    # Input manager exports
│   │   ├── sensor_read.cpp    # Sensor reading functions
│   │   ├── serial_config.h    # Serial command interface
│   │   └── serial_config.cpp  # Serial command implementation
│   │
│   ├── lib/                   # Library components
│   │   ├── platform.h         # Platform auto-detection
│   │   ├── sensor_types.h     # Data structures
│   │   ├── sensor_library.h   # Sensor catalog (30+ sensors)
│   │   ├── sensor_calibration_data.h  # Calibration database
│   │   └── application_presets.h      # Application preset configurations
│   │
│   ├── outputs/               # Output module directory
│   │   ├── output_base.h      # Output interface
│   │   ├── output_manager.cpp # Output coordinator
│   │   ├── output_can.cpp     # CAN bus (OBDII)
│   │   ├── output_realdash.cpp # RealDash protocol
│   │   ├── output_serial.cpp  # Serial CSV output
│   │   └── output_sdlog.cpp   # SD card logging
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
    ├── README.md                       # Complete documentation
    ├── getting-started/                # Getting started guides
    │   ├── QUICK_REFERENCE.md          # Quick lookup guide
    │   └── DIRECTORY_SETUP.md          # This file
    ├── guides/                         # User guides
    │   ├── sensor-types/               # Sensor-specific guides
    │   │   ├── SENSOR_SELECTION_GUIDE.md
    │   │   ├── PRESSURE_SENSOR_GUIDE.md
    │   │   ├── VOLTAGE_SENSOR_GUIDE.md
    │   │   ├── DIGITAL_SENSOR_GUIDE.md
    │   │   └── W_PHASE_RPM_GUIDE.md
    │   └── configuration/              # Configuration guides
    │       ├── ADDING_SENSORS.md
    │       ├── ADVANCED_CALIBRATION_GUIDE.md
    │       └── DISPLAY_UNITS_CONFIGURATION_GUIDE.md
    └── reference/                      # Reference materials
        └── README.md
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

**config.h** ⭐
- Primary user configuration file
- Build mode selection (compile-time vs runtime)
- Input definitions (for compile-time mode)
- Output enables (LCD, CAN, Serial, SD)
- **Edit:** YES - This is where you configure your system

```cpp
// Build Mode Selection
#define USE_STATIC_CONFIG     // Enable compile-time mode

// Input Configuration (compile-time mode)
#define INPUT_0_PIN            6
#define INPUT_0_APPLICATION    CHT
#define INPUT_0_SENSOR         MAX6675

// Output Enables
#define ENABLE_LCD
#define ENABLE_CAN
#define ENABLE_SERIAL_OUTPUT
```

**advanced_config.h**
- Advanced settings and customizations
- Custom calibration overrides
- Test mode configuration
- **Edit:** SOMETIMES (for custom calibrations)

**alarm.cpp**
- Alarm logic and thresholds
- Audible and visual alarm handling
- **Edit:** RARELY (alarm behavior is configurable via inputs)

---

## src/inputs/ - Input and Sensor Management

This directory contains the unified input-based architecture that supports both compile-time and runtime configuration.

**input.h**
- Defines the Input structure (core of the system)
- Application enum (CHT, OIL_PRESSURE, etc.)
- Sensor enum (MAX6675, VDO_120C_LOOKUP, etc.)
- Calibration override union
- **Edit:** NO - Core architecture definition

**Key concepts:**
- **Application** = What you're measuring (e.g., OIL_PRESSURE)
- **Sensor** = Hardware device (e.g., VDO_5BAR)
- **Input** = Physical pin with assigned application and sensor

**input_manager.cpp / input_manager.h**
- Manages input configuration (both modes)
- EEPROM save/load (runtime mode)
- Compile-time config processing (static mode)
- Input initialization and validation
- **Edit:** NO - Core system management

**sensor_read.cpp**
- All sensor reading implementations
- Thermistor, thermocouple, pressure, voltage functions
- Unit conversion functions (temp, pressure, etc.)
- Display conversion functions
- OBDII conversion functions
- **Edit:** RARELY (only when adding completely new sensor types)

**serial_config.h / serial_config.cpp**
- Serial command interface for runtime configuration
- Command parser and handlers
- Only compiled when USE_STATIC_CONFIG is NOT defined
- **Edit:** RARELY (when adding new commands)

**Serial Commands (runtime mode only):**

| Category | Commands |
|----------|----------|
| Configuration | `SET <pin> APPLICATION <app>`, `SET <pin> SENSOR <sensor>` |
| Control | `ENABLE`, `DISABLE`, `CLEAR` |
| Query | `LIST INPUTS`, `LIST APPLICATIONS`, `LIST SENSORS`, `INFO` |
| System | `SAVE`, `LOAD`, `RESET`, `HELP` |

---

## src/lib/ - Library Components

This directory contains core library components, sensor definitions, and platform abstractions.

**platform.h**
- Automatically detects Arduino, Teensy, ESP32, Due
- Configures system voltage (3.3V or 5V)
- Sets ADC resolution (10-bit or 12-bit)
- Configures voltage dividers for battery monitoring
- **Edit:** NO - Automatic detection handles everything

**What it detects:**
- Board type and family
- System voltage
- ADC resolution and range
- Appropriate voltage dividers
- Platform-specific optimizations

**sensor_types.h**
- Data structure definitions
- Calibration structure types
- Units enum (CELSIUS, PSI, BAR, etc.)
- **Edit:** NO - Core type definitions

**sensor_library.h**
- Catalog of 30+ sensor definitions
- Maps sensor IDs to read functions and calibrations
- All stored in PROGMEM (flash memory)
- **Edit:** YES (when adding new sensor types to library)

**Sensor entries include:**
```cpp
{
    .sensor = VDO_120C_LOOKUP,
    .name = "VDO 120C (Lookup)",
    .readFunction = readThermistorLookup,
    .measurementType = MEASURE_TEMPERATURE,
    .calibrationType = CAL_THERMISTOR_LOOKUP,
    .defaultCalibration = &vdo120_lookup_cal
}
```

**sensor_calibration_data.h**
- Centralized calibration database
- Lookup tables for VDO sensors
- Steinhart-Hart coefficients
- Pressure sensor polynomials
- All stored in PROGMEM (flash, not RAM)
- **Edit:** YES (when adding new sensor calibrations)

**What's stored here:**
- Temperature lookup tables
- Steinhart-Hart coefficients for thermistors
- Pressure sensor linear/polynomial calibrations
- Voltage divider configurations

**application_presets.h**
- Pre-configured application settings
- Default sensor assignments per application
- Default alarm thresholds
- OBD-II PID mappings
- **Edit:** SOMETIMES (when adding new application types)

---

## src/outputs/ - Output Modules

This directory contains all output implementations for sending sensor data to various devices and protocols.

**Architecture: Data-Driven Time-Sliced Outputs**

openEMS uses a **fully data-driven output architecture** where all outputs are defined in a single array with their send intervals:

```cpp
OutputModule outputModules[] = {
    {"CAN", true, initCAN, sendCAN, updateCAN, 100},       // 10Hz
    {"RealDash", true, initRealdash, sendRealdash, updateRealdash, 100},
    {"Serial", true, initSerialOutput, sendSerialOutput, updateSerialOutput, 1000},
    {"SD_Log", true, initSDLog, sendSDLog, updateSDLog, 5000}
};
```

Each output runs at its own independent interval without blocking others:
- **CAN/RealDash:** 100ms (10Hz) - smooth dashboards
- **Serial CSV:** 1000ms (1Hz) - avoid flooding
- **SD logging:** 5000ms (0.2Hz) - reduce wear

**Benefits:**
- Adding new output = ONE line in array
- No changes to main.cpp
- Fully scalable architecture
- Consistent with sensor library pattern

**output_base.h**
- Output module interface definition
- Defines OutputModule structure with `sendInterval` field
- **Edit:** NO - Core interface

**output_manager.cpp**
- Data-driven output coordinator
- Time-sliced sending via `sendToOutputs(now)`
- Per-output timing management
- **Edit:** YES - Add new output module here (one line in array)

**output_can.cpp**
- CAN bus OBDII output
- Standard diagnostic PIDs
- Compatible with Torque, RaceChrono, scan tools
- **Edit:** RARELY (to add custom PIDs or change mapping)

**output_realdash.cpp**
- RealDash mobile dashboard protocol
- Custom framing for RealDash app
- **Edit:** RARELY (RealDash protocol is stable)

**output_serial.cpp**
- Serial CSV output for debugging/logging
- Human-readable format
- **Edit:** RARELY (to change output format)

**output_sdlog.cpp**
- SD card data logging
- CSV file creation and writing
- **Edit:** SOMETIMES (to customize logging format)

---

## src/displays/ - Display Modules

This directory contains display driver implementations for visual output.

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
- Complete system documentation
- Hardware setup and wiring
- Configuration guide
- Troubleshooting
- **Edit:** SOMETIMES (to improve clarity or add sections)

---

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

---

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
- Troubleshooting pressure readings
- **Edit:** WHEN ADDING PRESSURE SENSORS

**docs/guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md**
- Battery and voltage monitoring
- Platform auto-configuration
- Voltage divider setup
- Calibration procedures
- **Edit:** FOR VOLTAGE-RELATED UPDATES

**docs/guides/sensor-types/W_PHASE_RPM_GUIDE.md**
- RPM sensing for classics without electronic ignition
- Voltage protection circuits (CRITICAL for 3.3V boards!)
- Wiring and calibration
- Troubleshooting RPM readings
- **Edit:** FOR RPM UPDATES

**docs/guides/sensor-types/DIGITAL_SENSOR_GUIDE.md**
- Float switches and digital inputs
- Normally closed vs normally open
- Wiring and configuration
- **Edit:** FOR DIGITAL SENSOR UPDATES

---

### Configuration Guides

**docs/guides/configuration/ADDING_SENSORS.md**
- How to add completely new sensor types
- Code structure and patterns
- Testing new sensors
- Contributing back to project
- **Edit:** WHEN SIMPLIFYING CONTRIBUTION PROCESS

**docs/guides/configuration/ADVANCED_CALIBRATION_GUIDE.md**
- For users with sensors not in library
- Custom sensor calibrations
- Adding to sensor library
- Steinhart-Hart coefficient calculation
- Lookup table creation
- **Edit:** WHEN ADDING CALIBRATION METHODS

**docs/guides/configuration/DISPLAY_UNITS_CONFIGURATION_GUIDE.md**
- Configuring display units (Celsius/Fahrenheit, PSI/bar, etc.)
- Unit conversion settings
- **Edit:** WHEN ADDING NEW UNIT OPTIONS

---

### Reference Materials

**docs/reference/README.md**
- Placeholder for future reference documentation
- Pin mappings, command reference, troubleshooting, etc.
- **Edit:** WHEN ADDING REFERENCE CONTENT

---

## Build System

### PlatformIO Configuration

**platformio.ini sections:**

```ini
[platformio]
default_envs = megaatmega2560    # Default build target

[env]
framework = arduino
monitor_speed = 115200
lib_deps = ...                   # Shared libraries

[env:megaatmega2560]            # Arduino Mega
platform = atmelavr
board = megaatmega2560
build_flags = -Os -Wall

[env:teensy40]                  # Teensy 4.0
platform = teensy
board = teensy40
build_flags = -O2 -Wall
lib_deps = ${env.lib_deps}
    FlexCAN_T4                  # Native CAN library

[env:uno]                       # Arduino Uno
platform = atmelavr
board = uno
build_flags = -Os -Wall
```

### Building

**Build default target:**
```bash
pio run
```

**Build specific target:**
```bash
pio run -e teensy40
pio run -e megaatmega2560
pio run -e uno
```

**Upload:**
```bash
pio run -t upload
pio run -e teensy40 -t upload
```

**Monitor:**
```bash
pio device monitor
```

### Switching Configuration Modes

**To Compile-Time Mode:**
```cpp
// In config.h
#define USE_STATIC_CONFIG
```

**To Runtime Mode:**
```cpp
// In config.h - comment out:
// #define USE_STATIC_CONFIG
```

---

## Understanding the Architecture

### Unified Input-Based System

The system uses a single, unified architecture for both configuration modes:

```
Input = {
    pin,                 // Physical pin (A0, Pin6, etc.)
    application,         // What you're measuring (CHT, OIL_PRESSURE)
    sensor,              // Hardware device (MAX6675, VDO_5BAR)
    calibration          // How to convert readings (from library)
}
```

**Both modes use the same:**
- Input structure
- Sensor library
- Calibration database
- Reading functions
- Output modules
- Display code

**Only difference:**
- **Compile-time:** Inputs defined in config.h, fixed at compile
- **Runtime:** Inputs configured via serial, saved to EEPROM

### Memory Management

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

**EEPROM (runtime mode only):**
- Configuration header
- Input configurations
- Persists across power cycles

### Data Flow

```
1. Sensor → ADC reading
2. ADC → Calibration function → Engineering units
3. Engineering units → Display conversion → Display
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
- **src/** - Main program files (main.cpp, config.h, alarm.cpp)
- **src/inputs/** - All input and sensor reading functionality
- **src/lib/** - Reusable library components and sensor definitions
- **src/outputs/** - Output module implementations (CAN, serial, SD, etc.)
- **src/displays/** - Display driver implementations (LCD, future OLED/TFT)
- **src/test/** - Test mode system for bench testing without sensors
- **docs/** - All user and developer documentation

**Best practices:**
- User edits only src/config.h and src/advanced_config.h (usually)
- Core code in inputs/ and lib/ rarely needs modification
- New sensors added to lib/sensor_library.h and lib/sensor_calibration_data.h
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
- Post in GitHub Discussions with your config.h

**Bug reports:**
- GitHub Issues
- Include file versions (git commit hash)
- State which files you modified

---

**Organized structure makes adding sensors and troubleshooting much easier!**

**For the classic car community.**
