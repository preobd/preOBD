# Directory Setup Guide - openEMS

**Understanding the project structure and where everything lives**

---

## Complete Directory Structure

```
openEMS/
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project overview, quick start
├── LICENSE                     # MIT License
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
    ├── QUICK_REFERENCE.md              # Quick lookup guide
    ├── SENSOR_SELECTION_GUIDE.md       # How to choose sensors
    ├── PRESSURE_SENSOR_GUIDE.md        # Pressure sensor details
    ├── VOLTAGE_SENSOR_GUIDE.md         # Voltage monitoring
    ├── W_PHASE_RPM_GUIDE.md            # RPM sensing for classics
    ├── DIGITAL_SENSOR_GUIDE.md         # Float switches
    ├── ADVANCED_CALIBRATION_GUIDE.md   # Custom sensor calibration
    ├── ADDING_SENSORS.md               # How to add new sensors
    ├── DISCLAIMER.md                   # Safety and warranty disclaimer
    └── DIRECTORY_SETUP.md              # This file
```

---

## File Descriptions

### Root Directory

**platformio.ini**
- Build configuration for different hardware platforms
- Defines board types, build flags, library dependencies
- **Edit:** Only when adding new board types or libraries

**README.md**
- Project overview and quick start guide
- Safety warnings and beta status
- Basic configuration examples
- **Edit:** Rarely (for project-wide changes)

**LICENSE**
- MIT License terms
- **Edit:** Never

---

## src/ - Core Source Files

### Main Program

**main.cpp**
- Main program initialization and loop
- Calls: input manager → sensor read → outputs → displays → alarms
- **Edit:** Rarely (only for architectural changes)

### User Configuration

**config.h** ⭐ **START HERE - THIS IS WHERE YOU CONFIGURE EVERYTHING**
- Choose configuration mode (compile-time or runtime)
- Enable/disable outputs (CAN, LCD, serial, etc.)
- Set default display units (Celsius/Fahrenheit, etc.)
- Configure sensors (compile-time mode only)
- **Edit:** YES - This is the main configuration file!

**Example sections:**
```cpp
// Choose mode
#define USE_STATIC_CONFIG    // Compile-time (comment out for runtime)

// Enable outputs
#define ENABLE_LCD
#define ENABLE_CAN

// Set defaults
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR

// Configure inputs (compile-time mode)
#define INPUT_1_PIN            A0
#define INPUT_1_APPLICATION    COOLANT_TEMP
#define INPUT_1_SENSOR         VDO_120C_LOOKUP
```

**advanced_config.h**
- Advanced feature configuration
- Test mode settings and configuration
- Custom alarm thresholds
- Expert-level options
- **Edit:** SOMETIMES (for advanced features and testing)

---

### Alarm System

**alarm.cpp**
- Monitors sensor values against thresholds
- Triggers buzzer on out-of-range conditions
- Silence button handling
- **Edit:** RARELY (to customize alarm behavior)

---

## src/inputs/ - Input and Sensor Management

This directory contains all input-related functionality including sensor reading, configuration, and serial interface.

**input.h**
- Defines the Input structure (core of the system)
- Application enum (CHT, OIL_PRESSURE, etc.)
- Sensor enum (VDO_120C_LOOKUP, K_TYPE_THERMOCOUPLE_MAX6675, etc.)
- Calibration override union
- **Edit:** NO - Core architecture definition

**Key concepts:**
- **Application** = What you're measuring (e.g., OIL_PRESSURE)
- **Sensor** = Hardware device (e.g., VDO_5BAR_PRESSURE)
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
- Only compiled in runtime mode
- **Edit:** RARELY (when adding new commands)

**Commands implemented:**
- SET, ENABLE, DISABLE, CLEAR
- LIST INPUTS, LIST APPLICATIONS, LIST SENSORS
- SAVE, LOAD, RESET
- INFO, HELP

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
- DisplayUnits enum
- **Edit:** NO - Core type definitions

**sensor_library.h**
- Catalog of 30+ sensor IDs
- Browse this to find your sensor type
- Just identifiers, no calibration data
- **Edit:** YES (when adding new sensor types to library)

**Example entries:**
```cpp
#define K_TYPE_THERMOCOUPLE_MAX6675  1
#define VDO_120C_LOOKUP              10
#define VDO_5BAR_PRESSURE            20
#define W_PHASE_RPM_12_POLE          70
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
- RPM pole count configurations
- Voltage divider values

**application_presets.h**
- Pre-configured application settings
- Default alarm thresholds per application type
- Application-specific display settings
- **Edit:** SOMETIMES (when adding new application types or modifying defaults)

---

## src/outputs/ - Output Modules

This directory contains all output implementations for sending sensor data to various devices and protocols.

**output_base.h**
- Output module interface definition
- Defines OutputModule structure
- **Edit:** NO - Core interface

**output_manager.cpp**
- Manages all output modules
- Iterates through enabled outputs
- Calls update() on each module
- **Edit:** SOMETIMES (when adding new output types)

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
- **Edit:** SOMETIMES (to customize logging format or add features)

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

**docs/QUICK_REFERENCE.md**
- Fast lookup for common tasks
- Sensor catalog summary
- Pin assignments
- Command reference
- **Edit:** SOMETIMES (when adding new quick references)

---

### Sensor-Specific Guides

**docs/SENSOR_SELECTION_GUIDE.md**
- How to pick the right sensor
- Sensor catalog with examples
- Lookup vs Steinhart comparison
- Complete configuration examples
- **Edit:** WHEN ADDING NEW SENSORS

**docs/PRESSURE_SENSOR_GUIDE.md**
- Everything about pressure sensors
- VDO vs generic sensors
- Wiring and calibration
- Troubleshooting pressure readings
- **Edit:** WHEN ADDING PRESSURE SENSORS

**docs/VOLTAGE_SENSOR_GUIDE.md**
- Battery and voltage monitoring
- Platform auto-configuration
- Voltage divider setup
- Calibration procedures
- **Edit:** FOR VOLTAGE-RELATED UPDATES

**docs/W_PHASE_RPM_GUIDE.md**
- RPM sensing for classics without electronic ignition
- Voltage protection circuits (CRITICAL for 3.3V boards!)
- Wiring and calibration
- Troubleshooting RPM readings
- **Edit:** FOR RPM UPDATES

**docs/DIGITAL_SENSOR_GUIDE.md**
- Float switches and digital inputs
- Normally closed vs normally open
- Wiring and configuration
- **Edit:** FOR DIGITAL SENSOR UPDATES

---

### Advanced Documentation

**docs/ADVANCED_CALIBRATION_GUIDE.md**
- For users with sensors not in library
- Custom sensor calibrations
- Adding to sensor library
- Steinhart-Hart coefficient calculation
- Lookup table creation
- **Edit:** WHEN ADDING CALIBRATION METHODS

**docs/ADDING_SENSORS.md**
- How to add completely new sensor types
- Code structure and patterns
- Testing new sensors
- Contributing back to project
- **Edit:** WHEN SIMPLIFYING CONTRIBUTION PROCESS

**docs/DIRECTORY_SETUP.md**
- This file - explains project structure
- **Edit:** WHEN STRUCTURE CHANGES

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

---

## Understanding the Architecture

### Phase 1a: Unified Input-Based System

**Current state:** COMPLETE ✅

The system now uses a single, unified architecture for both configuration modes:

```
Input = {
    pin,                 // Physical pin (A0, Pin6, etc.)
    application,         // What you're measuring (CHT, OIL_PRESSURE)
    sensor,              // Hardware device (VDO_120C_LOOKUP)
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
1. Initialization:
   config.h OR EEPROM → Input Manager → Input Array

2. Main Loop (every 200ms):
   Input Array → Read Sensors → Update Values
   ↓
   Values → Output Modules (CAN, Serial, SD)
   Values → Display Module (LCD)
   Values → Alarm System → Check Thresholds → Buzzer

3. Runtime Config (if enabled):
   Serial Commands → Input Manager → EEPROM → Input Array
```

---

## Step-by-Step Setup Process

### Method 1: PlatformIO (Recommended)

1. **Install PlatformIO:**
   ```bash
   pip install platformio
   ```

2. **Clone and enter project:**
   ```bash
   git clone https://github.com/yourusername/openEMS.git
   cd openEMS
   ```

3. **Edit configuration:**
   ```bash
   nano src/config.h    # or your preferred editor
   ```

4. **Build and upload:**
   ```bash
   pio run              # Compile
   pio run -t upload    # Upload to board
   pio device monitor   # View serial output
   ```

### Method 2: Arduino IDE

1. **Open project:**
   - Rename openEMS folder to openEMS.ino
   - Or create openEMS.ino that includes main.cpp

2. **Install libraries:**
   - Adafruit BME280
   - LiquidCrystal_I2C
   - CAN (for MCP2515)
   - FlexCAN_T4 (for Teensy)

3. **Select board and port:**
   - Tools → Board → Your board
   - Tools → Port → Your COM port

4. **Upload:**
   - Click Upload button

---

## Common Tasks

### Adding a New Sensor to Library

1. **Choose sensor ID in src/lib/sensor_library.h:**
   ```cpp
   #define MY_NEW_SENSOR_100  50  // Pick unused ID
   ```

2. **Add calibration in src/lib/sensor_calibration_data.h:**
   ```cpp
   static const ThermistorSteinhartCalibration my_sensor_cal PROGMEM = {
       .bias_resistor = 2200.0,
       .steinhart_a = 1.234e-3,
       .steinhart_b = 2.345e-4,
       .steinhart_c = 3.456e-7
   };
   ```

3. **Add to SENSOR_CONFIGS array in src/lib/sensor_calibration_data.h:**
   ```cpp
   {
       .sensorId = MY_NEW_SENSOR_100,
       .name = "My Sensor 100C",
       .internalType = THERMISTOR_STEINHART,
       .readFunction = readThermistorSteinhart,
       .displayConvert = convertTemperature,
       .obdConvert = obdConvertTemp,
       .calibrationData = &my_sensor_cal,
       .calibrationType = CAL_THERMISTOR_STEINHART
   }
   ```

4. **Test and document:**
   - Verify readings at known temperatures
   - Update documentation
   - Share with community!

### Changing Platform

1. **Edit platformio.ini default_envs:**
   ```ini
   [platformio]
   default_envs = teensy40    # Change this
   ```

2. **Or build specific target:**
   ```bash
   pio run -e teensy40
   pio run -e megaatmega2560
   ```

### Switching Configuration Modes

**To Compile-Time:**
```cpp
// In config.h
#define USE_STATIC_CONFIG
```

**To Runtime:**
```cpp
// In config.h - comment out:
// #define USE_STATIC_CONFIG
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
- See docs/README.md for system overview
- Ask in GitHub Discussions

**Configuration questions:**
- See docs/QUICK_REFERENCE.md
- Check relevant sensor guide
- Post in GitHub Discussions with your config.h

**Bug reports:**
- GitHub Issues
- Include file versions (git commit hash)
- State which files you modified

---

**Organized structure makes adding sensors and troubleshooting much easier!**

**For the classic car community.**