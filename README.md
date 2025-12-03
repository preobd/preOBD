# openEMS - Open Source Engine Monitoring System

**⚠️ BETA SOFTWARE** - See [DISCLAIMER](DISCLAIMER.md) for important safety information.

Open-source, modular engine monitoring system for classic cars and vehicles without OBDII diagnostics.

## What is openEMS?

openEMS provides comprehensive engine monitoring for vehicles that lack modern electronic systems. It monitors temperature, pressure, voltage, and RPM sensors, providing data via LCD display, CAN bus (OBDII compatible), serial output, and data logging.

**What it does:**
- Monitors multiple temperature sensors (thermocouples, thermistors)
- Monitors pressure sensors (oil, boost, fuel)
- Monitors battery voltage
- Monitors engine RPM (via alternator W-phase)
- Displays data on LCD screen
- Outputs standard OBDII PIDs via CAN bus
- Logs data to serial and SD card
- Configurable alarms with audible alerts

**What it doesn't do:**
- Engine control (monitoring only, no outputs to engine)
- Replace mechanical gauges (use as supplement, not replacement)
- Interface with existing factory ECUs or OBDII systems
- Provide safety certification or guarantees

## Current Capabilities

**Sensor Support:**
- 17+ sensor types
- 30+ pre-calibrated sensor configurations
- Custom calibration support for non-standard sensors

**Configuration:**
- Compile-time configuration mode (minimal RAM, recommended for Arduino Uno)
- Runtime configuration mode (serial commands, EEPROM storage, CONFIG/RUN mode separation)

**Hardware Platforms:**
- Arduino Uno (2KB RAM, limited sensors)
- Arduino Mega 2560 (8KB RAM, full features)
- Teensy 3.x/4.x (native CAN, excellent ADC)
- Arduino Due, ESP32 (less tested)

**Outputs:**
- 20x4 I2C LCD display
- CAN bus (standard OBDII PIDs)
- RealDash mobile dashboard
- Serial CSV output
- SD card data logging

## Supported Sensors

**Temperature:**
- MAX6675/MAX31855 K-type thermocouples (CHT, EGT)
- VDO thermistors (120°C, 150°C) - coolant, oil, transfer case
- Generic NTC thermistors with various β values
- BME280 ambient temperature

**Pressure:**
- VDO pressure sensors (2-bar, 5-bar) - oil, boost
- Generic linear sensors (0.5-4.5V)
- Freescale MPX4250AP MAP sensor
- BME280 barometric pressure

**RPM:**
- W-phase alternator sensing (for classics without electronic ignition)
- Configurable for 12/14/16-pole alternators

**Other:**
- Battery voltage monitoring (auto-configured per platform)
- Float switches (coolant level, fuel level)
- BME280 humidity and elevation

## Quick Start

### 1. Choose Configuration Mode

**Compile-Time Mode (Arduino Uno, stable setups):**
- Configure sensors in `config.h` at compile time
- Smallest memory footprint
- No runtime configuration overhead

**Runtime Mode (Teensy/Mega, under-development):**
- Configure sensors via serial commands
- Settings saved to EEPROM
- Change configuration without recompiling

### 2. Installation

```bash
# Clone the repository
git clone https://github.com/preobd/openEMS.git
cd openEMS

# Build with PlatformIO
pio run

# Upload to your board
pio run -t upload

# Monitor serial output
pio device monitor
```

### 3. Basic Configuration

**Compile-Time Mode (config.h):**

```cpp
// Choose compile-time mode
#define USE_STATIC_CONFIG

// Enable outputs
#define ENABLE_LCD
#define ENABLE_SERIAL_OUTPUT

// Configure sensors
#define INPUT_1_PIN            6
#define INPUT_1_APPLICATION    CHT
#define INPUT_1_SENSOR         K_TYPE_THERMOCOUPLE_MAX6675

#define INPUT_2_PIN            A2
#define INPUT_2_APPLICATION    COOLANT_TEMP
#define INPUT_2_SENSOR         VDO_120C_LOOKUP

#define INPUT_3_PIN            A3
#define INPUT_3_APPLICATION    OIL_PRESSURE
#define INPUT_3_SENSOR         VDO_5BAR_PRESSURE
```

**Runtime Mode (serial commands):**

```
CONFIG                                                # Enter CONFIG mode
SET 6 APPLICATION CHT
SET 6 SENSOR MAX6675
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR_PRESSURE
SAVE
RUN                                                   # Enter RUN mode (starts sensors)
```

**Hardware Requirements (Runtime Mode):**
- MODE_BUTTON (Pin 4): Hold during boot to enter CONFIG mode, press in RUN mode to silence alarms

## Documentation

**Getting Started:**
- **[Quick Reference](docs/getting-started/QUICK_REFERENCE.md)** - Common tasks and lookups
- **[Directory Structure](docs/getting-started/DIRECTORY_SETUP.md)** - File organization

**Sensor Guides:**
- **[Sensor Selection Guide](docs/guides/sensor-types/SENSOR_SELECTION_GUIDE.md)** - Choose the right sensor
- **[Pressure Sensor Guide](docs/guides/sensor-types/PRESSURE_SENSOR_GUIDE.md)** - Pressure sensor specifics
- **[Voltage Monitoring Guide](docs/guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md)** - Battery monitoring
- **[W-Phase RPM Guide](docs/guides/sensor-types/W_PHASE_RPM_GUIDE.md)** - RPM sensing for classics
- **[Digital Sensor Guide](docs/guides/sensor-types/DIGITAL_SENSOR_GUIDE.md)** - Float switches and digital inputs

**Configuration:**
- **[Adding Sensors](docs/guides/configuration/ADDING_SENSORS.md)** - How to add new sensors
- **[Advanced Calibration](docs/guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensor setup
- **[Display Units](docs/guides/configuration/DISPLAY_UNITS_CONFIGURATION_GUIDE.md)** - Configure display units
- **[Config/Run Mode Guide](docs/guides/configuration/CONFIG_RUN_MODE_GUIDE.md)** - CONFIG vs RUN mode (runtime mode only)

**Complete Documentation:**
- **[Full Documentation](docs/README.md)** - Complete system guide
- **[Disclaimer](DISCLAIMER.md)** - Safety information and limitations

## Project Structure

```
openEMS/
├── platformio.ini          # Build configuration
├── README.md               # This file
├── DISCLAIMER.md           # Safety information
│
├── src/                    # Source code
│   ├── main.cpp           # Main program loop
│   ├── config.h           # ⚠️ USER CONFIGURATION
│   ├── advanced_config.h  # Advanced features config
│   ├── alarm.cpp          # Alarm system
│   ├── inputs/            # Input and sensor management
│   │   ├── input.h
│   │   ├── input_manager.cpp/h
│   │   ├── sensor_read.cpp
│   │   └── serial_config.cpp/h
│   ├── lib/               # Library components
│   │   ├── platform.h
│   │   ├── sensor_types.h
│   │   ├── sensor_library.h
│   │   ├── sensor_calibration_data.h
│   │   └── application_presets.h
│   ├── outputs/           # Output modules (CAN, Serial, SD)
│   ├── displays/          # Display modules (LCD, etc.)
│   └── test/              # Test mode system
│
└── docs/                   # Documentation
    ├── README.md           # Complete documentation
    ├── DISCLAIMER.md       # Safety information
    ├── getting-started/    # Getting started guides
    │   ├── QUICK_REFERENCE.md
    │   └── DIRECTORY_SETUP.md
    ├── guides/             # User guides
    │   ├── sensor-types/   # Sensor-specific guides
    │   │   ├── SENSOR_SELECTION_GUIDE.md
    │   │   ├── PRESSURE_SENSOR_GUIDE.md
    │   │   ├── VOLTAGE_SENSOR_GUIDE.md
    │   │   ├── DIGITAL_SENSOR_GUIDE.md
    │   │   └── W_PHASE_RPM_GUIDE.md
    │   └── configuration/  # Configuration guides
    │       ├── ADDING_SENSORS.md
    │       ├── ADVANCED_CALIBRATION_GUIDE.md
    │       └── DISPLAY_UNITS_CONFIGURATION_GUIDE.md
    └── reference/          # Reference materials
        └── README.md
```

## OBDII Compatibility

openEMS outputs standard OBDII PIDs via CAN bus, making it compatible with:
- Torque Pro
- OBDLink scan tools
- Most OBDII diagnostic apps

**Note:** openEMS emulates OBDII for tool compatibility but does not interface with factory vehicle ECUs.

## Performance

**Typical Configuration (8 sensors, CAN, LCD):**
- RAM usage: ~3KB
- Flash usage: ~35KB

**Arduino Uno (minimal, 3-6 sensors):**
- RAM usage: ~1.5-2KB
- Flash usage: ~25-30KB
- Fully functional with compile-time mode

## Contributing

Contributions welcome:

1. **Sensor calibrations** - Share your sensor data
2. **Platform testing** - Test on different hardware
3. **Documentation** - Improve clarity
4. **Bug reports** - Help make it more reliable

**Please:**
- Test thoroughly on hardware before submitting
- Document new sensors with datasheets
- Follow existing code style
- Update documentation

## Community and Support

**Getting Help:**
- **GitHub Issues** - Bug reports and feature requests
- **GitHub Discussions** - Questions and setup help
- **Documentation** - Start here for most questions

**When asking for help, include:**
1. Hardware platform (Arduino Mega, Teensy 4.0, etc.)
2. Sensor types being used
3. Configuration mode (compile-time or runtime)
4. Serial output showing the issue
5. What you've already tried

## License
openEMS is licensed under a custom **MIT + NonCommercial License**.

### What You Can Do (Personal / Noncommercial Use)
- Use the software for personal projects
- Modify, fork, or extend the software
- Publish your modifications (noncommercially)
- Learn from and build upon the source code
- Use it for educational or research purposes

### What You Cannot Do (Without Permission)
- Sell the software
- Bundle it into a product you sell
- Use it in a for‑profit business
- Monetize it in any form
- Distribute commercial derivatives

For commercial use, contact: info@preobd.com

See [LICENSE](LICENSE) file for complete terms.

---

**openEMS is beta software. See [DISCLAIMER](DISCLAIMER.md) for important safety information and limitations.**
