# preOBD Documentation

**Open Source Engine Monitoring System for Classic Cars**

See [DISCLAIMER](../DISCLAIMER.md) for safety information and software limitations.

---

## Table of Contents

1. [Introduction](#introduction)
2. [System Architecture](#system-architecture)
3. [Hardware Setup](#hardware-setup)
4. [Configuration](#configuration)
5. [Sensor Library](#sensor-library)
6. [Application Types](#application-types)
7. [Output Modules](#output-modules)
8. [Documentation Index](#documentation-index)
9. [Getting Help](#getting-help)

---

## Introduction

preOBD provides comprehensive engine monitoring for vehicles that lack modern electronic systems. It's designed for classic cars that typically have minimal instrumentation.

**Key Features:**
- Multiple sensor type support (temperature, pressure, voltage, RPM, speed)
- Pre-calibrated sensor library (30+ configurations, including VDO, Smiths, Stewart-Warner, AC Delco, Bosch, Jeep, and generic NTC)
- Flexible runtime configuration via serial commands or Web Bluetooth webapp
- JSON bulk import/export for sensor configuration backup and transfer
- CAN sensor import — pull OBD-II or J1939 data from a modern engine swap's ECU alongside classic gauges
- Multiple outputs: LCD, CAN (OBD-II request/response + broadcast), ELM327 BLE emulator, RealDash, serial, SD logging, relay control
- Web Bluetooth PWA — configure and monitor wirelessly from a browser, no app install required
- Multi-platform support (Teensy 4.1, Teensy 3.6/4.0, ESP32-S3, Arduino Mega)
- Open-source and community-driven

**⚠️ Beta Software Notice:**
This is beta software under active development. Always maintain mechanical backup gauges and thoroughly test before relying on readings for critical engine monitoring.

---

## System Architecture

preOBD uses an **input-based system** where each physical pin (Input) is assigned:

1. **Application** - What you're measuring (CHT, OIL_PRESSURE, etc.)
2. **Sensor** - Physical hardware device (VDO sensor, thermocouple, etc.)
3. **Calibration** - Conversion from raw readings to engineering units (automatic from library)

This separates "what you want to measure" from "hardware you're using."

**Example data flow:**
```
VDO Oil Pressure Sensor → Pin A3 → 512 (raw ADC) → 2.5 bar → LCD: "OIL:2.5 BAR"
```

---

## Hardware Setup

### Supported Platforms

| Platform | ADC | Voltage | CAN Controller | Recommended For |
|----------|-----|---------|----------------|-----------------|
| Teensy 4.1 | 12-bit | 3.3V | FlexCAN (native, transceiver req.) | Best performance, built-in SD |
| Teensy 4.0 | 12-bit | 3.3V | FlexCAN (native, transceiver req.) | Modern features, smaller footprint |
| Teensy 3.6 | 13-bit | 3.3V | FlexCAN (native, transceiver req.) | Older builds |
| ESP32-S3 | 12-bit | 3.3V | TWAI (native, transceiver req.) | BLE / WiFi capability |
| Arduino Mega 2560 | 10-bit | 5V | MCP2515 (SPI) | Prototyping, 5V sensors |

All native CAN peripherals require an external transceiver (MCP2551, SN65HVD230, etc.). Per-platform RAM input limits are derived from EEPROM size — see [EEPROM_STRUCTURE.md](architecture/EEPROM_STRUCTURE.md).

### Critical: Voltage Compatibility

**3.3V Boards (Teensy, Due, ESP32):**
- **GPIO pins are NOT 5V tolerant**
- Applying 5V will permanently damage the board
- Use 3.3V sensors or voltage dividers
- Platform auto-configures 3.3V voltage dividers

**5V Boards (Arduino Mega):**
- Use 5V sensors or appropriate level shifting
- VDO sensors work directly (resistive)
- Platform auto-configures 5V voltage dividers

### Wiring Overview

**Thermocouples (MAX6675/MAX31855):**
```
MAX6675 Module:
  VCC → 5V (or 3.3V for 3.3V boards)
  GND → GND
  SCK → Pin 13 (SPI clock)
  SO  → Pin 12 (SPI MISO)
  CS  → Configurable pin (e.g., Pin 6)
```

**VDO Temperature Sensors:**
```
VDO Sensor:
  Signal wire → Analog pin (e.g., A2)
  Ground → Chassis ground (sensor body)

Required: 100Ω pull-down resistor from pin → GND (see BIAS_RESISTOR_GUIDE.md)
```

**VDO Pressure Sensors:**
```
VDO Sensor:
  Signal wire → Analog pin (e.g., A3)
  Ground → Chassis ground (sensor body)

Required: 100Ω pull-down resistor from pin → GND (see BIAS_RESISTOR_GUIDE.md)
```

**Voltage Monitoring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → Lower resistor → GND
  - 22kΩ for 3.3V boards (Teensy)
  - 6.8kΩ for 5V boards (Arduino)
```

**CAN Bus:**

**⚠️ All CAN functionality requires an external CAN transceiver** (MCP2551, SN65HVD230, etc.)

For complete wiring diagrams, transceiver selection, and troubleshooting, see:
[CAN Transceiver Hardware Guide](guides/hardware/CAN_TRANSCEIVER_GUIDE.md)

*Quick reference - Teensy with Native FlexCAN:*
```
Teensy Pin 22 (CAN_TX) → CAN transceiver TX
Teensy Pin 23 (CAN_RX) → CAN transceiver RX
Transceiver CAN_H → Vehicle CAN High
Transceiver CAN_L → Vehicle CAN Low
```

*Quick reference - Boards with MCP2515:*
```
MCP2515 CS → Configurable pin (default: Pin 9)
MCP2515 INT → Configurable pin (default: Pin 2)
MCP2515 SCK/MISO/MOSI → SPI pins
MCP2515 requires external MCP2551/TJA1050 transceiver
```

CAN bus requires 120Ω termination resistors at both ends.

---

## Configuration

### Serial Commands

The help system is hierarchical and organized by category. Use `HELP` to see all categories, or `HELP <category>` for detailed help:

```
HELP                    # Show all help categories
HELP SET                # Show all SET commands
HELP CALIBRATION        # Show calibration commands
HELP QUICK              # Show compact command reference
```

Configure sensors using the `SET` command:

```
SET <pin> <application> <sensor>
```

**Examples:**
```
SET 6 CHT MAX6675                    # K-type thermocouple on digital pin 6
SET A2 COOLANT_TEMP VDO_120C_TABLE  # VDO 120°C sensor on analog pin A2
SET A3 OIL_PRESSURE VDO_5BAR_CURVE         # VDO 5-bar pressure on analog pin A3
SET A4 OIL_TEMP VDO_150C_STEINHART   # VDO 150°C sensor on analog pin A4
SET A6 PRIMARY_BATTERY VOLTAGE_DIVIDER  # Battery voltage on analog pin A6
```

### Alarm Thresholds

```
SET A2 ALARM 60 120                  # Coolant: alarm if <60°C or >120°C
SET A3 ALARM 0.5 6.0                 # Oil pressure: alarm if <0.5 bar or >6.0 bar
```

### Display Units

```
SET A2 UNITS FAHRENHEIT              # Display coolant in °F
SET A3 UNITS PSI                     # Display pressure in PSI
```

### Persistence

```
SAVE                                 # Save all configuration to EEPROM
LOAD                                 # Reload from EEPROM
RESET                                # Clear all configuration (confirmation required)
```

For complete command reference, see [Serial Commands](reference/SERIAL_COMMANDS.md).

---

## Sensor Library

preOBD includes 30+ pre-calibrated sensor configurations:

### Temperature Sensors

| Sensor | Description | Range |
|--------|-------------|-------|
| `MAX6675` | K-type thermocouple | 0-1024°C |
| `MAX31855` | K-type thermocouple (high accuracy) | -200 to 1350°C |
| `VDO_120C_TABLE` | VDO 120°C sender | -40 to 120°C |
| `VDO_120C_STEINHART` | VDO 120°C sender (faster) | -40 to 120°C |
| `VDO_150C_TABLE` | VDO 150°C sender | -40 to 150°C |
| `VDO_150C_STEINHART` | VDO 150°C sender (faster) | -40 to 150°C |
| `SMITHS_*` | Smiths NTC thermistors | varies |
| `STEWART_WARNER_*` | Stewart-Warner NTC thermistors | varies |
| `AC_DELCO_*` | AC Delco NTC thermistors | varies |
| `BOSCH_NTC_*` | Bosch NTC thermistors | varies |
| `JEEP_4L_*` | Jeep 4.0L gauge sender, Renix CTS | varies |

### Pressure Sensors

| Sensor | Description | Range |
|--------|-------------|-------|
| `VDO_2BAR_CURVE` | VDO 2-bar sender | 0-2 bar (0-29 PSI) |
| `VDO_5BAR_CURVE` | VDO 5-bar sender | 0-5 bar (0-73 PSI) |
| `GENERIC_BOOST` | Generic 0.5-4.5V | Configurable |
| `MPX4250AP` | Freescale MAP sensor | 20-250 kPa |
| `MPX5700AP` | Freescale MAP sensor | 15-700 kPa |

### Other Sensors

| Sensor | Description |
|--------|-------------|
| `VOLTAGE_DIVIDER` | 12V battery monitoring |
| `W_PHASE_RPM` | Alternator W-phase RPM |
| `HALL_SPEED` | Hall effect speed sensor (VDO, OEM, generic) |
| `FLOAT_SWITCH` | Digital level switch |
| `BME280_TEMP` | Ambient temperature |
| `BME280_PRESSURE` | Barometric pressure |
| `BME280_HUMIDITY` | Relative humidity |

For complete catalog, see [Sensor Selection Guide](guides/sensor-types/SENSOR_SELECTION_GUIDE.md).

---

## Application Types

Each input measures a specific application:

| Application | Description | Warmup | Typical Sensor |
|-------------|-------------|--------|----------------|
| `CHT` | Cylinder Head Temperature | 30s | MAX6675 |
| `EGT` | Exhaust Gas Temperature | 20s | MAX31855 |
| `COOLANT_TEMP` | Engine Coolant | 60s | VDO_120C_TABLE |
| `OIL_TEMP` | Engine Oil | 60s | VDO_150C_STEINHART |
| `TCASE_TEMP` | Transfer Case | 60s | VDO_150C_STEINHART |
| `OIL_PRESSURE` | Engine Oil Pressure | 60s | VDO_5BAR_CURVE |
| `BOOST_PRESSURE` | Boost/MAP | 0s | MPX4250AP, MPX5700AP |
| `FUEL_PRESSURE` | Fuel Pressure | 5s | VDO_5BAR_CURVE |
| `PRIMARY_BATTERY` | Main Battery | 1s | VOLTAGE_DIVIDER |
| `AUXILIARY_BATTERY` | Secondary Battery | 2s | VOLTAGE_DIVIDER |
| `ENGINE_RPM` | Engine Speed | 0s | W_PHASE_RPM |
| `VEHICLE_SPEED` | Vehicle Speed | 0s | HALL_SPEED |
| `COOLANT_LEVEL` | Coolant Level | 0s | FLOAT_SWITCH |
| `AMBIENT_TEMP` | Ambient Temperature | 2s | BME280_TEMP |

---

## Output Modules

### LCD Display

20x4 I2C character LCD. Shows sensor values with automatic rotation.

```
OUTPUT LCD ENABLE
OUTPUT LCD DISABLE
DISPLAY STATUS
DISPLAY UNITS TEMP F        # Display temps in Fahrenheit
```

### CAN Bus

OBDII-compatible CAN output for gauges and data loggers.

```
OUTPUT CAN ENABLE
OUTPUT CAN INTERVAL 100     # Send every 100ms
```

### Serial CSV

CSV output for data logging and debugging.

```
OUTPUT Serial ENABLE
OUTPUT Serial INTERVAL 1000 # Output every 1 second
```

### SD Card Logging

Continuous data logging to SD card.

```
OUTPUT SD_Log ENABLE
```

### Alarm Output

Buzzer and LED alarm indication.

```
OUTPUT Alarm ENABLE
OUTPUT Alarm INTERVAL 500   # Check alarms every 500ms
```

### OBD-II Scanner Support

Standard OBD-II request/response for Bluetooth scanners.

```
OUTPUT CAN ENABLE
# Works automatically with ELM327 Bluetooth adapters
# Supports apps: Torque, OBD Fusion, Car Scanner, DashCommand
```

See [OBD-II Scanner Guide](guides/outputs/OBD2_SCANNER_GUIDE.md) for setup.

### Bluetooth (BLE)

preOBD has three BLE connectivity modes:

**1. Web Bluetooth webapp** — A browser-based PWA (installable via GitHub Pages) for wireless sensor configuration and live monitoring. No app install required; works in Chrome and Edge on desktop and Android.

**2. ELM327 AT command emulator** — Emulates an ELM327 OBD adapter over BLE, so OBD-II apps (Torque, OBD Fusion, Car Scanner) can connect directly to preOBD without any hardware adapter.

```
# No configuration needed — enable CAN output and connect your OBD app via BLE
OUTPUT CAN ENABLE
```

**3. HM-10 / UART BLE module** — Transparent serial bridge for the webapp or a serial terminal over BLE. Attach via `AT <port> <command>` for module configuration.

```
AT UART2 AT+BAUD4    # Set HM-10 to 115200 baud
TRANSPORT BLE PRIMARY
```

See [BLUETOOTH_HARDWARE_GUIDE.md](guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md), [DIRECT_BLE_OBD_GUIDE.md](guides/outputs/DIRECT_BLE_OBD_GUIDE.md), and [BLE_GATT_PROFILE.md](architecture/BLE_GATT_PROFILE.md).

---

## Documentation Index

### Getting Started
- [QUICK_REFERENCE.md](getting-started/QUICK_REFERENCE.md) - Command cheat sheet
- [DIRECTORY_SETUP.md](getting-started/DIRECTORY_SETUP.md) - Project file organization

### Configuration Guides
- [CONFIG_RUN_MODE_GUIDE.md](guides/configuration/CONFIG_RUN_MODE_GUIDE.md) - Safe configuration workflow
- [BUILD_CONFIGURATION_GUIDE.md](guides/configuration/BUILD_CONFIGURATION_GUIDE.md) - Compile-time feature flags
- [JSON_CONFIGURATION_GUIDE.md](guides/configuration/JSON_CONFIGURATION_GUIDE.md) - JSON config import/export
- [JSON_QUICK_REFERENCE.md](guides/configuration/JSON_QUICK_REFERENCE.md) - JSON command cheat sheet
- [ADDING_SENSORS.md](guides/configuration/ADDING_SENSORS.md) - Adding new sensor types
- [ADVANCED_CALIBRATION_GUIDE.md](guides/configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom calibrations
- [ALARM_SYSTEM_GUIDE.md](guides/configuration/ALARM_SYSTEM_GUIDE.md) - Alarm state machine

### Sensor Type Guides
- [SENSOR_SELECTION_GUIDE.md](guides/sensor-types/SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [THERMOCOUPLE_GUIDE.md](guides/sensor-types/THERMOCOUPLE_GUIDE.md) - MAX6675/MAX31855
- [THERMISTOR_GUIDE.md](guides/sensor-types/THERMISTOR_GUIDE.md) - Resistive temperature sensors (NTC, VDO senders)
- [PRESSURE_SENSOR_GUIDE.md](guides/sensor-types/PRESSURE_SENSOR_GUIDE.md) - Pressure sensors (VDO, MPX, generic)
- [W_PHASE_RPM_GUIDE.md](guides/sensor-types/W_PHASE_RPM_GUIDE.md) - RPM for classic cars
- [HALL_SPEED_GUIDE.md](guides/sensor-types/HALL_SPEED_GUIDE.md) - Vehicle speed sensing
- [VOLTAGE_SENSOR_GUIDE.md](guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md) - Voltage monitoring
- [DIGITAL_SENSOR_GUIDE.md](guides/sensor-types/DIGITAL_SENSOR_GUIDE.md) - Float switches
- [BME280_GUIDE.md](guides/sensor-types/BME280_GUIDE.md) - BME280 environmental sensor
- [CAN_SENSOR_IMPORT_GUIDE.md](guides/sensor-types/CAN_SENSOR_IMPORT_GUIDE.md) - Import sensors from CAN bus

### Output Guides
- [REALDASH_SETUP_GUIDE.md](guides/outputs/REALDASH_SETUP_GUIDE.md) - RealDash dashboard
- [OBD2_SCANNER_GUIDE.md](guides/outputs/OBD2_SCANNER_GUIDE.md) - ELM327 / Torque setup
- [DIRECT_BLE_OBD_GUIDE.md](guides/outputs/DIRECT_BLE_OBD_GUIDE.md) - Direct BLE OBD adapters
- [RELAY_CONTROL.md](guides/outputs/RELAY_CONTROL.md) - Relay control outputs

### Hardware Guides
- [CAN_TRANSCEIVER_GUIDE.md](guides/hardware/CAN_TRANSCEIVER_GUIDE.md) - CAN transceiver selection and wiring
- [BLUETOOTH_HARDWARE_GUIDE.md](guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md) - BLE module selection and wiring
- [BIAS_RESISTOR_GUIDE.md](guides/hardware/BIAS_RESISTOR_GUIDE.md) - Resistor selection
- [LED_INDICATOR_GUIDE.md](guides/hardware/LED_INDICATOR_GUIDE.md) - Status LED wiring
- [PIN_REQUIREMENTS_GUIDE.md](guides/hardware/PIN_REQUIREMENTS_GUIDE.md) - Pin validation

### Reference
- [SERIAL_COMMANDS.md](reference/SERIAL_COMMANDS.md) - Complete command reference
- [OBD2_PID_REFERENCE.md](reference/OBD2_PID_REFERENCE.md) - OBD-II PID catalog

### Architecture (Contributors)
- [REGISTRY_SYSTEM.md](architecture/REGISTRY_SYSTEM.md) - Hash-based sensor lookups
- [EEPROM_STRUCTURE.md](architecture/EEPROM_STRUCTURE.md) - Memory layout and versioning
- [TRANSPORT_ARCHITECTURE.md](architecture/TRANSPORT_ARCHITECTURE.md) - Multi-transport serial communication
- [CAN_HAL_ARCHITECTURE.md](architecture/CAN_HAL_ARCHITECTURE.md) - CAN controller abstraction and hybrid mode
- [BLE_GATT_PROFILE.md](architecture/BLE_GATT_PROFILE.md) - BLE GATT service definitions

---

## Getting Help

**File organization questions:**
- Check [DIRECTORY_SETUP.md](getting-started/DIRECTORY_SETUP.md)

**Configuration questions:**
- Check [QUICK_REFERENCE.md](getting-started/QUICK_REFERENCE.md)
- Check the relevant sensor guide
- Post in GitHub Discussions

**Bug reports:**
- GitHub Issues
- Include firmware version (`VERSION` command)
- Include your configuration (`DUMP` command)