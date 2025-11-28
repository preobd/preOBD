# openEMS Quick Reference

**Fast lookup guide for common tasks and configurations**

---

## ⚠️ Safety First

- **Beta software** - use at your own risk
- **Always maintain mechanical backup gauges**
- **Test thoroughly before trusting readings**
- **Monitor your engine actively during testing**

---

## Configuration Quick Start

### Compile-Time Mode (Arduino Uno, Stable Setups)

**In config.h:**
```cpp
// Enable compile-time mode
#define USE_STATIC_CONFIG

// Configure an input (repeat for each sensor)
#define INPUT_0_PIN            6              // Pin number
#define INPUT_0_APPLICATION    CHT            // What you're measuring
#define INPUT_0_SENSOR         MAX6675        // Hardware sensor type
```

### Runtime Mode (Teensy, Mega, Development)

**Leave compile-time mode disabled:**
```cpp
// In config.h - leave this commented out:
// #define USE_STATIC_CONFIG
```

**Configure via serial (115200 baud):**
```
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2
SAVE
```

---

## Common Sensor Configurations

### K-Type Thermocouple (CHT/EGT)

**Compile-Time:**
```cpp
#define INPUT_0_PIN            6
#define INPUT_0_APPLICATION    CHT
#define INPUT_0_SENSOR         MAX6675
```

**Runtime:**
```
SET 6 APPLICATION CHT
SET 6 SENSOR MAX6675
ENABLE 6
SAVE
```

**Wiring:**
```
MAX6675 VCC → 5V (or 3.3V)
MAX6675 GND → GND
MAX6675 SCK → Pin 13 (SPI)
MAX6675 SO  → Pin 12 (SPI MISO)
MAX6675 CS  → Pin 6 (or your configured pin)
```

**Sensor types:**
- `MAX6675` - Standard K-type thermocouple amplifier
- `MAX31855` - Higher accuracy K-type amplifier

### VDO Temperature Sensor (Coolant/Oil)

**Compile-Time:**
```cpp
#define INPUT_1_PIN            A2
#define INPUT_1_APPLICATION    COOLANT_TEMP
#define INPUT_1_SENSOR         VDO_120C_LOOKUP
```

**Runtime:**
```
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2
SAVE
```

**Wiring:**
```
VDO Sensor Signal wire → Analog pin (A2)
VDO Sensor Ground → Chassis ground (sensor body)
Add 1kΩ pull-down resistor: Pin → resistor → GND
```

**Sensor types:**
- `VDO_120C_LOOKUP` - Most accurate for 120°C sensors (±0.5°C)
- `VDO_120C_STEINHART` - Slightly less accurate (±1°C)
- `VDO_150C_LOOKUP` - For 150°C sensors
- `VDO_150C_STEINHART` - For 150°C sensors

### VDO Pressure Sensor (Oil/Boost)

**Compile-Time:**
```cpp
#define INPUT_2_PIN            A3
#define INPUT_2_APPLICATION    OIL_PRESSURE
#define INPUT_2_SENSOR         VDO_5BAR
```

**Runtime:**
```
SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
ENABLE A3
SAVE
```

**Wiring:**
```
VDO Sensor Signal wire → Analog pin (A3)
VDO Sensor Ground → Chassis ground (sensor body)
Add 1kΩ pull-down resistor: Pin → resistor → GND
```

**Sensor types:**
- `VDO_5BAR` - 0-5 bar (0-73 PSI)
- `VDO_2BAR` - 0-2 bar (0-29 PSI)

### Battery Voltage

**Compile-Time:**
```cpp
#define INPUT_3_PIN            A8
#define INPUT_3_APPLICATION    PRIMARY_BATTERY
#define INPUT_3_SENSOR         VOLTAGE_DIVIDER
```

**Runtime:**
```
SET A8 APPLICATION PRIMARY_BATTERY
SET A8 SENSOR VOLTAGE_DIVIDER
ENABLE A8
SAVE
```

**Wiring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → 22kΩ (3.3V boards) or 6.8kΩ (5V boards) → GND
```

**Note:** Platform auto-configures correct divider ratio based on detected board voltage.

### W-Phase RPM (Classic Cars)

**Compile-Time:**
```cpp
#define INPUT_4_PIN            5
#define INPUT_4_APPLICATION    ENGINE_RPM
#define INPUT_4_SENSOR         W_PHASE_RPM
```

**Runtime:**
```
SET 5 APPLICATION ENGINE_RPM
SET 5 SENSOR W_PHASE_RPM
ENABLE 5
SAVE
```

**⚠️ CRITICAL:** See [W_PHASE_RPM_GUIDE.md](../guides/sensor-types/W_PHASE_RPM_GUIDE.md) for voltage protection circuit. Teensy boards (3.3V) require 22kΩ/4.7kΩ divider with 3.3V zener!

### BME280 Environmental Sensor

**Compile-Time:**
```cpp
#define INPUT_5_PIN            0x76  // I2C address
#define INPUT_5_APPLICATION    AMBIENT_TEMP
#define INPUT_5_SENSOR         BME280_TEMP
```

**Runtime:**
```
SET 0x76 APPLICATION AMBIENT_TEMP
SET 0x76 SENSOR BME280_TEMP
ENABLE 0x76
SAVE
```

**Wiring:**
```
BME280 VCC → 3.3V
BME280 GND → GND
BME280 SDA → SDA pin (board-specific)
BME280 SCL → SCL pin (board-specific)
```

**Sensor types:**
- `BME280_TEMP` - Ambient temperature
- `BME280_PRESSURE` - Barometric pressure
- `BME280_HUMIDITY` - Relative humidity
- `BME280_ELEVATION` - Estimated altitude

### Digital Float Switch (Coolant Level)

**Compile-Time:**
```cpp
#define INPUT_6_PIN            7
#define INPUT_6_APPLICATION    COOLANT_LEVEL
#define INPUT_6_SENSOR         FLOAT_SWITCH
```

**Runtime:**
```
SET 7 APPLICATION COOLANT_LEVEL
SET 7 SENSOR FLOAT_SWITCH
ENABLE 7
SAVE
```

---

## Serial Commands Reference

### Configuration Commands

| Command | Description |
|---------|-------------|
| `SET <pin> APPLICATION <app>` | Set measurement type for an input |
| `SET <pin> SENSOR <sensor>` | Set hardware sensor type |
| `SET <pin> NAME <name>` | Set short display name (3-4 chars) |
| `SET <pin> DISPLAY_NAME <name>` | Set full display name |
| `SET <pin> UNITS <units>` | Set display units |
| `SET <pin> ALARM <min> <max>` | Set alarm thresholds |
| `ENABLE <pin>` | Enable an input |
| `DISABLE <pin>` | Disable an input |
| `CLEAR <pin>` | Remove input configuration |

### Query Commands

| Command | Description |
|---------|-------------|
| `LIST INPUTS` | Show all configured inputs |
| `LIST APPLICATIONS` | Show available application types |
| `LIST SENSORS` | Show available sensor types |
| `INFO <pin>` | Show detailed info for one input |
| `HELP` | Show all available commands |

### System Commands

| Command | Description |
|---------|-------------|
| `SAVE` | Save configuration to EEPROM |
| `LOAD` | Load configuration from EEPROM |
| `RESET` | Clear all configuration (requires confirmation) |
| `RESET CONFIRM` | Confirm configuration reset |

### Pin Formats

- Analog pins: `A0`, `A1`, `A2`, ... `A15`
- Digital pins: `0`, `1`, `2`, ... `53`
- I2C addresses: `0x76`, `0x77` (for BME280)

### Available Units

| Unit | Description |
|------|-------------|
| `CELSIUS` or `C` | Temperature in Celsius |
| `FAHRENHEIT` or `F` | Temperature in Fahrenheit |
| `PSI` | Pressure in PSI |
| `BAR` | Pressure in bar |
| `KPA` | Pressure in kilopascals |
| `VOLTS` or `V` | Voltage |
| `RPM` | Revolutions per minute |
| `PERCENT` or `%` | Percentage |
| `METERS` or `M` | Altitude in meters |
| `FEET` or `FT` | Altitude in feet |

---

## Application Types

| Application | Description | Default Sensor |
|-------------|-------------|----------------|
| `CHT` | Cylinder Head Temperature | MAX6675 |
| `EGT` | Exhaust Gas Temperature | MAX31855 |
| `COOLANT_TEMP` | Engine Coolant Temperature | VDO_120C_LOOKUP |
| `OIL_TEMP` | Engine Oil Temperature | VDO_150C_STEINHART |
| `TCASE_TEMP` | Transfer Case Temperature | VDO_150C_STEINHART |
| `OIL_PRESSURE` | Engine Oil Pressure | VDO_5BAR |
| `BOOST_PRESSURE` | Boost/Manifold Pressure | GENERIC_BOOST |
| `FUEL_PRESSURE` | Fuel Pressure | VDO_5BAR |
| `PRIMARY_BATTERY` | Main Battery Voltage | VOLTAGE_DIVIDER |
| `AUXILIARY_BATTERY` | Secondary Battery Voltage | VOLTAGE_DIVIDER |
| `COOLANT_LEVEL` | Coolant Level Switch | FLOAT_SWITCH |
| `AMBIENT_TEMP` | Ambient Temperature | BME280_TEMP |
| `BAROMETRIC_PRESSURE` | Barometric Pressure | BME280_PRESSURE |
| `HUMIDITY` | Relative Humidity | BME280_HUMIDITY |
| `ELEVATION` | Estimated Altitude | BME280_ELEVATION |
| `ENGINE_RPM` | Engine RPM | W_PHASE_RPM |

---

## Sensor Types

### Thermocouples
- `MAX6675` - K-Type thermocouple (MAX6675 amplifier)
- `MAX31855` - K-Type thermocouple (MAX31855 amplifier)

### Thermistors (VDO)
- `VDO_120C_LOOKUP` - VDO 120°C (lookup table)
- `VDO_120C_STEINHART` - VDO 120°C (Steinhart-Hart)
- `VDO_150C_LOOKUP` - VDO 150°C (lookup table)
- `VDO_150C_STEINHART` - VDO 150°C (Steinhart-Hart)

### Pressure Sensors
- `VDO_2BAR` - VDO 0-2 bar
- `VDO_5BAR` - VDO 0-5 bar
- `GENERIC_BOOST` - Generic 0-5V boost sensor
- `MPX4250AP` - Freescale MAP sensor

### Voltage & RPM
- `VOLTAGE_DIVIDER` - Standard 12V battery monitoring
- `W_PHASE_RPM` - W-phase alternator RPM

### Environmental (BME280)
- `BME280_TEMP` - Temperature
- `BME280_PRESSURE` - Barometric pressure
- `BME280_HUMIDITY` - Relative humidity
- `BME280_ELEVATION` - Altitude estimation

### Digital
- `FLOAT_SWITCH` - Digital float switch input

---

## Platform Quick Reference

| Board | ADC | Voltage | Max Inputs | Notes |
|-------|-----|---------|------------|-------|
| Arduino Uno | 10-bit | 5V | 6 | Use compile-time mode |
| Arduino Mega | 10-bit | 5V | 16 | Good all-rounder |
| Teensy 4.0/4.1 | 12-bit | 3.3V | 16 | Best performance, native CAN |
| Arduino Due | 12-bit | 3.3V | 16 | High resolution ADC |
| ESP32 | 12-bit | 3.3V | 16 | WiFi capable |

---

## Common Mistakes

1. **Wrong sensor type** - VDO 120C vs 150C makes huge difference
2. **Missing pull-down resistor** - VDO thermistors need pull-down (typically 1kΩ)
3. **Wrong I2C address** - Try both 0x27 and 0x3F for LCD
4. **5V to 3.3V board** - Will destroy Teensy instantly!
5. **No CAN termination** - CAN bus needs 120Ω resistors
6. **Loose connections** - Vibration in engine bay
7. **Too many sensors on Uno** - Max 6 sensors
8. **Forgetting to SAVE** - Runtime config must be saved to EEPROM

---

## Getting More Help

**Documentation:**
- [Full Documentation](../README.md) - Complete guide
- [Sensor Selection Guide](../guides/sensor-types/SENSOR_SELECTION_GUIDE.md) - Choose sensors
- [Pressure Sensor Guide](../guides/sensor-types/PRESSURE_SENSOR_GUIDE.md) - Pressure specifics
- [Voltage Guide](../guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md) - Battery monitoring
- [RPM Guide](../guides/sensor-types/W_PHASE_RPM_GUIDE.md) - RPM for classics
- [Advanced Calibration](../guides/configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom sensors

**Support:**
- GitHub Issues - Bug reports
- GitHub Discussions - Questions and help
- Search existing issues first

**When asking for help, include:**
- Board type (Mega, Teensy 4.0, etc.)
- Configuration mode (compile-time or runtime)
- Sensor types
- Serial output showing problem
- What you've already tried

---

**Remember: This is beta software. Test thoroughly and maintain mechanical backup gauges!**

**For the classic car community.**
