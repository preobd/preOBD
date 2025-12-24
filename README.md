# openEMS - Open Source Engine Monitoring System

**⚠️ BETA SOFTWARE** - See [DISCLAIMER](DISCLAIMER.md) for important safety information.

Open-source, modular engine monitoring system for classic cars and vehicles without OBDII diagnostics.

---

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
- Interface with existing factory ECUs or OBDII systems
- Provide safety certification or guarantees

---

## Supported Hardware

**Platforms:**
- Teensy 4.0/4.1 (native CAN, excellent ADC)
- Arduino Mega 2560 (8KB RAM, full features)
- Arduino Due, ESP32 (12-bit ADC)
- Arduino Uno (2KB RAM, limited - see [Static Builds](docs/advanced/STATIC_BUILDS_GUIDE.md))

**Sensors:**

*Temperature:*
- K-type thermocouples (MAX6675, MAX31855) for CHT/EGT
- VDO thermistors (120°C, 150°C) for coolant, oil, transmission
- Generic NTC thermistors (10K, with Beta or Steinhart-Hart coefficients)
- Linear temperature sensors (0.5-4.5V output)

*Pressure:*
- VDO resistive pressure senders (2-bar, 5-bar, 10-bar)
- Generic linear pressure sensors (0.5-4.5V, any range)
- Freescale/NXP MAP sensors (MPX4250)

*Other:*
- Battery voltage monitoring (with divider)
- W-phase alternator RPM sensing
- BME280 environmental (temp, pressure, humidity, altitude)
- Digital inputs (float switches, warning lights)
- Custom calibration support for unlisted sensors

**Outputs:**
- 20x4 I2C LCD display
- CAN bus (standard OBDII PIDs)
- RealDash mobile dashboard
- Serial CSV output
- SD card data logging

---

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/preobd/openEMS.git
cd openEMS

# Build with PlatformIO
pio run

# Upload to your board
pio run -t upload

# Open serial monitor (115200 baud)
pio device monitor
```

### Configure Sensors

Connect to the serial monitor and enter configuration commands:

```
CONFIG                                                # Enter CONFIG mode
SET 7 EGT MAX31855
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A1 COOLANT_TEMP VDO_120C_STEINHART
SET A2 OIL_PRESSURE VDO_5BAR_PRESSURE
SET A3 BOOST_PRESSURE MPX4250AP
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET I2C:0 AMBIENT_TEMP BME280_TEMP
SAVE
RUN                                                   # Enter RUN mode (starts sensors)
```

**Multi-function button (Pin 5):**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Silence alarms for 30 seconds
- Hold briefly during RUN mode to toggle display on/off

**Serial commands in RUN mode:**
```
LIST INPUTS              # Show configured sensors
INFO A2                  # Show details for one sensor
VERSION                  # Show firmware version
```

---

## Documentation

### Getting Started
- **[Quick Reference](docs/getting-started/QUICK_REFERENCE.md)** - Command cheat sheet and common tasks
- **[Directory Structure](docs/getting-started/DIRECTORY_SETUP.md)** - Project file organization

### Sensor Guides
- **[Sensor Selection Guide](docs/guides/sensor-types/SENSOR_SELECTION_GUIDE.md)** - Complete sensor catalog
- **[Pressure Sensor Guide](docs/guides/sensor-types/PRESSURE_SENSOR_GUIDE.md)** - Pressure sensor wiring
- **[Voltage Monitoring Guide](docs/guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md)** - Battery monitoring
- **[W-Phase RPM Guide](docs/guides/sensor-types/W_PHASE_RPM_GUIDE.md)** - RPM sensing for classics
- **[Digital Sensor Guide](docs/guides/sensor-types/DIGITAL_SENSOR_GUIDE.md)** - Float switches

### Configuration
- **[Serial Commands Reference](docs/reference/SERIAL_COMMANDS.md)** - Complete command list
- **[CONFIG/RUN Mode Guide](docs/guides/configuration/CONFIG_RUN_MODE_GUIDE.md)** - Safe configuration workflow
- **[Alarm System Guide](docs/guides/configuration/ALARM_SYSTEM_GUIDE.md)** - Alarm state machine
- **[Advanced Calibration](docs/guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensor calibrations

### Advanced Topics
- **[Static Builds Guide](docs/advanced/STATIC_BUILDS_GUIDE.md)** - Compile-time configuration for Uno/constrained boards
- **[Complete Documentation](docs/README.md)** - Full documentation index

---

## Common Commands

### Configuration (in CONFIG mode)

```
SET <pin> <app> <sensor>    # Configure a sensor (e.g., SET A2 COOLANT_TEMP VDO_120C_LOOKUP)
SET <pin> ALARM <min> <max> # Set alarm thresholds
SET <pin> UNITS <unit>      # Set display units (CELSIUS, FAHRENHEIT, PSI, BAR, etc.)
SET <pin> NAME <name>       # Set short display name
CLEAR <pin>                 # Remove a sensor
ENABLE <pin>                # Enable a disabled sensor
DISABLE <pin>               # Disable a sensor
SAVE                        # Save configuration to EEPROM
```

### Query (any mode)

```
LIST INPUTS               # Show all configured sensors
LIST APPLICATIONS         # Show available application types
LIST SENSORS              # Show available sensor types  
INFO <pin>                # Show details for one input
DUMP                      # Show complete system state
VERSION                   # Show firmware version
```

### System

```
CONFIG                    # Enter configuration mode
RUN                       # Enter run mode (start monitoring)
RESET                     # Clear all configuration (requires confirmation)
OUTPUT <name> ENABLE      # Enable output module (CAN, Serial, SD_Log, Alarm)
OUTPUT <name> DISABLE     # Disable output module
DISPLAY STATUS            # Show display configuration
```

---

## Project Structure

```
openEMS/
├── platformio.ini          # Build configuration
├── README.md               # This file
├── DISCLAIMER.md           # Safety information
├── src/                    # Source code
│   ├── main.cpp            # Main program loop
│   ├── config.h            # Hardware configuration
│   ├── advanced_config.h   # Advanced features
│   ├── inputs/             # Input and sensor management
│   ├── lib/                # Sensor library and calibrations
│   ├── outputs/            # Output modules (CAN, Serial, SD)
│   ├── displays/           # Display modules (LCD, OLED)
│   └── test/               # Test mode system
└── docs/                   # Documentation
    ├── getting-started/    # Quick start guides
    ├── guides/             # User guides
    ├── reference/          # Command reference
    └── advanced/           # Advanced topics
```

---

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
