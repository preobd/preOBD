# openEMS - Open Engine Monitoring System

Open-source, modular engine monitoring for vehicles without OBDII diagnostics.

## Features

- **Modular sensor support**: Easily add/remove sensors via configuration
- **Multiple output options**: CAN bus, RealDash, Serial debugging
- **Standard OBDII PIDs**: Compatible with existing diagnostic tools
- **Alarm system**: Configurable thresholds with silence button
- **LCD display**: Real-time monitoring on 20x4 I2C display
- **Function pointer architecture**: No switch statements, easier to extend

## Supported Hardware

### Microcontrollers
- Arduino Mega
- Teensy 3.x/4.x (recommended for more analog inputs)
- Arduino Uno (limited analog inputs)

### Sensors

**Temperature Sensors:**
- MAX6675 K-type thermocouple (CHT, EGT)
- MAX31855 K-type thermocouple
- VDO 120°C thermistor (coolant, transfer case)
- VDO 150°C thermistor (oil temp)
- BME280 (ambient temperature)

**Pressure Sensors:**
- VDO 5 bar oil pressure
- VDO 2 bar boost pressure
- Generic MAP sensors (MPX4250AP)
- BME280 (barometric pressure)

**Other:**
- Voltage divider (battery monitoring)

## File Structure

```
openEMS/
├── src/
│   ├── main.cpp                    # Main program loop
│   ├── config.h                    # User configuration (EDIT THIS)
│   ├── sensor_types.h              # Sensor structure definitions
│   ├── sensors.cpp                 # Sensor instance definitions
│   ├── sensor_read.cpp             # All sensor reading functions
│   ├── alarm.cpp                   # Alarm system
│   ├── outputs/
│   │   ├── output_base.h           # Output module interface
│   │   ├── output_manager.cpp      # Output module manager
│   │   ├── output_can.cpp          # CAN bus output
│   │   ├── output_realdash.cpp     # RealDash output
│   │   ├── output_serial.cpp       # Serial debug output
│   │   └── output_sdlog.cpp        # SD card logging
│   └── displays/
│       └── display_lcd.cpp         # LCD display module
└── platformio.ini                  # Build configuration
```

## Quick Start

### 1. Configure Your System

Edit `config.h`:

```cpp
// Enable the sensors you have installed
#define ENABLE_CHT
#define ENABLE_EGT
#define ENABLE_COOLANT_TEMP
// ... etc

// Enable your desired outputs
#define ENABLE_CAN
#define ENABLE_LCD
```

### 2. Assign Pins

In `config.h`, set your pin assignments:

```cpp
#define CHT_INPUT 6
#define EGT_INPUT 7
#define COOLANT_TEMP_INPUT A2
// ... etc
```

### 3. Set Thresholds

Configure alarm thresholds in `config.h`:

```cpp
#define CHT_MAX 495       // °C
#define OIL_PRESSURE_MIN 1  // bar
```

### 4. Compile and Upload

Upload to your microcontroller using Arduino IDE or PlatformIO.

## Adding a New Sensor

### Step 1: Add read function in `sensor_read.cpp`

```cpp
void readMySensor(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    // Process reading...
    ptr->value = processedValue;  // Store in standard units
}
```

### Step 2: Define sensor in `sensors.cpp`

```cpp
#ifdef ENABLE_MY_SENSOR
Sensor mySensor = {
    .input = MY_SENSOR_INPUT,
    .obd2pid = 0xXX,
    .obd2length = 1,
    .value = 0,
    .sensorType = MY_SENSOR_TYPE,
    .abbrName = "SENS",
    .displayName = "My Sensor",
    .displayUnits = CELSIUS,
    .minValue = 0,
    .maxValue = 100,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readMySensor,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif
```

### Step 3: Add to sensor array in `sensors.cpp`

```cpp
Sensor *sensors[] = {
    &CHT,
    &EGT,
    &mySensor,  // Add here
    // ...
};
```

### Step 4: Enable in `config.h`

```cpp
#define ENABLE_MY_SENSOR
#define MY_SENSOR_INPUT A5
```

## Adding a New Output Module

### Step 1: Create output file `output_mymodule.cpp`

```cpp
#include "output_base.h"
#include "config.h"

#ifdef ENABLE_MYMODULE

void initMyModule() {
    // Initialize your output
}

void sendMyModule(Sensor *ptr) {
    // Send sensor data
}

void updateMyModule() {
    // Periodic updates if needed
}

#else
void initMyModule() {}
void sendMyModule(Sensor *ptr) {}
void updateMyModule() {}
#endif
```

### Step 2: Register in `output_manager.cpp`

```cpp
extern void initMyModule();
extern void sendMyModule(Sensor*);
extern void updateMyModule();

OutputModule outputModules[] = {
    #ifdef ENABLE_MYMODULE
    {"MyModule", true, initMyModule, sendMyModule, updateMyModule},
    #endif
    // ... other modules
};
```

### Step 3: Enable in `config.h`

```cpp
#define ENABLE_MYMODULE
```

## Calibration

### Voltage Reference Calibration

Measure your Arduino's internal reference voltage and update in `config.h`:

```cpp
#define AREF_VOLTAGE 1.065  // Measure with multimeter
```

### Sensor Calibration

For sensors with lookup tables (VDO thermistors), you can modify the arrays in `sensor_read.cpp`.

## OBDII PID Reference

The system uses standard and custom OBDII PIDs:

| PID  | Description                  | Format |
|------|------------------------------|--------|
| 0x05 | Engine Coolant Temp          | A-40   |
| 0x33 | Barometric Pressure          | A/10   |
| 0x46 | Ambient Air Temp             | A-40   |
| 0x5C | Oil Temperature              | A-40   |
| 0x6F | Turbo Boost Pressure         | A/10   |
| 0x78 | Exhaust Gas Temperature      | A      |
| 0xC8 | Cylinder Head Temperature    | A      |
| 0xC9 | Transfer Case Oil Temp       | A-40   |
| 0xCA | Engine Oil Pressure          | A/10   |
| 0xCB | Primary Battery Voltage      | A/10   |
| 0xCC | Secondary Battery Voltage    | A/10   |

## Wiring

### Thermocouples (MAX6675/MAX31855)
- VCC → 5V
- GND → GND
- SCK → SPI SCK (Pin 13 on Mega)
- SO → SPI MISO (Pin 50 on Mega)
- CS → Digital pin (configurable)

### Thermistors (VDO)
- Sensor → Analog pin
- 2.2kΩ resistor to ground
- Use internal 1.1V reference for stability

### Pressure Sensors (VDO)
- Red → 5V
- Black → GND
- White/Signal → Analog pin

### Battery Voltage Monitoring
- 100kΩ from battery positive
- Junction → Analog pin
- 6.8kΩ to ground
- 100nF capacitor to ground

## Troubleshooting

**Sensor reads NAN or 0:**
- Check wiring
- Verify sensor type matches configuration
- Check pin assignment

**CAN not working:**
- Verify CAN transceiver wiring
- Check baud rate (500k standard)
- Ensure termination resistors present

**LCD not displaying:**
- Check I2C address (0x27 or 0x3F common)
- Verify SDA/SCL connections
- Ensure 5V power to LCD

## License

MIT License - Free for personal and commercial use

## Contributing

Contributions welcome! Please:
1. Test thoroughly on hardware
2. Document any new sensors or features
3. Follow existing code style
4. Update this README

## Credits

Built for the classic car community. Based on various open-source examples and datasheets.
