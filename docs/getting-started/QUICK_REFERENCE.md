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
#define INPUT_1_PIN            6              // Pin number
#define INPUT_1_APPLICATION    CHT            // What you're measuring
#define INPUT_1_SENSOR         K_TYPE_THERMOCOUPLE_MAX6675  // Hardware type
```

### Runtime Mode (Teensy, Mega, Development)

**Leave compile-time mode disabled:**
```cpp
// In config.h - leave this commented out:
// #define USE_STATIC_CONFIG
```

**Configure via serial (115200 baud):**
```
SET A2 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
SET A3 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
SAVE
```

---

## Common Sensor Configurations

### K-Type Thermocouple (CHT/EGT)

**Compile-Time:**
```cpp
#define INPUT_1_PIN            6
#define INPUT_1_APPLICATION    CHT
#define INPUT_1_SENSOR         K_TYPE_THERMOCOUPLE_MAX6675
```

**Runtime:**
```
SET Pin6 APPLICATION CHT K_TYPE_THERMOCOUPLE_MAX6675
```

**Wiring:**
```
MAX6675 VCC → 5V (or 3.3V)
MAX6675 GND → GND
MAX6675 SCK → Pin 13 (SPI)
MAX6675 SO  → Pin 12 (SPI MISO)
MAX6675 CS  → Pin 6 (or your configured pin)
```

### VDO Temperature Sensor (Coolant/Oil)

**Compile-Time:**
```cpp
#define INPUT_2_PIN            A2
#define INPUT_2_APPLICATION    COOLANT_TEMP
#define INPUT_2_SENSOR         VDO_120C_LOOKUP
```

**Runtime:**
```
SET A2 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
```

**Wiring:**
```
VDO Sensor Terminal 1 → Analog pin (A2)
VDO Sensor Terminal 2 → GND
Add 2.2kΩ pull-down resistor: Pin → 2.2kΩ → GND
```

**Choose sensor type:**
- `VDO_120C_LOOKUP` - Most accurate (±0.5°C)
- `VDO_120C_STEINHART` - Slightly less accurate (±1°C)
- `VDO_150C_LOOKUP` - For 150°C sensors
- `VDO_150C_STEINHART` - For 150°C sensors

### VDO Pressure Sensor (Oil/Boost)

**Compile-Time:**
```cpp
#define INPUT_3_PIN            A3
#define INPUT_3_APPLICATION    OIL_PRESSURE
#define INPUT_3_SENSOR         VDO_5BAR_PRESSURE
```

**Runtime:**
```
SET A3 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
```

**Wiring:**
```
VDO Sensor Ground → GND
VDO Sensor Signal → Analog pin (A3)
VDO Sensor +12V   → Vehicle 12V
No external resistors needed
```

**Sensor types:**
- `VDO_5BAR_PRESSURE` - 0-5 bar (0-73 PSI)
- `VDO_2BAR_PRESSURE` - 0-2 bar (0-29 PSI)

### Battery Voltage

**Compile-Time:**
```cpp
#define INPUT_4_PIN            A8
#define INPUT_4_APPLICATION    PRIMARY_BATTERY
#define INPUT_4_SENSOR         STANDARD_12V_DIVIDER
```

**Runtime:**
```
SET A8 APPLICATION PRIMARY_BATTERY STANDARD_12V_DIVIDER
```

**Wiring:**
```
Battery + → 100kΩ → Junction → Analog pin
Junction → 22kΩ (3.3V) or 6.8kΩ (5V) → GND
```

**Automatic:** Platform auto-configures correct divider based on board voltage.

### W-Phase RPM (Classic Cars)

**Compile-Time:**
```cpp
#define INPUT_5_PIN            5
#define INPUT_5_APPLICATION    ENGINE_RPM
#define INPUT_5_SENSOR         W_PHASE_RPM_12_POLE
```

**Runtime:**
```
SET Pin5 APPLICATION ENGINE_RPM W_PHASE_RPM_12_POLE
```

**⚠️ CRITICAL:** See [W_PHASE_RPM_GUIDE.md](../guides/sensor-types/W_PHASE_RPM_GUIDE.md) for voltage protection circuit. Teensy boards require 22kΩ/4.7kΩ divider with 3.3V zener!

**Sensor types:**
- `W_PHASE_RPM_12_POLE` - Most common
- `W_PHASE_RPM_14_POLE` - High-output alternators
- `W_PHASE_RPM_16_POLE` - Some marine/industrial

### BME280 Environmental Sensor

**Compile-Time:**
```cpp
#define INPUT_6_PIN            0x76  // I2C address
#define INPUT_6_APPLICATION    AMBIENT_TEMP
#define INPUT_6_SENSOR         BME280_AMBIENT_TEMPERATURE
```

**Runtime:**
```
SET 0x76 APPLICATION AMBIENT_TEMP BME280_AMBIENT_TEMPERATURE
```

**Wiring:**
```
BME280 VCC → 3.3V
BME280 GND → GND
BME280 SDA → Pin 18 (I2C SDA)
BME280 SCL → Pin 19 (I2C SCL)
```

**Available:**
- `BME280_AMBIENT_TEMPERATURE`
- `BME280_BAROMETRIC_PRESSURE`
- `BME280_RELATIVE_HUMIDITY`
- `BME280_ESTIMATED_ALTITUDE`

---

## Serial Commands Reference

### Configuration Commands

```
SET <pin> APPLICATION <app> <sensor>  Configure input
ENABLE <pin>                          Enable input
DISABLE <pin>                         Disable input
CLEAR <pin>                           Remove input
SET <pin> NAME <name>                 Set short name (e.g. "CHT")
SET <pin> DISPLAY_NAME <name>         Set LCD name
SET <pin> UNITS <unit>                Override display units
```

### Query Commands

```
LIST INPUTS          Show all configured inputs
LIST APPLICATIONS    Show available application types
LIST SENSORS         Show available sensor types
INFO <pin>           Show detailed info for one input
HELP                 Show command help
```

### System Commands

```
SAVE         Save configuration to EEPROM
LOAD         Load configuration from EEPROM
RESET        Clear all configuration
```

---

## Applications (What You're Measuring)

```
CHT                 - Cylinder Head Temperature
EGT                 - Exhaust Gas Temperature
COOLANT_TEMP        - Coolant/Water Temperature
OIL_TEMP            - Oil Temperature
TCASE_TEMP          - Transfer Case Temperature
OIL_PRESSURE        - Oil Pressure
BOOST_PRESSURE      - Turbo Boost Pressure
FUEL_PRESSURE       - Fuel Pressure
PRIMARY_BATTERY     - Main Battery Voltage
AUXILIARY_BATTERY   - Auxiliary Battery Voltage
COOLANT_LEVEL       - Coolant Level (float switch)
AMBIENT_TEMP        - Outside Air Temperature
BAROMETRIC_PRESSURE - Barometric Pressure
HUMIDITY            - Relative Humidity
ELEVATION           - Estimated Altitude
ENGINE_RPM          - Engine RPM
```

---

## Sensor Types (Hardware)

### Temperature
```
K_TYPE_THERMOCOUPLE_MAX6675    MAX6675 K-type TC
K_TYPE_THERMOCOUPLE_MAX31855   MAX31855 K-type TC
VDO_120C_LOOKUP                VDO 120°C (lookup table)
VDO_120C_STEINHART             VDO 120°C (Steinhart-Hart)
VDO_150C_LOOKUP                VDO 150°C (lookup table)
VDO_150C_STEINHART             VDO 150°C (Steinhart-Hart)
GENERIC_NTC_10K_3950           Generic 10K NTC, β=3950K
GENERIC_NTC_10K_3435           Generic 10K NTC, β=3435K
GENERIC_NTC_10K_3380           Generic 10K NTC, β=3380K
BME280_AMBIENT_TEMPERATURE     BME280 ambient temp
```

### Pressure
```
VDO_5BAR_PRESSURE              VDO 5-bar (0-73 PSI)
VDO_2BAR_PRESSURE              VDO 2-bar (0-29 PSI)
GENERIC_0_5V_5BAR              Generic 0.5-4.5V → 0-5 bar
GENERIC_0_5V_10BAR             Generic 0.5-4.5V → 0-10 bar
GENERIC_0_5V_100PSI            Generic 0.5-4.5V → 0-100 PSI
MPX4250AP_PRESSURE             Freescale MPX4250AP MAP
BME280_BAROMETRIC_PRESSURE     BME280 barometric
```

### RPM
```
W_PHASE_RPM_12_POLE            12-pole alternator
W_PHASE_RPM_14_POLE            14-pole alternator
W_PHASE_RPM_16_POLE            16-pole alternator
```

### Other
```
STANDARD_12V_DIVIDER           12V battery monitoring
BME280_RELATIVE_HUMIDITY       BME280 humidity
BME280_ESTIMATED_ALTITUDE      BME280 elevation
DIGITAL_FLOAT_SWITCH           Float switch (NC or NO)
```

---

## Display Units

**Available Units:**
```
CELSIUS / FAHRENHEIT           Temperature
BAR / PSI / KPA                Pressure
VOLTS                          Voltage (fixed)
RPM                            RPM (fixed)
PERCENT                        Humidity (fixed)
METERS / FEET                  Elevation
```

**Set defaults in config.h:**
```cpp
#define DEFAULT_TEMPERATURE_UNITS  CELSIUS
#define DEFAULT_PRESSURE_UNITS     BAR
#define DEFAULT_ELEVATION_UNITS    FEET
```

**Override per sensor (runtime mode):**
```
SET A2 UNITS FAHRENHEIT
SET A3 UNITS PSI
```

---

## Pin Designations

**Analog Pins:**
```
A0, A1, A2, ... A15            Arduino Mega (A0-A15)
A0, A1, A2, ... A13            Teensy 4.0 (A0-A13)
A0, A1, ... A5                 Arduino Uno (A0-A5)
```

**Digital Pins:**
```
Pin2, Pin3, Pin4, ... Pin53    Any digital pin
6, 7, 8, ...                   Alternate format (no "Pin")
```

**SPI Pins (MAX6675/MAX31855):**
```
Pin6, Pin7, etc.               CS (Chip Select) pins
Pin13                          SCK (shared, SPI clock)
Pin12                          MISO (shared, SPI data)
```

**I2C Addresses (BME280):**
```
0x76                           BME280 default address
0x77                           BME280 alternate address
```

---

## Troubleshooting Quick Checks

### Sensor Shows NAN

1. Check wiring - most common issue
2. Verify sensor type matches physical sensor
3. Check pin number is correct
4. Test sensor with multimeter

### Wrong Reading

1. Verify correct sensor type (VDO 120C vs 150C)
2. Check bias resistor value (should be 2.2kΩ)
3. Measure sensor resistance at known temperature
4. Compare to datasheet values

### CAN Not Working

**Native FlexCAN (Teensy):**
- Define `USE_FLEXCAN_NATIVE` in config.h
- Check Pin 22 (TX) and Pin 23 (RX)
- Verify CAN transceiver has power
- Check for 120Ω termination

**MCP2515:**
- Check CS and INT pin assignments
- Verify SPI connections
- Check CAN_H and CAN_L not reversed

### LCD Blank

- Try I2C address 0x3F instead of 0x27
- Check SDA/SCL connections (Pin 18/19)
- Verify 5V power (or 3.3V for 3.3V boards)
- Adjust contrast pot on LCD module

---

## Memory Usage Guidelines

**Arduino Uno (2KB RAM):**
- 3-6 sensors maximum
- Use compile-time configuration
- Disable unused outputs
- Minimize enabled features

**Arduino Mega (8KB RAM):**
- 10-15 sensors comfortably
- Either configuration mode works
- All outputs available

**Teensy 4.0 (1MB RAM):**
- Essentially unlimited sensors (limited by pins)
- Either configuration mode works
- All features available

---

## Build Commands

### PlatformIO

```bash
pio run                        Compile for default board
pio run -e teensy40            Compile for Teensy 4.0
pio run -e megaatmega2560      Compile for Arduino Mega
pio run -t upload              Upload to board
pio device monitor             Open serial monitor (115200 baud)
pio run -t clean               Clean build files
```

### Arduino IDE

1. Open openEMS.ino
2. Tools → Board → Select your board
3. Tools → Port → Select COM port
4. Click Upload button
5. Tools → Serial Monitor (set to 115200 baud)

---

## Platform Auto-Configuration

**System automatically detects:**

| Platform | Voltage | ADC Bits | ADC Max | Battery Divider |
|----------|---------|----------|---------|-----------------|
| Mega     | 5.0V    | 10       | 1023    | 100kΩ / 6.8kΩ   |
| Teensy 4.x | 3.3V  | 12       | 4095    | 100kΩ / 22kΩ    |
| Teensy 3.x | 3.3V  | 12       | 4095    | 100kΩ / 22kΩ    |
| Uno      | 5.0V    | 10       | 1023    | 100kΩ / 6.8kΩ   |
| Due      | 3.3V    | 12       | 4095    | 100kΩ / 22kΩ    |

---

## Typical Configurations

### Minimal (3 sensors, Arduino Uno)

```cpp
#define USE_STATIC_CONFIG
#define ENABLE_LCD
#define ENABLE_SERIAL_OUTPUT

#define INPUT_1_PIN         A0
#define INPUT_1_APPLICATION COOLANT_TEMP
#define INPUT_1_SENSOR      VDO_120C_LOOKUP

#define INPUT_2_PIN         A1
#define INPUT_2_APPLICATION OIL_PRESSURE
#define INPUT_2_SENSOR      VDO_5BAR_PRESSURE

#define INPUT_3_PIN         A2
#define INPUT_3_APPLICATION PRIMARY_BATTERY
#define INPUT_3_SENSOR      STANDARD_12V_DIVIDER
```

### Standard (8 sensors, Arduino Mega/Teensy)

```cpp
// Leave USE_STATIC_CONFIG undefined for runtime mode
#define ENABLE_LCD
#define ENABLE_CAN
#define ENABLE_SERIAL_OUTPUT

// Configure via serial:
SET Pin6 APPLICATION CHT K_TYPE_THERMOCOUPLE_MAX6675
SET Pin7 APPLICATION EGT K_TYPE_THERMOCOUPLE_MAX6675
SET A0 APPLICATION COOLANT_TEMP VDO_120C_LOOKUP
SET A1 APPLICATION OIL_TEMP VDO_120C_LOOKUP
SET A2 APPLICATION OIL_PRESSURE VDO_5BAR_PRESSURE
SET A3 APPLICATION BOOST_PRESSURE VDO_2BAR_PRESSURE
SET A8 APPLICATION PRIMARY_BATTERY STANDARD_12V_DIVIDER
SET Pin5 APPLICATION ENGINE_RPM W_PHASE_RPM_12_POLE
SAVE
```

### Full Feature (Teensy 4.0)

Runtime mode with all outputs enabled:
- CHT, EGT (thermocouples)
- Coolant, oil temps (VDO thermistors)
- Oil pressure, boost (VDO pressure)
- Battery voltage
- RPM (W-phase)
- Ambient conditions (BME280)
- LCD display
- CAN bus output
- RealDash protocol
- SD card logging

---

## Serial Output Format

**CSV Format:**
```
Sensor,Value,Units
CHT,485.2,C
EGT,610.5,C
WTR,92.0,C
OIL,2.5,bar
BAT,13.8,V
RPM,2450,RPM
```

**Baud Rate:** 115200

**Use for:**
- Debugging sensor readings
- Data logging to computer
- Custom dashboards
- Analysis and graphing

---

## Common Mistakes to Avoid

1. **Wrong sensor type** - VDO 120C vs 150C makes huge difference
2. **Missing pull-down resistor** - VDO thermistors need 2.2kΩ
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