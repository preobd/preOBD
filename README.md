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
- Runtime configuration mode (serial commands, EEPROM storage)

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

**Runtime Mode (Teensy/Mega, experimentation):**
- Configure sensors via serial commands
- Settings saved to EEPROM
- Change configuration without recompiling

### 2. Installation

```bash
# Clone the repository
git clone https://github.com/yourusername/openEMS.git
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
SET A0 APPLICATION CHT K_TYPE_THERMOCOUPLE_MAX6675
SET A1 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
SET A2 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
SAVE
```

## Documentation

- **[Full Documentation](docs/README.md)** - Complete system guide
- **[Quick Reference](docs/QUICK_REFERENCE.md)** - Common tasks and lookups
- **[Sensor Selection Guide](docs/SENSOR_SELECTION_GUIDE.md)** - Choose the right sensor
- **[Pressure Sensor Guide](docs/PRESSURE_SENSOR_GUIDE.md)** - Pressure sensor specifics
- **[Voltage Monitoring Guide](docs/VOLTAGE_SENSOR_GUIDE.md)** - Battery monitoring
- **[W-Phase RPM Guide](docs/W_PHASE_RPM_GUIDE.md)** - RPM sensing for classics
- **[Advanced Calibration](docs/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensor setup
- **[Directory Structure](docs/DIRECTORY_SETUP.md)** - File organization
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
│   ├── platform.h         # Auto-detects hardware
│   ├── input.h            # Input-based architecture
│   ├── input_manager.cpp  # Runtime configuration
│   ├── sensor_library.h   # 30+ pre-calibrated sensors
│   ├── sensor_configs.h   # Calibration database
│   ├── sensor_read.cpp    # Sensor reading functions
│   ├── serial_config.h    # Serial command interface
│   ├── outputs/           # Output modules (CAN, Serial, etc.)
│   └── displays/          # Display modules (LCD, etc.)
│
└── docs/                   # Documentation
    ├── README.md
    ├── QUICK_REFERENCE.md
    ├── SENSOR_SELECTION_GUIDE.md
    ├── PRESSURE_SENSOR_GUIDE.md
    ├── VOLTAGE_SENSOR_GUIDE.md
    ├── W_PHASE_RPM_GUIDE.md
    ├── ADVANCED_CALIBRATION_GUIDE.md
    └── DIRECTORY_SETUP.md
```

## OBDII Compatibility

openEMS outputs standard OBDII PIDs via CAN bus, making it compatible with:
- Torque Pro
- RaceChrono
- Harry's Lap Timer
- OBDLink scan tools
- Most OBDII diagnostic apps

**Note:** openEMS emulates OBDII for tool compatibility but does not interface with factory vehicle ECUs.

## Performance

**Typical Configuration (8 sensors, CAN, LCD):**
- Loop time: ~93ms
- RAM usage: ~3KB
- Flash usage: ~35KB
- Update rate: 200ms (configurable)

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

MIT License - Free for personal and commercial use.

See [LICENSE](LICENSE) file for complete terms.

---

**openEMS is beta software. See [DISCLAIMER](DISCLAIMER.md) for important safety information and limitations.**