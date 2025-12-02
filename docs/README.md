# openEMS Documentation

**Open Source Engine Monitoring System for Classic Cars**

See [DISCLAIMER](../DISCLAIMER.md) for safety information and software limitations.

---

## Table of Contents

1. [Introduction](#introduction)
2. [System Architecture](#system-architecture)
3. [Hardware Setup](#hardware-setup)
4. [Configuration](#configuration)
5. [Sensor Library](#sensor-library)
6. [Wiring Guide](#wiring-guide)
7. [Outputs](#outputs)
8. [Troubleshooting](#troubleshooting)
9. [Advanced Topics](#advanced-topics)

---

## Introduction

openEMS provides comprehensive engine monitoring for vehicles that lack modern electronic systems. It's designed for classic cars that typically have minimal instrumentation.

**Key Features:**
- Multiple sensor type support (temperature, pressure, voltage, RPM)
- Pre-calibrated sensor library (30+ sensors)
- Flexible configuration (compile-time or runtime)
- Multiple outputs (LCD, CAN, serial, SD logging)
- Arduino Uno to Teensy 4.x compatibility
- Open-source and community-driven

**⚠️ Beta Software Notice:**
This is beta software under active development. Always maintain mechanical backup gauges and thoroughly test before relying on readings for critical engine monitoring.

---

## System Architecture

### Input-Based Architecture

openEMS uses an **input-based system** where each physical pin (Input) is assigned:

1. **Application** - What you're measuring (CHT, OIL_PRESSURE, etc.)
2. **Sensor** - Physical hardware device (VDO sensor, thermocouple, etc.)
3. **Calibration** - Conversion from raw readings to engineering units (automatic from library)

This separates "what you want to measure" from "hardware you're using."

**Example data flow:**
```
VDO Oil Pressure Sensor → Pin A3 → 512 (raw ADC) → 2.5 bar → LCD: "OIL:2.5 BAR"
```

### Configuration Modes

**Compile-Time Configuration:**
- Settings defined in `config.h` at compile time
- Smallest memory footprint (~1.5KB RAM minimum)
- Perfect for Arduino Uno and stable setups
- Configuration is explicit in source code

**Runtime Configuration:**
- Settings configured via serial commands
- Configuration saved to EEPROM
- Change sensors without recompiling
- Ideal for experimentation
- Requires more RAM (~3KB+)

Both modes use identical underlying code - the same Input structure, sensor library, calibration functions, and output modules.

---

## Hardware Setup

### Supported Platforms

| Platform | ADC | Voltage | Max Inputs | CAN | Recommended For |
|----------|-----|---------|------------|-----|-----------------|
| Arduino Uno | 10-bit | 5V | 6 | MCP2515 | Simple setups, compile-time only |
| Arduino Mega | 10-bit | 5V | 16 | MCP2515 | General purpose |
| Teensy 4.0/4.1 | 12-bit | 3.3V | 16 | Native | Best performance, modern features |
| Arduino Due | 12-bit | 3.3V | 16 | Native | High resolution ADC |
| ESP32 | 12-bit | 3.3V | 16 | Native | WiFi capability |

### Critical: Voltage Compatibility

**5V Boards (Arduino Mega, Uno):**
- Use 5V sensors or appropriate level shifting
- VDO sensors work directly (resistive)
- Platform auto-configures 5V voltage dividers

**3.3V Boards (Teensy, Due, ESP32):**
- **GPIO pins are NOT 5V tolerant**
- Applying 5V will permanently damage the board
- Use 3.3V sensors or voltage dividers
- Platform auto-configures 3.3V voltage dividers

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

Required: Pull-down resistor (VDO_BIAS_RESISTOR in config.h) from pin → resistor → GND
```

**VDO Pressure Sensors:**
```
VDO Sensor:
  Signal wire → Analog pin (e.g., A3)
  Ground → Chassis ground (sensor body)

Required: Pull-down resistor (VDO_BIAS_RESISTOR in config.h) from pin → resistor → GND
```

**Voltage Monitoring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → Lower resistor → GND
  - 22kΩ for 3.3V boards (Teensy)
  - 6.8kΩ for 5V boards (Arduino)
```

**CAN Bus:**

*For Teensy with Native FlexCAN:*
```
Teensy Pin 22 (CAN_TX) → CAN transceiver TX
Teensy Pin 23 (CAN_RX) → CAN transceiver RX
Transceiver CAN_H → Vehicle CAN High
Transceiver CAN_L → Vehicle CAN Low
```

*For Boards with MCP2515:*
```
MCP2515 CS → Configurable pin (default: Pin 9)
MCP2515 INT → Configurable pin (default: Pin 2)
MCP2515 SCK/MISO/MOSI → SPI pins
```

CAN bus requires 120Ω termination resistors at both ends.

---

## Configuration

### Compile-Time Mode

**Enable in config.h:**
```cpp
#define USE_STATIC_CONFIG
```

**Configure inputs:**
```cpp
// Input 0: CHT with K-type thermocouple
#define INPUT_0_PIN            6
#define INPUT_0_APPLICATION    CHT
#define INPUT_0_SENSOR         MAX6675

// Input 1: Coolant with VDO sensor
#define INPUT_1_PIN            A2
#define INPUT_1_APPLICATION    COOLANT_TEMP
#define INPUT_1_SENSOR         VDO_120C_LOOKUP

// Input 2: Oil pressure with VDO sensor
#define INPUT_2_PIN            A3
#define INPUT_2_APPLICATION    OIL_PRESSURE
#define INPUT_2_SENSOR         VDO_5BAR
```

### Runtime Mode

**Enable in config.h:**
```cpp
// Leave USE_STATIC_CONFIG commented out
// #define USE_STATIC_CONFIG
```

**Configure via serial commands (115200 baud):**
```
SET 6 APPLICATION CHT
SET 6 SENSOR MAX6675
ENABLE 6

SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2

SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
ENABLE A3

SAVE
```

### Serial Commands Reference

**Configuration:**
```
SET <pin> APPLICATION <app>   - Set measurement type
SET <pin> SENSOR <sensor>     - Set hardware sensor
SET <pin> NAME <n>            - Set short name (3-4 chars)
SET <pin> DISPLAY_NAME <n>    - Set full display name
SET <pin> UNITS <units>       - Set display units
SET <pin> ALARM <min> <max>   - Set alarm thresholds
ENABLE <pin>                  - Enable an input
DISABLE <pin>                 - Disable an input
CLEAR <pin>                   - Remove an input
```

**Query:**
```
LIST INPUTS          - Show all configured inputs
LIST APPLICATIONS    - Show available applications
LIST SENSORS         - Show available sensor types
INFO <pin>           - Show details for one input
HELP                 - Show all commands
```

**System:**
```
SAVE              - Save configuration to EEPROM
LOAD              - Load configuration from EEPROM
RESET             - Clear all configuration (requires confirmation)
RESET CONFIRM     - Confirm configuration reset
```

---

## Sensor Library

openEMS includes 30+ pre-calibrated sensor configurations. See [SENSOR_SELECTION_GUIDE.md](guides/sensor-types/SENSOR_SELECTION_GUIDE.md) for the complete list.

### Thermocouples
| Sensor | Description | Temp Range |
|--------|-------------|------------|
| `MAX6675` | K-Type with MAX6675 amplifier | 0-1024°C |
| `MAX31855` | K-Type with MAX31855 amplifier | -200-1350°C |

### VDO Thermistors
| Sensor | Description | Temp Range |
|--------|-------------|------------|
| `VDO_120C_LOOKUP` | VDO 120°C (lookup table, most accurate) | -40-120°C |
| `VDO_120C_STEINHART` | VDO 120°C (Steinhart-Hart equation) | -40-120°C |
| `VDO_150C_LOOKUP` | VDO 150°C (lookup table) | -40-150°C |
| `VDO_150C_STEINHART` | VDO 150°C (Steinhart-Hart equation) | -40-150°C |

### Pressure Sensors
| Sensor | Description | Range |
|--------|-------------|-------|
| `VDO_2BAR` | VDO 0-2 bar | 0-29 PSI |
| `VDO_5BAR` | VDO 0-5 bar | 0-73 PSI |
| `GENERIC_BOOST` | Generic 0-5V boost sensor | Configurable |
| `MPX4250AP` | Freescale MAP sensor | 20-250 kPa |

### Other Sensors
| Sensor | Description |
|--------|-------------|
| `VOLTAGE_DIVIDER` | Battery voltage monitoring |
| `W_PHASE_RPM` | W-phase alternator RPM |
| `BME280_TEMP` | Environmental temperature |
| `BME280_PRESSURE` | Barometric pressure |
| `BME280_HUMIDITY` | Relative humidity |
| `BME280_ELEVATION` | Altitude estimation |
| `FLOAT_SWITCH` | Digital level switch |

---

## Wiring Guide

### Critical Safety Notes

**⚠️ 3.3V Boards (Teensy, Due, ESP32):**
Never connect 12V signals directly! Always use voltage dividers and protection circuits. The W-phase RPM signal from an alternator can reach 40V+ at high RPM.

**⚠️ W-Phase RPM Circuit:**
See [W_PHASE_RPM_GUIDE.md](guides/sensor-types/W_PHASE_RPM_GUIDE.md) for the required voltage protection circuit. For 3.3V boards, use:
- 22kΩ/4.7kΩ voltage divider
- 3.3V zener diode protection
- 100nF filtering capacitor

### Thermistor Bias Resistors

VDO thermistors require a bias (pull-down) resistor:

| Sensor Type | Bias Resistor |
|-------------|---------------|
| VDO 120C | 1kΩ default (configurable in config.h) |
| VDO 150C | 1kΩ default (configurable in config.h) |
| Generic NTC 10K | 10kΩ |

See [BIAS_RESISTOR_GUIDE.md](../hardware/BIAS_RESISTOR_GUIDE.md)

**Wiring:**
```
VCC (5V or 3.3V) → Thermistor → Analog Pin
                                    ↓
                              Bias Resistor → GND
```

### I2C Addressing

Common I2C addresses:

| Device | Addresses |
|--------|-----------|
| 20x4 LCD | 0x27 or 0x3F |
| BME280 | 0x76 or 0x77 |

---

## Outputs

### LCD Display
- 20x4 character display via I2C
- Rotates through configured sensors
- Shows alarm status

### CAN Bus (OBDII)
- Standard OBDII PIDs
- Compatible with Torque Pro, RaceChrono, etc.
- Custom PID mapping available

### Serial Output
- CSV format at 115200 baud
- Useful for debugging and data logging
- Can connect to PC logging software

### SD Card Logging
- Automatic file creation
- Timestamped entries
- Configurable logging interval

---

## Troubleshooting

### Common Issues

**No readings / stuck at zero:**
1. Check wiring connections
2. Verify correct sensor type selected
3. For thermistors, ensure bias resistor is present
4. Check pin assignments match physical wiring

**Erratic readings:**
1. Add filtering capacitor (100nF) near sensor
2. Check for loose connections
3. Shield wiring from ignition interference
4. Verify power supply stability

**LCD not working:**
1. Check I2C address (try 0x27 and 0x3F)
2. Verify SDA/SCL connections
3. Enable LCD in config.h: `#define ENABLE_LCD`

**CAN bus not communicating:**
1. Check 120Ω termination resistors
2. Verify CAN_H and CAN_L connections
3. Check transceiver power supply
4. Confirm baud rate matches (typically 500kbps)

**EEPROM not saving (runtime mode):**
1. Ensure SAVE command is issued
2. Check for "Configuration saved" confirmation
3. Verify USE_STATIC_CONFIG is NOT defined

---

## Advanced Topics

### Custom Calibrations

See [ADVANCED_CALIBRATION_GUIDE.md](guides/configuration/ADVANCED_CALIBRATION_GUIDE.md) for:
- Creating custom Steinhart-Hart coefficients
- Building lookup tables
- Pressure sensor polynomial fitting
- Contributing calibrations to the library

### Adding New Sensors

See [ADDING_SENSORS.md](guides/configuration/ADDING_SENSORS.md) for:
- Sensor library architecture
- Adding new sensor definitions
- Testing new sensors
- Submitting contributions

### Test Mode

Enable test mode for bench testing without physical sensors:

```cpp
// In advanced_config.h
#define ENABLE_TEST_MODE
#define TEST_MODE_TRIGGER_PIN 5
```

Ground pin 5 at startup to enter test mode, which generates synthetic sensor values for testing outputs.

---

## Performance

**Typical Configuration (8 sensors, CAN, LCD):**
- RAM usage: ~3KB
- Flash usage: ~35KB

**Arduino Uno (minimal, 3-6 sensors):**
- RAM usage: ~1.5-2KB
- Flash usage: ~25-30KB
- Fully functional with compile-time mode

---

## Contributing

Contributions welcome:

1. **Sensor calibrations** - Share your sensor data
2. **Platform testing** - Test on different hardware
3. **Documentation** - Improve clarity
4. **Bug reports** - Help make it more reliable

**Guidelines:**
- Test thoroughly on hardware before submitting
- Document new sensors with datasheets
- Follow existing code style
- Update documentation

---

## License

openEMS is licensed under a custom **MIT + NonCommercial License**.

**Personal/Noncommercial Use:** Free to use, modify, and share
**Commercial Use:** Contact info@preobd.com

---

**For the classic car community.**

**Remember: This is beta software. Test thoroughly and maintain mechanical backup gauges!**
