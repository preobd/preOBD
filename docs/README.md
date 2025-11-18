# openEMS - Open Source Engine Monitoring System

Open-source, modular engine monitoring system for classic cars and vehicles without OBDII diagnostics.

## Features

✅ **Plug-and-Play Sensors** - 30+ pre-calibrated sensor configurations  
✅ **Modular Architecture** - Enable only the sensors you need  
✅ **Multiple Outputs** - CAN bus, RealDash, Serial, SD logging  
✅ **Standard OBDII PIDs** - Works with existing diagnostic tools  
✅ **Configurable Alarms** - Set thresholds with silence button  
✅ **LCD Display** - Real-time monitoring on 20x4 I2C display  
✅ **Cross-Platform** - Arduino Mega, Teensy 3.x/4.x, Due, ESP32  

## Supported Sensors

**Temperature:**
- K-type thermocouples (MAX6675/MAX31855) - CHT, EGT
- VDO thermistors (120°C, 150°C) - Coolant, oil, transfer case
- Generic NTC thermistors (10K, various β values)
- BME280 ambient temperature

**Pressure:**
- VDO pressure sensors (2-bar, 5-bar) - Oil, boost
- Generic linear sensors (0.5-4.5V)
- Freescale MPX4250AP MAP sensor
- BME280 barometric pressure

**Other:**
- Battery voltage monitoring (auto-configured dividers)
- BME280 humidity
- BME280 elevation estimation

## Quick Start

### 1. Hardware Requirements

**Microcontroller (choose one):**
- **Teensy 4.0** (recommended - 14-bit ADC, native CAN)
- Arduino Mega 2560
- Teensy 3.x
- Arduino Due
- ESP32

**Optional Modules:**
- MCP2515 CAN module (if no native CAN)
- 20x4 I2C LCD display
- SD card module
- BME280 environmental sensor

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

### 3. Configuration (Super Easy!)

Edit `src/config.h` - just pick sensor types from the catalog:

```cpp
// === Enable Outputs ===
#define ENABLE_CAN
#define ENABLE_LCD

// === CHT (Cylinder Head Temperature) ===
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675  // Pick from catalog
#define CHT_INPUT             6                             // Your pin
#define CHT_MIN               -1
#define CHT_MAX               495

// === Coolant Temperature ===
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP              // Pick from catalog
#define COOLANT_TEMP_INPUT    A2                           // Your pin
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100

// === Oil Pressure ===
#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE  VDO_5BAR_PRESSURE        // Pick from catalog
#define OIL_PRESSURE_INPUT        A3                       // Your pin
#define OIL_PRESSURE_MIN          1
#define OIL_PRESSURE_MAX          5
```

**That's it!** The system automatically:
- Loads correct calibration data
- Selects appropriate read function
- Configures conversion functions
- Handles all the math

No more manual calibration calculations!

## Project Structure

```
openEMS/
├── platformio.ini          # Build configuration
├── README.md               # This file
│
├── src/                    # Source code
│   ├── main.cpp           # Main program
│   ├── config.h           # ⚠️ EDIT THIS (easy!)
│   ├── platform.h         # Auto-detects your hardware
│   ├── sensor_library.h   # Sensor catalog (30+ sensors)
│   ├── sensor_configs.h   # Calibration database
│   ├── sensor_types.h     # Data structures
│   ├── sensors.cpp        # Sensor instances
│   ├── sensor_read.cpp    # Reading functions
│   ├── outputs/           # Output modules (CAN, Serial, etc.)
│   └── displays/          # Display modules (LCD, etc.)
│
└── docs/                   # Documentation
    ├── README.md                       # Full documentation
    ├── QUICK_REFERENCE.md              # Quick lookup
    ├── SENSOR_SELECTION_GUIDE.md       # How to pick sensors
    ├── PRESSURE_SENSOR_GUIDE.md        # Pressure sensor details
    ├── VOLTAGE_SENSOR_GUIDE.md         # Voltage monitoring
    ├── ADVANCED_CALIBRATION_GUIDE.md   # Custom sensors
    └── DIRECTORY_SETUP.md              # File organization
```

## Documentation

- **[Sensor Selection Guide](docs/SENSOR_SELECTION_GUIDE.md)** - Pick the right sensor (90% of users)
- **[Quick Reference](docs/QUICK_REFERENCE.md)** - Common tasks and commands
- **[Full Documentation](docs/README.md)** - Complete system guide
- **[Pressure Sensors](docs/PRESSURE_SENSOR_GUIDE.md)** - Pressure sensor details
- **[Voltage Monitoring](docs/VOLTAGE_SENSOR_GUIDE.md)** - Battery and voltage sensing
- **[Advanced Calibration](docs/ADVANCED_CALIBRATION_GUIDE.md)** - Custom sensors (10% of users)

## Example: Complete 4-Sensor Setup

```cpp
// config.h - A complete monitoring system in ~20 lines!

#define ENABLE_CAN
#define ENABLE_LCD

#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675
#define CHT_INPUT             6

#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP
#define COOLANT_TEMP_INPUT    A2

#define ENABLE_OIL_PRESSURE
#define OIL_PRESSURE_SENSOR_TYPE  VDO_5BAR_PRESSURE
#define OIL_PRESSURE_INPUT        A3

#define ENABLE_PRIMARY_BATTERY
#define PRIMARY_BATTERY_SENSOR_TYPE  STANDARD_12V_DIVIDER
#define PRIMARY_BATTERY_INPUT        A8
```

Done! No calibration math, no lookup tables, no Steinhart-Hart coefficients.

## Sensor Catalog Highlights

The system includes 30+ pre-calibrated sensors. Here are some popular ones:

**Temperature:**
- `K_TYPE_THERMOCOUPLE_MAX6675` / `MAX31855`
- `VDO_120C_LOOKUP` / `VDO_120C_STEINHART`
- `VDO_150C_LOOKUP` / `VDO_150C_STEINHART`
- `GENERIC_NTC_10K_3950` (common Amazon sensor)
- `BME280_AMBIENT_TEMPERATURE`

**Pressure:**
- `VDO_5BAR_PRESSURE` / `VDO_2BAR_PRESSURE`
- `GENERIC_0_5V_5BAR` / `GENERIC_0_5V_10BAR`
- `GENERIC_0_5V_100PSI`
- `MPX4250AP_PRESSURE`
- `BME280_BAROMETRIC_PRESSURE`

**Voltage:**
- `STANDARD_12V_DIVIDER` (auto-configured!)
- `STANDARD_24V_DIVIDER`

**Environmental:**
- `BME280_RELATIVE_HUMIDITY`
- `BME280_ESTIMATED_ELEVATION`

See [Sensor Selection Guide](docs/SENSOR_SELECTION_GUIDE.md) for complete catalog.

## Supported Outputs

- **CAN Bus** - Standard OBDII PIDs for diagnostic tools
- **RealDash** - Custom dashboard on mobile devices
- **Serial** - Debugging and data logging via USB
- **SD Card** - Local data logging (optional)
- **LCD Display** - Real-time 20x4 character display

## OBDII Compatibility

openEMS uses standard OBDII PIDs, making it compatible with:

- Torque Pro
- RaceChrono
- Harry's Lap Timer
- OBDLink
- Most OBDII scan tools and apps

## Platform Auto-Configuration

The system automatically detects your hardware and configures:
- ADC resolution (10-bit to 14-bit)
- Reference voltage (1.1V, 3.3V, or 5V)
- Voltage divider ratios
- Optimal ADC settings

**Supported platforms:**
- Arduino Mega 2560 (5V, 10-bit ADC)
- Teensy 3.x (3.3V, 12-bit ADC)
- Teensy 4.x (3.3V, 12-bit ADC)
- Arduino Due (3.3V, 12-bit ADC)
- ESP32 (3.3V, 12-bit ADC)

## Wiring Example - Teensy 4.0

```
Thermocouples (MAX6675):
  VCC → 5V
  GND → GND
  SCK → Pin 13 (SPI)
  SO  → Pin 12 (SPI MISO)
  CS  → Pin 6 (configurable)

VDO Sensors (Thermistors/Pressure):
  Signal → Analog pin (A0-A13)
  Ground → GND
  (2.2kΩ pulldown for thermistors)

CAN Bus:
  CAN_H → Vehicle CAN High
  CAN_L → Vehicle CAN Low
  GND   → Vehicle GND

LCD (I2C):
  VCC → 5V
  GND → GND
  SDA → Pin 18
  SCL → Pin 19

Battery Voltage:
  Battery + → [100kΩ] → Junction → [22kΩ] → GND
                          ↓
                       Analog Pin
                          ↓
                      [100nF Cap]
                          ↓
                        GND
```

See [full wiring guide](docs/README.md#wiring) for detailed connections.

## Performance

- **Loop time:** ~93ms with all sensors enabled
- **Memory:** ~3KB RAM, ~35KB program space
- **Update rate:** Configurable (default 200ms)
- **Sensors:** Up to 20+ simultaneous sensors

## Troubleshooting

**Sensor shows NAN:**
- Check wiring and connections
- Verify sensor type matches physical hardware
- Check pin assignments in config.h

**Wrong values displayed:**
- Verify you picked the correct sensor type from catalog
- Check that physical sensor matches catalog entry
- Use serial output to see raw values

**Platform not detected:**
- Check platform.h supports your board
- Open GitHub issue with board details

See [troubleshooting guide](docs/README.md#troubleshooting) for more help.

## Adding Custom Sensors

Need a sensor not in the catalog?

**Easy way:** Post in GitHub Discussions with your sensor datasheet - community can add it!

**Advanced way:** See [Advanced Calibration Guide](docs/ADVANCED_CALIBRATION_GUIDE.md) to add your own.

## Contributing

Contributions welcome! Especially:

1. **New sensor calibrations** - Help grow the library
2. **Platform support** - Test on new boards
3. **Documentation improvements** - Clarify confusing parts
4. **Bug fixes** - Make it more reliable

Please:
- Test thoroughly on hardware
- Document new sensors/features
- Follow existing code style
- Update documentation

## Credits

Created for the classic car community.

MIT License - Free for personal and commercial use.

See [LICENSE](LICENSE) file for details.

## Support

- **Issues:** [GitHub Issues](https://github.com/yourusername/openEMS/issues)
- **Discussions:** [GitHub Discussions](https://github.com/yourusername/openEMS/discussions)
- **Documentation:** [Full Docs](docs/README.md)
- **Sensor Library:** [Sensor Guide](docs/SENSOR_SELECTION_GUIDE.md)

