# Quick Reference Card

## File Quick Guide

| File | Purpose | Edit This? |
|------|---------|------------|
| **config.h** | Enable sensors, set pins, thresholds | ✅ YES |
| **main.cpp** | Main program loop | ⚠️ Rarely |
| **sensors.cpp** | Sensor definitions | ⚠️ When adding sensors |
| **sensor_read.cpp** | Sensor reading functions | ⚠️ When adding sensors |
| **sensor_types.h** | Data structures | ❌ No |
| **output_*.cpp** | Output modules | ⚠️ When adding outputs |
| **display_lcd.cpp** | LCD display | ⚠️ To customize display |
| **alarm.cpp** | Alarm system | ⚠️ To customize alarms |

## Common Tasks

### Enable a Sensor

1. Edit `config.h`:
```cpp
#define ENABLE_CHT
#define CHT_INPUT 6
#define CHT_MAX 495
```

### Disable a Sensor

Comment out in `config.h`:
```cpp
//#define ENABLE_CHT
```

### Change Display Units

In `sensors.cpp`, find your sensor:
```cpp
.displayUnits = FAHRENHEIT,  // or CELSIUS, PSI, BAR, etc.
```

### Enable CAN Output

In `config.h`:
```cpp
#define ENABLE_CAN
```

### Enable Multiple Outputs

In `config.h`:
```cpp
#define ENABLE_CAN
#define ENABLE_LCD
#define ENABLE_SERIAL_OUTPUT
```

### Change Alarm Thresholds

In `config.h`:
```cpp
#define CHT_MAX 495
#define OIL_PRESSURE_MIN 1
```

### Calibrate Voltage Reading

In `config.h`:
```cpp
#define AREF_VOLTAGE 1.065  // Measure with multimeter
```

## Sensor Value Storage

| Sensor Type | Stored As | Display Conversion | OBDII Conversion |
|-------------|-----------|-------------------|------------------|
| Temperature | Celsius | C or F | A-40 |
| Pressure | bar | bar, psi, or kPa | A/10 |
| Voltage | volts | V | A/10 |
| Thermocouple | Celsius | C or F | Direct (A) |

## Pin Assignments Reference

### Arduino Mega / Teensy 4.0

| Pin | Purpose | Notes |
|-----|---------|-------|
| A0-A7 | Analog sensors | Thermistors, pressure |
| A8-A15 | Additional analog | Teensy only |
| 2 | CAN INT | Interrupt pin |
| 3 | Buzzer | PWM output |
| 4 | Silence button | Digital input |
| 6 | CHT thermocouple CS | SPI chip select |
| 7 | EGT thermocouple CS | SPI chip select |
| 9 | CAN CS | SPI chip select |
| 13 | SPI SCK | Hardware SPI |
| 50 | SPI MISO | Mega only |
| 51 | SPI MOSI | Mega only |
| SDA | I2C Data | LCD, BME280 |
| SCL | I2C Clock | LCD, BME280 |

## OBDII PID Quick Reference

| PID | Sensor | Format | Example |
|-----|--------|--------|---------|
| 0x05 | Coolant Temp | A-40 | 80°C = 120 |
| 0x33 | Baro Pressure | A/10 | 101kPa = 1010 |
| 0x46 | Ambient Temp | A-40 | 25°C = 65 |
| 0x5C | Oil Temp | A-40 | 90°C = 130 |
| 0x6F | Boost Pressure | A/10 | 1.5bar = 15 |
| 0x78 | EGT | A | 600°C = 600 |
| 0xC8 | CHT | A | 495°C = 495 |
| 0xC9 | Transfer Case | A-40 | 75°C = 115 |
| 0xCA | Oil Pressure | A/10 | 4.5bar = 45 |
| 0xCB | Battery 1 | A/10 | 13.8V = 138 |
| 0xCC | Battery 2 | A/10 | 13.5V = 135 |

## Troubleshooting Quick Checks

### Sensor Shows NAN
- [ ] Check wiring
- [ ] Verify pin number in config.h
- [ ] Check sensor type matches actual hardware
- [ ] Verify power to sensor

### LCD Not Working
- [ ] Check I2C address (0x27 or 0x3F)
- [ ] Verify SDA/SCL connections
- [ ] Check 5V power to LCD
- [ ] Enable with `#define ENABLE_LCD`

### CAN Not Working
- [ ] Verify wiring (CANH, CANL, GND)
- [ ] Check 120Ω termination resistors
- [ ] Verify baud rate (500kbps)
- [ ] Check CAN_CS and CAN_INT pins
- [ ] Enable with `#define ENABLE_CAN`

### Alarm Not Triggering
- [ ] Verify thresholds in config.h
- [ ] Check alarm enabled on sensor:
```cpp
.alarm = true,
```
- [ ] Verify buzzer connected to pin 3
- [ ] Check value is outside min/max

### Wrong Values Displayed
- [ ] Verify sensor type in sensors.cpp
- [ ] Check calibration constants
- [ ] Verify AREF_VOLTAGE setting
- [ ] Compare to known good values
- [ ] Enable serial output to see raw values

## Compilation Errors

### "Sensor not declared"
- Add `#define ENABLE_SENSORNAME` in config.h

### "Multiple definition"
- Check for duplicate sensor definitions
- Ensure only one main.cpp

### "undefined reference"
- Add missing library to platformio.ini or Arduino IDE
- Check all function declarations match

### "not enough memory"
- Disable unused sensors/outputs
- Reduce sensor array size
- Use simpler output methods

## Serial Commands

Enable serial output:
```cpp
#define ENABLE_SERIAL_OUTPUT
```

Connect at 115200 baud. Output format:
```
Sensor,Value,Units
CHT,485.2,C
EGT,610.5,C
WTR,92.0,C
```

## Adding Custom Code

### Add Sensor Read Function

In `sensor_read.cpp`:
```cpp
void readMyNewSensor(Sensor *ptr) {
    // Read hardware
    int reading = analogRead(ptr->input);
    
    // Process
    float value = reading * 0.1;  // Your conversion
    
    // Store in standard units
    ptr->value = value;
}
```

### Define New Sensor

In `sensors.cpp`:
```cpp
#ifdef ENABLE_MY_SENSOR
Sensor mySensor = {
    .input = MY_SENSOR_INPUT,
    .obd2pid = 0xCD,
    .obd2length = 1,
    .value = 0,
    .sensorType = MY_TYPE,
    .abbrName = "MY",
    .displayName = "My Custom Sensor",
    .displayUnits = CELSIUS,
    .minValue = 0,
    .maxValue = 100,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readMyNewSensor,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif
```

### Add to Array

In `sensors.cpp`:
```cpp
Sensor *sensors[] = {
    &CHT,
    &EGT,
    &mySensor,  // Add here
    // ...
};
```

### Enable in Config

In `config.h`:
```cpp
#define ENABLE_MY_SENSOR
#define MY_SENSOR_INPUT A5
#define MY_SENSOR_MAX 100
```

## Performance Tips

- Keep `LOOP_DELAY_MS` at 100ms or higher for LCD readability
- Disable unused outputs to reduce loop time
- Use `#define ENABLE_SERIAL_OUTPUT` for debugging only
- SD card writes should be throttled
- Don't read sensors more frequently than their update rate

## Common Constants

```cpp
// SPI Modes
SPI_MODE0  // Most thermocouples

// Analog Reference (Arduino Mega)
INTERNAL1V1  // 1.1V for voltage sensing

// I2C Addresses
0x27  // Common LCD address
0x3F  // Alternate LCD address
0x76  // BME280 address (alt: 0x77)

// CAN IDs
0x7E8  // Standard ECU response ID
0x0C80  // RealDash frame ID
```

## Build Commands

### PlatformIO
```bash
pio run                    # Compile
pio run -t upload          # Upload
pio device monitor         # Open serial monitor
pio run -e teensy40        # Build for specific board
```

### Arduino IDE
1. Select board: Tools → Board
2. Select port: Tools → Port
3. Click Upload button

## Quick Diagnostic

Serial output will show:
```
=== Engine Monitoring System ===
Initializing...
BME280 initialized
LCD initialized
CAN initialized
Initialization complete!
Active sensors: 11
```

If a module fails to initialize, it will show an error.
