# preOBD - Open Source Engine Monitoring System

**⚠️ BETA SOFTWARE** - See [DISCLAIMER](DISCLAIMER.md) for important safety information.

Open-source, modular engine monitoring system for classic cars and vehicles without OBDII diagnostics.

---

## What is preOBD?

preOBD provides comprehensive engine monitoring for vehicles that predate modern ECUs. It reads temperature, pressure, voltage, RPM, and speed sensors from classic gauges and OEM senders, and makes that data available everywhere: an LCD on the dash, OBD-II apps on your phone, RealDash, SD logs, and a browser-based webapp over Bluetooth.

**What it does:**
- Monitors temperature sensors (thermocouples, thermistors, linear)
- Monitors pressure sensors (oil, boost, fuel, coolant)
- Monitors battery voltage
- Monitors engine RPM (via alternator W-phase)
- Monitors vehicle speed (via hall effect sensors)
- **Imports sensors from CAN bus** (OBD-II, J1939) — combine classic gauges with a modern engine swap's ECU data
- Displays data on LCD screen
- Outputs standard OBD-II PIDs via CAN — works with ELM327 adapters, Torque, OBD Fusion, and similar apps
- **Emulates an ELM327 AT command interface over BLE** — connect OBD-II apps directly without any hardware adapter
- Streams data to RealDash for a custom digital dashboard
- **Configures and monitors wirelessly** via a browser-based Web Bluetooth webapp (no app install required)
- Logs data to serial and SD card
- Configurable alarms with audible alerts
- **Controls 12V relays** based on sensor thresholds (cooling fans, pumps, warning lights)

**What it doesn't do:**
- Engine control (monitoring only, relay outputs are auxiliary)
- Provide safety certification or guarantees

---

## Supported Hardware

**Platforms:**
- **Teensy 4.1** (recommended — 8MB flash, built-in SD, native FlexCAN, excellent ADC)
- Teensy 4.0 / Teensy 3.6 (native FlexCAN, excellent ADC)
- ESP32-S3 (native TWAI CAN, BLE / WiFi capable)
- Arduino Mega 2560 (256KB flash, 8KB RAM, MCP2515 SPI CAN)

**Sensors:**

*Temperature:*
- K-type thermocouples (MAX6675, MAX31855) for CHT/EGT
- VDO thermistors (120°C, 150°C) for coolant, oil, transmission
- Smiths, Stewart-Warner, AC Delco, Bosch NTC thermistors
- Generic NTC thermistors (10K, with Beta or Steinhart-Hart coefficients)
- Linear temperature sensors (0.5–4.5V output)

*Pressure:*
- VDO resistive pressure senders (2-bar, 5-bar, 10-bar)
- Generic linear pressure sensors (0.5–4.5V, any range)
- Freescale/NXP MAP sensors (MPX4250)

*Other:*
- Battery voltage monitoring (with divider)
- W-phase alternator RPM sensing
- Hall effect speed sensors (VDO, OEM, generic 3-wire)
- BME280 environmental (temp, pressure, humidity, altitude)
- Digital inputs (float switches, warning lights)
- Custom calibration support for unlisted sensors

**Outputs:**
- 20x4 I2C LCD display
- CAN bus with OBD-II request/response (works with ELM327 adapters and Torque/OBD Fusion)
- ELM327 AT command emulator over BLE — direct OBD-II app connectivity without a hardware adapter
- RealDash mobile dashboard (CAN broadcast mode)
- Web Bluetooth webapp — browser-based configuration and live monitoring, installable as a PWA
- HM-10 BLE module support (UART bridge to webapp or serial terminal)
- Serial CSV output
- SD card data logging
- 12V relay control

---

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/preobd/preOBD.git
cd preOBD

# Build for your platform
pio run -e teensy41      # Teensy 4.1 with built-in SD (recommended)
pio run -e teensy40      # Teensy 4.0
pio run -e mega2560      # Arduino Mega 2560

# Upload to your board
pio run -e teensy41 -t upload

# Open serial monitor (115200 baud)
pio device monitor
```

**Build configuration** uses a three-layer system:
- **`src/profiles/profile_*.h`** — board definitions: all feature flags and hardware pin assignments
- **`src/config.h`** — application constants: timing intervals, alarm thresholds, calibration defaults
- **`platformio.ini`** — selects which profile to build, library deps

See [Build Configuration Guide](docs/guides/configuration/BUILD_CONFIGURATION_GUIDE.md) for details.

### Configure Sensors

Connect to the serial monitor (or the Web Bluetooth webapp) and enter configuration commands:

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

**Multi-function button:**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Silence alarms for 30 seconds
- Hold briefly during RUN mode → Toggle display on/off

**Serial commands in RUN mode:**
```
LIST INPUTS              # Show configured sensors
INFO A2                  # Show details for one sensor
VERSION                  # Show firmware version
```

---

## Documentation

### Getting Started
- **[Quick Reference](docs/getting-started/QUICK_REFERENCE.md)** — Command cheat sheet and common tasks
- **[Directory Structure](docs/getting-started/DIRECTORY_SETUP.md)** — Project file organization

### Sensor Guides
- **[Sensor Selection Guide](docs/guides/sensor-types/SENSOR_SELECTION_GUIDE.md)** — Complete sensor catalog
- **[Pressure Sensor Guide](docs/guides/sensor-types/PRESSURE_SENSOR_GUIDE.md)** — Pressure sensor wiring
- **[Voltage Monitoring Guide](docs/guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md)** — Battery monitoring
- **[W-Phase RPM Guide](docs/guides/sensor-types/W_PHASE_RPM_GUIDE.md)** — RPM sensing for classics
- **[Hall Effect Speed Guide](docs/guides/sensor-types/HALL_SPEED_GUIDE.md)** — Vehicle speed sensing
- **[Digital Sensor Guide](docs/guides/sensor-types/DIGITAL_SENSOR_GUIDE.md)** — Float switches
- **[CAN Sensor Import Guide](docs/guides/sensor-types/CAN_SENSOR_IMPORT_GUIDE.md)** — Import data from OBD-II and J1939 ECUs

### Configuration
- **[Serial Commands Reference](docs/reference/SERIAL_COMMANDS.md)** — Complete command list
- **[CONFIG/RUN Mode Guide](docs/guides/configuration/CONFIG_RUN_MODE_GUIDE.md)** — Safe configuration workflow
- **[JSON Configuration Guide](docs/guides/configuration/JSON_CONFIGURATION_GUIDE.md)** — Bulk import/export over serial
- **[Alarm System Guide](docs/guides/configuration/ALARM_SYSTEM_GUIDE.md)** — Alarm state machine
- **[Relay Control Guide](docs/guides/outputs/RELAY_CONTROL.md)** — Automatic relay control for fans, pumps, and lights
- **[Advanced Calibration](docs/guides/configuration/ADVANCED_CALIBRATION_GUIDE.md)** — Custom sensor calibrations

### Outputs and Connectivity
- **[Direct BLE OBD Guide](docs/guides/outputs/DIRECT_BLE_OBD_GUIDE.md)** — Connect OBD-II apps via BLE without a hardware adapter
- **[OBD-II Scanner Guide](docs/guides/outputs/OBD2_SCANNER_GUIDE.md)** — ELM327 adapter and app compatibility
- **[RealDash Setup Guide](docs/guides/outputs/REALDASH_SETUP_GUIDE.md)** — RealDash dashboard configuration

### Hardware
- **[Bluetooth Hardware Guide](docs/guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md)** — HM-10 and BLE module wiring
- **[CAN Transceiver Guide](docs/guides/hardware/CAN_TRANSCEIVER_GUIDE.md)** — CAN bus hardware
- **[Build Configuration Guide](docs/guides/configuration/BUILD_CONFIGURATION_GUIDE.md)** — Compile-time options and board profiles

### Advanced Topics
- **[Complete Documentation](docs/README.md)** — Full documentation index

---

## Common Commands

### Configuration (in CONFIG mode)

```
SET <pin> <app> <sensor>         # Configure a sensor
SET <pin> ALARM <min> <max>      # Set alarm thresholds
SET <pin> UNITS <unit>           # Set display units (CELSIUS, FAHRENHEIT, PSI, BAR, etc.)
SET <pin> NAME <name>            # Set short display name
SET <pin> DIVIDER <ratio>        # Set voltage divider ratio for 5V sensors on 3.3V ADC
CLEAR <pin>                      # Remove a sensor
ENABLE <pin>                     # Enable a disabled sensor
DISABLE <pin>                    # Disable a sensor
SAVE                             # Save configuration to EEPROM
```

### Query (any mode)

```
HELP                             # Show help category overview
HELP <category>                  # Show detailed help (LIST, SET, CALIBRATION, etc.)
HELP QUICK                       # Show compact command reference
LIST INPUTS                      # Show all configured sensors
LIST INPUTS JSON                 # Export sensor configuration as JSON
LIST APPLICATIONS                # Show available application types
LIST SENSORS [category]          # Show available sensor types (optionally filtered)
INFO <pin>                       # Show details for one input
DUMP                             # Show complete system state
SYSTEM DUMP REGISTRY JSON        # Export full sensor/application catalog as JSON
VERSION                          # Show firmware version
```

### Connectivity

```
TRANSPORT <port> PRIMARY         # Set primary control/response port (USB, BLE, UART2, etc.)
TRANSPORT <port> SECONDARY       # Add secondary port for simultaneous control
AT <port> <command>              # Send raw AT command to an attached BLE or serial module
OUTPUT <name> ENABLE             # Enable output module (CAN, Serial, SD_Log, Alarm)
OUTPUT <name> DISABLE            # Disable output module
```

### Bulk Configuration

```
JSON IMPORT                      # Stream a JSON config blob over serial to bulk-configure inputs
LIST INPUTS JSON                 # Export current config as JSON (for backup or transfer)
```

### System

```
CONFIG                           # Enter configuration mode
RUN                              # Enter run mode (start monitoring)
RESET                            # Clear all configuration (requires confirmation)
DISPLAY STATUS                   # Show display configuration
SYSTEM PINS                      # Show pin allocation status
```

---

## Project Structure

```
preOBD/
├── platformio.ini          # Build environments
├── README.md               # This file
├── DISCLAIMER.md           # Safety information
├── src/                    # Source code
│   ├── main.cpp            # Main program loop
│   ├── config.h            # Application constants (timing, thresholds, defaults)
│   ├── profiles/           # Board definitions (feature flags + pin assignments)
│   ├── inputs/             # Input and sensor management
│   ├── lib/                # Sensor library and calibrations
│   ├── outputs/            # Output modules (CAN, Serial, SD, relay)
│   ├── displays/           # Display modules (LCD)
│   └── test/               # Test mode system
└── docs/                   # Documentation
    ├── getting-started/    # Quick start guides
    ├── guides/             # User guides (configuration, hardware, sensors, outputs)
    ├── reference/          # Command and PID reference
    └── architecture/       # Contributor-facing architecture docs
```

---

## Contributing

Contributions welcome:

1. **Sensor calibrations** — Share your sensor data
2. **Platform testing** — Test on different hardware
3. **Documentation** — Improve clarity
4. **Bug reports** — Help make it more reliable

**Please:**
- Test thoroughly on hardware before submitting
- Document new sensors with datasheets
- Follow existing code style
- Update documentation

## Community and Support

**Getting Help:**
- **GitHub Issues** — Bug reports and feature requests
- **GitHub Discussions** — Questions and setup help
- **Documentation** — Start here for most questions

**When asking for help, include:**
1. Hardware platform (Teensy 4.1, ESP32-S3, Arduino Mega, etc.)
2. Sensor types being used
3. Serial output showing the issue
4. What you've already tried

## License

preOBD is licensed under a custom **MIT + NonCommercial License**.

### What You Can Do (Personal / Noncommercial Use)
- Use the software for personal projects
- Modify, fork, or extend the software
- Publish your modifications (noncommercially)
- Learn from and build upon the source code
- Use it for educational or research purposes

### What You Cannot Do (Without Permission)
- Sell the software
- Bundle it into a product you sell
- Use it in a for-profit business
- Monetize it in any form
- Distribute commercial derivatives

For commercial use, contact: info@preobd.com

See [LICENSE](LICENSE) file for complete terms.

---

**preOBD is beta software. See [DISCLAIMER](DISCLAIMER.md) for important safety information and limitations.**
