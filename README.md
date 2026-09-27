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
- **Imports sensors from CAN bus** (OBD-II PIDs) — combine classic gauges with a modern engine swap's ECU data
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
- VDO resistive pressure senders (2, 5, 10 and 25 bar; 80 PSI)
- Smiths, Stewart-Warner and Jeep resistive oil-pressure senders
- Generic and AEM linear pressure sensors (0.5–4.5V)
- Freescale/NXP MAP sensors (MPX4250AP, MPX5700AP)

*Other:*
- Fuel level senders (VDO, Stewart-Warner, Ford/Chrysler, Jeep CJ)
- Throttle position (generic 0.5–4.5V)
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

**Build configuration** is organized per board under `boards/<board>/`:
- **`boards/<board>/board.h`** — board definition: capabilities, ceilings, sizing, and CHOICE feature flags (no wired pins)
- **`boards/<board>/wiring.ini`** — your physical pin assignments as `-D` flags (gitignored; copy `wiring.ini.example`)
- **`boards/<board>/board.ini`** — the PlatformIO `[env:<board>]` fragment; `boards/resolve.h` derives wired feature flags from your pins
- **`src/config.h`** — application constants: timing intervals, alarm thresholds, calibration defaults

See [Build Configuration Guide](docs/guides/configuration/BUILD_CONFIGURATION_GUIDE.md) for details.

### Configure Sensors

Connect to the serial monitor (or the Web Bluetooth webapp) and enter configuration commands:

```
CONFIG                                                # Enter CONFIG mode
SET 7 EGT MAX31855
SET A0 OIL_TEMP VDO_150C_STEINHART
SET A1 COOLANT_TEMP VDO_120C_STEINHART
SET A2 OIL_PRESSURE VDO_5BAR_CURVE
SET A3 BOOST_PRESSURE MPX4250AP
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER
SET I2C:0 AMBIENT_TEMP BME280_TEMP
SAVE
RUN                                                   # Enter RUN mode (starts sensors)
```

**Multi-function button:**
- Hold during boot → Enter CONFIG mode
- Press in RUN mode → Acknowledge the sounding alarm(s) (silent until that alarm clears by default; `ALARM DURATION <secs>` / `ALARM SCOPE` change the policy, `ALARM SILENCE` does it remotely)
- Hold for 2 seconds in RUN mode → Toggle display on/off

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
- **[CAN Sensor Import Guide](docs/guides/sensor-types/CAN_SENSOR_IMPORT_GUIDE.md)** — Import OBD-II data from an ECU on the CAN bus

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
SET <pin> <app> <sensor>         # Configure a sensor (also seeds a default alarm Rule)
SET <pin> UNITS <unit>           # Set display units (CELSIUS, FAHRENHEIT, PSI, BAR, etc.)
SET <pin> NAME <name>            # Set full display label
SET <pin> ABBR <abbr>            # Set short (LCD) label
SET <pin> DIVIDER <ratio>        # Set voltage divider ratio for 5V sensors on 3.3V ADC
CLEAR <pin>                      # Remove a sensor (and any Rules watching it)
SET <pin> INPUT ENABLE          # Enable a disabled sensor
SET <pin> INPUT DISABLE         # Disable a sensor
SAVE                             # Save configuration to EEPROM
```

### Alarms & outputs — the control engine (Rules + Actions)

Alarms and relays are unified into one condition→effect pipeline: a **Rule**
watches an input and (with a severity) annunciates and/or triggers **Actions**.
`SET <pin> APPLICATION` seeds a sensible default alarm Rule automatically.

```
SET <pin> ALARM <min> <max>                                    # per-input alarm: band
SET <pin> ALARM MIN <v> | MAX <v>                              #   or one-sided floor/ceiling
RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>] [WARN <lo|NONE|AUTO>] [INFO|WARNING|ALARM|NONE]   # threshold Rule
RULE ADD <pin> OUTSIDE <lo> <hi> [HYST <w>] [WARN <lo|NONE|AUTO> <hi|NONE|AUTO>] [INFO|WARNING|ALARM|NONE] # both-sided band
RULE LIST | RULE <id> | RULE DEL <id> | RULE RESET <id>         # RESET = app defaults
ACTION ADD RELAY <outpin> [FAILSAFE ON|OFF] [INVERT]           # an actuator (relay/GPIO)
ACTION <id> RULE <ruleId>                                       # drive it from a Rule
ACTION <id> RULE ADD|REMOVE <ruleId>                            # several Rules can drive one Action
ACTION <id> ON|OFF                                             # manual override (RUN-ok)
ACTION LIST | ACTION DEL <id>
```

`WARNING`-severity Rules light the indicator; `ALARM` adds the buzzer; `NONE` is
control-only (drives an Action without annunciating). Limits are always in base
units (°C, bar, V, RPM, km/h), whatever units the input displays in.
`SET <pin> ALARM` is a shortcut facade for a single per-input alarm Rule (band or
one-sided); `RULE`/`ACTION` are the full surface. (The old `RELAY` command is
deprecated and points here.)

### Query (any mode)

```
HELP                             # Show help category overview
HELP <category>                  # Show detailed help (LIST, SET, CALIBRATION, etc.)
HELP QUICK                       # Show compact command reference
LIST INPUTS                      # Show all configured sensors
LIST APPLICATIONS                # Show available application types
LIST SENSORS [category]          # Show available sensor types (optionally filtered)
INFO <pin>                       # Show details for one input
SYSTEM DUMP                      # Show complete system state
SYSTEM DUMP JSON                 # Export the full configuration as JSON
SYSTEM DUMP REGISTRY JSON        # Export full sensor/application catalog as JSON
VERSION                          # Show firmware version
```

### Connectivity

```
TRANSPORT STATUS                 # Show which port carries commands, data and debug
TRANSPORT CONTROL <port>         # Route commands/responses to a port (USB, SERIAL1-8)
TRANSPORT CONTROL <port> SECONDARY  # Add a second port for simultaneous control
AT <n> <command>                 # Send a raw AT command to a module on Serial<n>
OUTPUT <name> ENABLE             # Enable output module (CAN, RealDash, Serial, SD_Log, Alarm)
OUTPUT <name> DISABLE            # Disable output module
```

### Bulk Configuration

```
SYSTEM DUMP JSON                 # Export current config as JSON (for backup or transfer)
JSON IMPORT                      # Stream a JSON config blob over serial to restore it
```

### System

```
CONFIG                           # Enter configuration mode
RUN                              # Enter run mode (start monitoring)
SYSTEM RESET CONFIRM             # Erase all configuration and reboot
DISPLAY STATUS                   # Show display configuration
SYSTEM PINS                      # Show pin allocation status
```

---

## Project Structure

```
preOBD/
├── platformio.ini          # Shared build settings (per-board envs live in boards/)
├── boards/                 # One folder per board: board.h, board.ini, wiring.ini.example
├── src/                    # Firmware source
│   ├── main.cpp            # Setup and main loop
│   ├── config.h            # Application constants (timing, thresholds, defaults)
│   ├── cli/                # Serial command interface
│   ├── inputs/             # Input model and management
│   ├── sensors/            # Sensor library, calibration data and drivers
│   ├── rules/              # Alarm / relay control engine (Rules + Actions)
│   ├── outputs/            # CAN/OBD-II, RealDash, serial, SD, alarm, BLE, display
│   ├── storage/            # EEPROM and JSON configuration
│   ├── comms/, core/, drivers/, hal/, pinctrl/   # Transports, platform and bus layers
│   └── test/               # Test mode (simulated sensors)
├── nrf_firmware/           # Firmware for the optional nRF52840 BLE coprocessor
├── webapp/                 # Web Bluetooth configuration page
├── test/                   # Host-side unit tests
├── tools/                  # Registry generators, validators, helper scripts
└── docs/                   # Documentation
    ├── getting-started/    # Quick reference
    ├── guides/             # Configuration, hardware, sensor and output guides
    ├── reference/          # Command and PID reference
    └── architecture/       # Contributor-facing design docs
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
