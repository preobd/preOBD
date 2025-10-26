# openEMS - Open Engine Monitoring System

**Version 1.0**

Open-source, modular engine monitoring system for classic cars and vehicles without OBDII diagnostics.

## Features

✅ **Modular Architecture** - Enable only the sensors you need  
✅ **Multiple Outputs** - CAN bus, RealDash, Serial, SD logging  
✅ **Standard OBDII PIDs** - Works with existing diagnostic tools  
✅ **Configurable Alarms** - Set thresholds with silence button  
✅ **LCD Display** - Real-time monitoring on 20x4 I2C display  
✅ **Function Pointers** - Clean, extensible code architecture  

## Supported Sensors

**Temperature:**
- MAX6675/MAX31855 K-type thermocouples (CHT, EGT)
- VDO thermistors (coolant, oil, transfer case)
- BME280 (ambient temperature)

**Pressure:**
- VDO pressure sensors (oil, boost)
- Generic MAP sensors
- BME280 (barometric pressure)

**Other:**
- Battery voltage monitoring
- Future: RPM, fuel level, and more

## Quick Start

### 1. Hardware Requirements

**Microcontroller (choose one):**
- Teensy 4.0 (recommended - 14-bit ADC, native CAN)
- Arduino Mega 2560
- Teensy 3.x

**Optional Modules:**
- MCP2515 CAN module (if no native CAN)
- 20x4 I2C LCD display
- SD card module
- BME280 sensor

### 2. Installation

```bash
# Clone or download the project
git clone https://github.com/yourusername/openEMS.git
cd openEMS

# Build with PlatformIO
pio run

# Upload to your board
pio run -t upload

# Monitor serial output
pio device monitor
```

### 3. Configuration

Edit `src/config.h` to enable your sensors and set pins:

```cpp
// Enable the sensors you have
#define ENABLE_CHT
#define ENABLE_EGT
#define ENABLE_COOLANT_TEMP

// Set your pin assignments
#define CHT_INPUT 6
#define EGT_INPUT 7
#define COOLANT_TEMP_INPUT A2

// Enable desired outputs
#define ENABLE_CAN
#define ENABLE_LCD
```

That's it! The system is ready to compile and run.

## Project Structure

```
openEMS/
├── platformio.ini          # Build configuration
├── README.md               # This file
│
├── src/                    # Source code
│   ├── main.cpp           # Main program
│   ├── config.h           # ⚠️ EDIT THIS
│   ├── outputs/           # Output modules (CAN, Serial, etc.)
│   └── displays/          # Display modules (LCD, etc.)
│
└── docs/                   # Documentation
    ├── README.md          # Full documentation
    └── QUICK_REFERENCE.md # Quick lookup guide
```

## Documentation

- **[Full Documentation](docs/README.md)** - Complete guide
- **[Quick Reference](docs/QUICK_REFERENCE.md)** - Common tasks
- **[Directory Setup](docs/DIRECTORY_SETUP.md)** - File organization

## Adding Sensors

Adding new sensors is easy:

1. Write a read function in `sensor_read.cpp`
2. Define the sensor in `sensors.cpp`
3. Enable it in `config.h`

No need to modify core code! See the [documentation](docs/README.md#adding-a-new-sensor) for examples.

## Adding Outputs

Adding output modules is straightforward:

1. Create `output_yourtype.cpp` in `src/outputs/`
2. Register it in `output_manager.cpp`
3. Enable it in `config.h`

See the [SD logging example](src/outputs/output_sdlog.cpp) for reference.

## Supported Outputs

- **CAN Bus** - Standard OBDII PIDs for diagnostic tools
- **RealDash** - Custom dashboard on mobile devices
- **Serial** - Debugging and data logging via USB
- **SD Card** - Local data logging (optional)

## OBDII Compatibility

openEMS uses standard OBDII PIDs, making it compatible with:

- Torque Pro
- RaceChrono
- Harry's Lap Timer
- OBDLink
- Most OBDII scan tools and apps

## Wiring Examples

### Teensy 4.0 Basic Setup

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
  (Use 2.2kΩ pulldown for thermistors)

CAN Bus:
  CAN_H → Vehicle CAN High
  CAN_L → Vehicle CAN Low
  GND   → Vehicle GND

LCD (I2C):
  VCC → 5V
  GND → GND
  SDA → Pin 18 (I2C)
  SCL → Pin 19 (I2C)
```

See [full wiring guide](docs/README.md#wiring) for detailed connections.

## Calibration

### Voltage Reference

Measure your board's internal voltage reference:

```cpp
// In config.h
#define AREF_VOLTAGE 1.065  // Replace with measured value
```

### Sensor Calibration

Modify lookup tables in `sensor_read.cpp` for VDO sensors, or adjust conversion functions.

## Troubleshooting

**Sensor shows NAN:**
- Check wiring and connections
- Verify sensor type matches configuration
- Check pin assignments in config.h

**CAN not working:**
- Verify 120Ω termination resistors
- Check CAN_H and CAN_L connections
- Confirm 500kbps baud rate

**LCD blank:**
- Try I2C address 0x3F instead of 0x27
- Check SDA/SCL connections
- Verify 5V power

See [troubleshooting guide](docs/README.md#troubleshooting) for more help.

## Performance

- **Loop time:** ~93ms with all sensors enabled
- **Memory:** ~2.7KB RAM, ~30KB program space
- **Update rate:** Configurable (default 100ms)

## Contributing

Contributions welcome! Please:

1. Test thoroughly on hardware
2. Document new sensors/features
3. Follow existing code style
4. Update documentation

## License

MIT License - Free for personal and commercial use.

See [LICENSE](LICENSE) file for details.

## Credits

Created by [Your Name] for the classic car community.

Based on years of experience with vehicle diagnostics and open-source examples.

## Support

- **Issues:** [GitHub Issues](https://github.com/yourusername/openEMS/issues)
- **Discussions:** [GitHub Discussions](https://github.com/yourusername/openEMS/discussions)
- **Documentation:** [Full Docs](docs/README.md)

## Version History

**v1.0 (2024)** - Initial modular release
- Function pointer architecture
- Modular output system
- Configuration-driven design
- Support for 11+ sensor types
- Multiple output options

---

