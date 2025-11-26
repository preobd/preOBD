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

---

## System Architecture

### Input-Based Architecture

openEMS uses an **input-based system** where each physical pin (Input) is assigned:

1. **Application** - What you're measuring (CHT, OIL_PRESSURE, etc.)
2. **Sensor** - Physical hardware device (VDO sensor, thermocouple, etc.)
3. **Calibration** - Conversion from raw readings to engineering units

This separates "what you want to measure" from "hardware you're using."

**Example:**
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

Both modes use identical underlying code.

### Memory Architecture

**Flash (Program Memory):**
- Sensor calibration data stored in PROGMEM
- Lookup tables in flash (not RAM)
- Steinhart-Hart coefficients in flash

**RAM:**
- Input array (~140 bytes per configured input)
- Active sensor readings
- Output buffers

**EEPROM (runtime mode only):**
- Configuration storage
- 4KB on Mega, 1KB on Teensy

### Platform Auto-Detection

openEMS automatically detects and configures:
- System voltage (3.3V or 5V)
- ADC resolution (10-bit or 12-bit)
- Reference voltage
- Voltage dividers for battery monitoring

---

## Hardware Setup

### Supported Microcontrollers

| Platform | Voltage | ADC | RAM | Flash | CAN | Notes |
|----------|---------|-----|-----|-------|-----|-------|
| **Arduino Mega** | 5V | 10-bit | 8KB | 256KB | External | Good I/O, well-supported |
| **Teensy 4.0** | 3.3V | 14-bit | 1MB | 2MB | Native | Best performance |
| **Teensy 3.2** | 3.3V | 12-bit | 64KB | 256KB | Native | Good balance |
| **Arduino Uno** | 5V | 10-bit | 2KB | 32KB | External | Minimal config only |
| **Arduino Due** | 3.3V | 12-bit | 96KB | 512KB | External | Less tested |
| **ESP32** | 3.3V | 12-bit | 520KB | 4MB | Native | Wi-Fi capable, less tested |

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

### Sensor Connections

**Thermocouples (MAX6675/MAX31855):**
```
MAX6675 Module:
  VCC → 5V (or 3.3V for 3.3V boards)
  GND → GND
  SCK → Pin 13 (SPI clock)
  SO  → Pin 12 (SPI MISO)
  CS  → Configurable pin (e.g., Pin 6)
```

**VDO Thermistors (2-wire):**
```
VDO Sensor:
  Terminal 1 → Analog pin (e.g., A2)
  Terminal 2 → GND
  
Required: 2.2kΩ pull-down resistor (pin → resistor → GND)
```

**VDO Pressure Sensors (3-wire):**
```
VDO Sensor:
  Ground → GND
  Sensor → Analog pin (e.g., A3)
  +12V → Vehicle 12V supply

No external resistors needed
```

**Battery Voltage:**
```
Battery + → Voltage divider → Analog pin
Battery - → GND

Auto-configured: 100kΩ/22kΩ for 3.3V, 100kΩ/6.8kΩ for 5V
```

**W-Phase RPM:**
See [W_PHASE_RPM_GUIDE.md](W_PHASE_RPM_GUIDE.md) for voltage protection circuit details.

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

Requires 120Ω termination resistors at both ends of CAN bus.

---

## Configuration

### Compile-Time Mode

**Enable in config.h:**
```cpp
#define USE_STATIC_CONFIG
```

**Configure inputs:**
```cpp
// Input 1: CHT with K-type thermocouple
#define INPUT_1_PIN            6
#define INPUT_1_APPLICATION    CHT
#define INPUT_1_SENSOR         K_TYPE_THERMOCOUPLE_MAX6675

// Input 2: Coolant with VDO sensor
#define INPUT_2_PIN            A2
#define INPUT_2_APPLICATION    COOLANT_TEMP
#define INPUT_2_SENSOR         VDO_120C_LOOKUP

// Input 3: Oil pressure with VDO sensor
#define INPUT_3_PIN            A3
#define INPUT_3_APPLICATION    OIL_PRESSURE
#define INPUT_3_SENSOR         VDO_5BAR_PRESSURE
```

### Runtime Mode

**Enable in config.h:**
```cpp
// Leave USE_STATIC_CONFIG commented out
```

**Configure via serial commands (115200 baud):**
```
SET Pin6 APPLICATION CHT K_TYPE_THERMOCOUPLE_MAX6675
SET A2 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
SET A3 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
SAVE
```

### Serial Commands

**Configuration:**
```
SET <pin> APPLICATION <app> <sensor>  - Configure an input
ENABLE <pin>                          - Enable an input
DISABLE <pin>                         - Disable an input
CLEAR <pin>                           - Remove an input
```

**Query:**
```
LIST INPUTS          - Show all configured inputs
LIST APPLICATIONS    - Show available applications
LIST SENSORS         - Show available sensor types
INFO <pin>           - Show details for one input
```

**System:**
```
SAVE        - Save configuration to EEPROM
LOAD        - Load configuration from EEPROM
RESET       - Clear all configuration
```

---

## Sensor Library

openEMS includes 30+ pre-calibrated sensor configurations. See [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) for the complete list.

### Temperature Sensors

**Thermocouples:**
- `K_TYPE_THERMOCOUPLE_MAX6675` - MAX6675 amplifier
- `K_TYPE_THERMOCOUPLE_MAX31855` - MAX31855 amplifier

**VDO Thermistors:**
- `VDO_120C_LOOKUP` - VDO 120°C sender, lookup table (±0.5°C)
- `VDO_120C_STEINHART` - VDO 120°C sender, Steinhart-Hart (±1°C)
- `VDO_150C_LOOKUP` - VDO 150°C sender, lookup table
- `VDO_150C_STEINHART` - VDO 150°C sender, Steinhart-Hart

**Generic NTC:**
- `GENERIC_NTC_10K_3950` - 10KΩ NTC, β=3950K
- `GENERIC_NTC_10K_3435` - 10KΩ NTC, β=3435K
- `GENERIC_NTC_10K_3380` - 10KΩ NTC, β=3380K

### Pressure Sensors

**VDO:**
- `VDO_5BAR_PRESSURE` - VDO 5-bar (0-73 PSI)
- `VDO_2BAR_PRESSURE` - VDO 2-bar (0-29 PSI)

**Generic Linear:**
- `GENERIC_0_5V_5BAR` - 0.5-4.5V → 0-5 bar
- `GENERIC_0_5V_10BAR` - 0.5-4.5V → 0-10 bar
- `GENERIC_0_5V_100PSI` - 0.5-4.5V → 0-100 PSI

**Specific Models:**
- `MPX4250AP_PRESSURE` - Freescale MPX4250AP (20-250 kPa)

### RPM Sensors

- `W_PHASE_RPM_12_POLE` - 12-pole alternator (most common)
- `W_PHASE_RPM_14_POLE` - 14-pole alternator
- `W_PHASE_RPM_16_POLE` - 16-pole alternator

### Other

- `STANDARD_12V_DIVIDER` - 12V battery monitoring
- `BME280_AMBIENT_TEMPERATURE` - BME280 temperature
- `BME280_BAROMETRIC_PRESSURE` - BME280 pressure
- `BME280_RELATIVE_HUMIDITY` - BME280 humidity
- `BME280_ESTIMATED_ALTITUDE` - BME280 elevation
- `DIGITAL_FLOAT_SWITCH` - Float switch

---

## Wiring Guide

### Wire Selection

- Use automotive-grade TXL or GXL wire (18 AWG minimum)
- Never use solid core wire
- Use proper crimp terminals
- Protect wiring with split loom or conduit
- Route away from hot exhaust and moving parts

### Grounding

- Use single, clean ground point for all sensors
- Connect to engine block or chassis ground
- Star ground topology preferred
- Never daisy-chain sensor grounds

### Noise Reduction

- Route sensor wires away from ignition wires
- Use shielded cable for RPM sensing
- Add 100nF capacitor across sensor inputs
- Twist sensor signal and ground wires together
- Keep sensor wires short (< 3 feet if possible)

---

## Outputs

### LCD Display

**20x4 I2C Character Display**

**Wiring:**
```
LCD VCC → 5V (or 3.3V)
LCD GND → GND
LCD SDA → Pin 18 (I2C SDA)
LCD SCL → Pin 19 (I2C SCL)
```

**Display Format:**
```
CHT:485C  EGT:610C
WTR:92C   OIL:2.5bar
BAT:13.8V RPM:2450
STATUS: OK
```

I2C address is typically 0x27 or 0x3F.

### CAN Bus / OBDII

Compatible with standard OBDII tools:
- Torque Pro
- RaceChrono
- Harry's Lap Timer
- OBDLink scan tools

**Configuration:**
```cpp
#define ENABLE_CAN
// For Teensy:
#define USE_FLEXCAN_NATIVE
```

### Serial Output

**CSV Format:**
```
Sensor,Value,Units
CHT,485.2,C
EGT,610.5,C
```

Baud rate: 115200

**Configuration:**
```cpp
#define ENABLE_SERIAL_OUTPUT
```

### SD Card Logging

Local timestamped CSV logging.

**Configuration:**
```cpp
#define ENABLE_SD_LOGGING
#define SD_CS_PIN 10
```

---

## Troubleshooting

### Sensor Shows NAN

1. Check all connections are secure
2. Verify correct sensor type selected
3. Check pin assignments in config
4. Measure sensor resistance with multimeter

### Wrong Temperature Reading

**If reading is significantly off:**
- Verify correct sensor type (VDO 120C vs 150C)
- Check bias resistor value (should be 2.2kΩ for VDO)
- Measure sensor resistance at known temperature

**If reading is close but consistently off:**
- May need custom calibration
- Check ADC reference voltage

### Pressure Reading Issues

**Zero or negative pressure:**
- Check sensor power supply (needs 12V)
- Verify sensor ground connection
- Measure sensor output voltage directly

**Reading doesn't change:**
- Sensor may be failed or stuck
- Verify sensor type matches configuration

### CAN Bus Not Working

**For Native FlexCAN (Teensy):**
- Verify `USE_FLEXCAN_NATIVE` is defined
- Check CAN transceiver power
- Verify RX/TX connections (Pin 22/23 on Teensy 4.0)

**For MCP2515:**
- Check CS and INT pin assignments
- Verify SPI connections
- Check CAN_H and CAN_L not reversed

**For Both:**
- Check 120Ω termination resistors
- Verify baud rate (usually 500 kbps)

### LCD Display Issues

**Blank display:**
- Check power connections (5V and GND)
- Try I2C address 0x3F instead of 0x27
- Check contrast adjustment pot on LCD

**Garbled display:**
- Wrong I2C address selected
- Loose connections
- Check I2C pullup resistors

### Memory Issues (Arduino Uno)

Use compile-time configuration mode and limit to 6 sensors maximum.

---

## Advanced Topics

### Adding Custom Sensors

See [ADVANCED_CALIBRATION_GUIDE.md](ADVANCED_CALIBRATION_GUIDE.md) for instructions on:
- Creating custom thermistor calibrations
- Adding new sensor types
- Extending the sensor library
- Contributing calibrations

### Performance Optimization

**For maximum speed:**
- Reduce number of sensors
- Optimize loop timing
- Use DMA for displays (Teensy only)

**For minimum power:**
- Use sleep modes between readings
- Reduce display brightness
- Power down unused peripherals

---

## Community and Contributing

### Getting Help

**Before asking:**
1. Read documentation thoroughly
2. Check [QUICK_REFERENCE.md](QUICK_REFERENCE.md)
3. Review troubleshooting section
4. Search GitHub issues

**When asking, provide:**
- Hardware platform
- Configuration mode
- Sensor types being used
- Serial output showing issue
- What you've tried

### Contributing

**Contributions welcome:**
1. Sensor calibrations with datasheets
2. Platform testing and reports
3. Bug reports with reproduction steps
4. Code improvements with hardware testing
5. Documentation improvements

**Please:**
- Test thoroughly on hardware
- Follow existing code style
- Update documentation
- Provide clear explanations

---

## Appendix

### Pin Reference

**Arduino Mega:**
- Analog: A0-A15
- Digital: 2-53
- SPI: 50 (MISO), 51 (MOSI), 52 (SCK)
- I2C: 20 (SDA), 21 (SCL)

**Teensy 4.0:**
- Analog: A0-A13
- Digital: 0-39
- SPI: 12 (MISO), 11 (MOSI), 13 (SCK)
- I2C: 18 (SDA), 19 (SCL)
- CAN: 22 (TX), 23 (RX)

**Arduino Uno:**
- Analog: A0-A5
- Digital: 2-13
- SPI: 12 (MISO), 11 (MOSI), 13 (SCK)
- I2C: A4 (SDA), A5 (SCL)

---

**See [DISCLAIMER](../DISCLAIMER.md) for important safety information.**