# Quick Reference Card

## File Quick Guide

| File | Purpose | Edit This? |
|------|---------|------------|
| **config.h** | Pick sensors, set pins | ✅ YES (super easy!) |
| **sensor_library.h** | Sensor catalog (read-only) | 👀 Browse for IDs |
| **main.cpp** | Main program loop | ⚠️ Rarely |
| **platform.h** | Auto-detects hardware | ❌ No (automatic) |
| **sensor_configs.h** | Calibration database | ⚠️ To add new sensors |
| **sensors.cpp** | Sensor definitions | ⚠️ When adding sensors |
| **sensor_read.cpp** | Sensor reading functions | ❌ No (uses library) |
| **sensor_types.h** | Data structures | ❌ No |
| **output_*.cpp** | Output modules | ⚠️ When adding outputs |
| **display_lcd.cpp** | LCD display | ⚠️ To customize display |
| **alarm.cpp** | Alarm system | ⚠️ To customize alarms |

## Common Tasks

### Enable a Sensor

1. Look up sensor type in `sensor_library.h`
2. Edit `config.h`:

```cpp
#define ENABLE_CHT
#define CHT_SENSOR_TYPE       K_TYPE_THERMOCOUPLE_MAX6675  // From catalog
#define CHT_INPUT             6                             // Your pin
#define CHT_MIN               -1
#define CHT_MAX               495
```

**That's it!** Calibration is automatic.

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

// For Teensy boards, choose CAN implementation:
#define USE_FLEXCAN_NATIVE  // Use built-in FlexCAN
// OR leave undefined to use external MCP2515
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

## Sensor Catalog Quick Reference

### Temperature Sensors

| What You Have | Sensor ID to Use |
|---------------|------------------|
| MAX6675 thermocouple | `K_TYPE_THERMOCOUPLE_MAX6675` |
| MAX31855 thermocouple | `K_TYPE_THERMOCOUPLE_MAX31855` |
| VDO 120°C sensor (most accurate) | `VDO_120C_LOOKUP` |
| VDO 120°C sensor (faster) | `VDO_120C_STEINHART` |
| VDO 150°C sensor (most accurate) | `VDO_150C_LOOKUP` |
| VDO 150°C sensor (faster) | `VDO_150C_STEINHART` |
| Generic 10K NTC (Amazon) | `GENERIC_NTC_10K_3950` |
| BME280 ambient temp | `BME280_AMBIENT_TEMPERATURE` |

### Pressure Sensors

| What You Have | Sensor ID to Use |
|---------------|------------------|
| VDO 5-bar oil pressure | `VDO_5BAR_PRESSURE` |
| VDO 2-bar boost | `VDO_2BAR_PRESSURE` |
| Generic 0-5 bar linear | `GENERIC_0_5V_5BAR` |
| Generic 0-10 bar linear | `GENERIC_0_5V_10BAR` |
| Generic 0-100 psi linear | `GENERIC_0_5V_100PSI` |
| Freescale MPX4250AP | `MPX4250AP_PRESSURE` |
| BME280 baro pressure | `BME280_BAROMETRIC_PRESSURE` |

### Voltage Sensors

| What You Have | Sensor ID to Use |
|---------------|------------------|
| 12V battery (any board) | `STANDARD_12V_DIVIDER` * |
| 24V truck battery | `STANDARD_24V_DIVIDER` |

\* Automatically uses correct divider for your board!

### Environmental Sensors

| What You Have | Sensor ID to Use |
|---------------|------------------|
| BME280 temperature | `BME280_AMBIENT_TEMPERATURE` |
| BME280 pressure | `BME280_BAROMETRIC_PRESSURE` |
| BME280 humidity | `BME280_RELATIVE_HUMIDITY` |
| BME280 elevation | `BME280_ESTIMATED_ELEVATION` |

## Sensor Value Storage

| Sensor Type | Stored As | Display Conversion | OBDII Conversion |
|-------------|-----------|-------------------|------------------|
| Temperature | Celsius | C or F | A-40 |
| Pressure | bar | bar, psi, or kPa | A/10 |
| Voltage | volts | V | A/10 |
| Thermocouple | Celsius | C or F | Direct (A) |
| Humidity | percent | % | A*2.55 |
| Altitude | meters | m or ft | Direct (A) |

## Pin Assignments Reference

### Arduino Mega / Teensy (MCP2515 CAN)

| Pin | Purpose | Notes |
|-----|---------|-------|
| A0-A7 | Analog sensors | Thermistors, pressure |
| A8-A15 | Additional analog | Teensy only |
| 2 | CAN INT | MCP2515 interrupt pin |
| 3 | Buzzer | PWM output |
| 4 | Silence button | Digital input |
| 6 | CHT thermocouple CS | SPI chip select |
| 7 | EGT thermocouple CS | SPI chip select |
| 9 | CAN CS | MCP2515 chip select |
| 12 | SPI MISO | Hardware SPI |
| 13 | SPI SCK | Hardware SPI |
| 50 | SPI MISO | Mega only |
| 51 | SPI MOSI | Mega only |
| SDA | I2C Data | LCD, BME280 |
| SCL | I2C Clock | LCD, BME280 |

### Teensy Native FlexCAN Pins

| Board | CAN1 TX | CAN1 RX | CAN2 TX | CAN2 RX | CAN3 TX | CAN3 RX |
|-------|---------|---------|---------|---------|---------|---------|
| Teensy 4.0 | 22 | 23 | 0 | 1 | 31 | 30 |
| Teensy 4.1 | 22 | 23 | 0 | 1 | 31 | 30 |
| Teensy 3.2 | 3 | 4 | - | - | - | - |
| Teensy 3.5 | 3 | 4 | - | - | - | - |
| Teensy 3.6 | 3 | 4 | 33 | 34 | - | - |

**Note:** Native FlexCAN requires external CAN transceiver (MCP2562, SN65HVD230, etc.)

## OBDII PID Quick Reference

| PID | Sensor | Format | Example |
|-----|--------|--------|---------|
| 0x05 | Coolant Temp | A-40 | 80°C = 120 |
| 0x33 | Baro Pressure | A/10 | 101kPa = 1010 |
| 0x46 | Ambient Temp | A-40 | 25°C = 65 |
| 0x5C | Oil Temp | A-40 | 90°C = 130 |
| 0x6F | Boost Pressure | A/10 | 1.5bar = 15 |
| 0x78 | EGT | A | 600°C = 600 |
| 0xA0 | Humidity | A/2.55 | 50% = 128 |
| 0xA1 | Altitude | A (MSB), B (LSB) | 1000m |
| 0xC8 | CHT | A | 495°C = 495 |
| 0xC9 | Transfer Case | A-40 | 75°C = 115 |
| 0xCA | Oil Pressure | A/10 | 4.5bar = 45 |
| 0xCB | Battery 1 | A/10 | 13.8V = 138 |
| 0xCC | Battery 2 | A/10 | 13.5V = 135 |

## Troubleshooting Quick Checks

### Sensor Shows NAN
- [ ] Check wiring
- [ ] Verify pin number in config.h
- [ ] **NEW: Check sensor type matches actual hardware**
- [ ] Verify power to sensor

### Wrong Values (But Not NAN)
- [ ] **NEW: Verify sensor ID is correct**
- [ ] Check you picked the right sensor from catalog
- [ ] For VDO sensors: lookup vs Steinhart won't be identical
- [ ] Enable serial output to see raw values

### LCD Not Working
- [ ] Check I2C address (0x27 or 0x3F)
- [ ] Verify SDA/SCL connections
- [ ] Check 5V power to LCD
- [ ] Enable with `#define ENABLE_LCD`

### CAN Not Working
- [ ] Verify wiring (CANH, CANL, GND)
- [ ] Check 120Ω termination resistors
- [ ] Verify baud rate (500kbps)
- [ ] **Native FlexCAN:** Check transceiver is powered, pins 22/23 connected
- [ ] **Native FlexCAN:** Check `USE_FLEXCAN_NATIVE` is defined
- [ ] **MCP2515:** Check CAN_CS (pin 9) and CAN_INT (pin 2) pins
- [ ] **MCP2515:** Verify `USE_FLEXCAN_NATIVE` is NOT defined
- [ ] Enable with `#define ENABLE_CAN`
- [ ] Check serial monitor for init message

### Alarm Not Triggering
- [ ] Verify thresholds in config.h
- [ ] Check alarm enabled on sensor:
```cpp
.alarm = true,
```
- [ ] Verify buzzer connected to pin 3
- [ ] Check value is outside min/max

### Platform Not Detected Correctly
- [ ] Check serial output for detected platform
- [ ] Verify board selection in platformio.ini
- [ ] Check platform.h supports your board

## Compilation Errors

### "Sensor type not found" or similar
- Check sensor ID spelling in config.h
- Verify ID exists in sensor_library.h

### "Sensor not declared"
- Add `#define ENABLE_SENSORNAME` in config.h

### "undefined reference to getSensorConfig"
- Make sure sensor_configs.h is included
- Check that sensor ID is in SENSOR_CONFIGS array

### "not enough memory"
- Disable unused sensors/outputs
- Reduce sensor array size
- Use simpler output methods
- Use Steinhart method instead of lookup tables

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

## Configuration Pattern

```cpp
// Step 1: Enable the sensor
#define ENABLE_[SENSOR_NAME]

// Step 2: Pick sensor type from catalog
#define [SENSOR_NAME]_SENSOR_TYPE  [ID_FROM_CATALOG]

// Step 3: Assign pin
#define [SENSOR_NAME]_INPUT  [PIN]

// Step 4: Set thresholds (optional)
#define [SENSOR_NAME]_MIN  [value]
#define [SENSOR_NAME]_MAX  [value]
```

**Example:**
```cpp
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_LOOKUP
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100
```

## Advanced: Custom Calibration Override

If you need different calibration than preset:

```cpp
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   CUSTOM_THERMISTOR_STEINHART
#define COOLANT_TEMP_INPUT    A2

// Override with custom calibration
#define COOLANT_CUSTOM_CALIBRATION
#define COOLANT_BIAS_RESISTOR     470.0
#define COOLANT_STEINHART_A       1.299e-3
#define COOLANT_STEINHART_B       2.401e-4
#define COOLANT_STEINHART_C       1.301e-7
```

See [Advanced Calibration Guide](ADVANCED_CALIBRATION_GUIDE.md) for details.

## Platform Auto-Configuration

The system automatically detects and configures:

**Arduino Mega:**
- 5V system, 10-bit ADC (0-1023)
- 5V reference voltage
- 100kΩ/6.8kΩ battery divider

**Teensy 3.x/4.x:**
- 3.3V system, 12-bit ADC (0-4095)
- 3.3V reference voltage
- 100kΩ/22kΩ battery divider
- ADC averaging enabled

**ESP32:**
- 3.3V system, 12-bit ADC (0-4095)
- 3.3V reference voltage
- 11dB attenuation (0-3.3V range)

**Arduino Due:**
- 3.3V system, 12-bit ADC (0-4095)
- 3.3V reference voltage

## Build Commands

### PlatformIO
```bash
pio run                    # Compile
pio run -t upload          # Upload
pio device monitor         # Open serial monitor
pio run -e teensy40        # Build for specific board
pio run -e megaatmega2560  # Build for Arduino Mega
```

### Arduino IDE
1. Select board: Tools → Board
2. Select port: Tools → Port
3. Click Upload button

## Quick Diagnostic Output

Serial output will show:
```
=== openEMS v2.0 ===
Open Engine Monitoring System
Initializing...

=== System Configuration ===
System voltage: 3.3V
ADC reference: 3.3V
ADC resolution: 12 bits
ADC max value: 4095
============================

BME280 initialized (at 0x76)
LCD initialized
CAN initialized
Initialization complete!
Active sensors: 11
```

## Sensor Accuracy Comparison

**VDO Thermistors:**
- Lookup table: ±0.5°C (most accurate)
- Steinhart-Hart: ±1-2°C (very good, faster)

**Choice:** Use lookup for critical sensors (CHT), Steinhart for less critical (coolant).

## Memory Usage Estimates

**Typical configuration (8 sensors, CAN, LCD):**
- RAM: ~3KB
- Flash: ~35KB

**Minimal configuration (3 sensors, serial only):**
- RAM: ~1.5KB
- Flash: ~25KB

**Maximum configuration (20 sensors, all outputs):**
- RAM: ~6KB
- Flash: ~50KB

## Getting Help

1. **Check documentation:**
   - Sensor Selection Guide (90% of questions)
   - Pressure/Voltage guides for those sensors
   - Advanced Calibration for custom sensors

2. **Enable serial debugging:**
   ```cpp
   #define ENABLE_SERIAL_OUTPUT
   ```

3. **Post in GitHub Discussions:**
   - Include sensor type from catalog
   - Include serial output
   - Include config.h relevant section

4. **File GitHub Issue for bugs:**
   - Include platform (Teensy 4.0, etc.)
   - Include error message
   - Include minimal config.h to reproduce

## Most Common Mistakes

1. ❌ Not picking sensor type from catalog
   - ✅ Open sensor_library.h and find your sensor

2. ❌ Mixing up lookup vs Steinhart sensor IDs
   - ✅ Both work, lookup is more accurate

3. ❌ Forgetting to define sensor type
   - ✅ Must have both ENABLE_X and X_SENSOR_TYPE

4. ❌ Wrong sensor ID for physical hardware
   - ✅ Double-check catalog ID matches your sensor

## Version Migration

**From v1.0 to v2.0:**
1. Replace manual calibration with sensor IDs
2. Remove old read function references
3. Add sensor type definitions
4. Test and verify readings match

See [Migration Guide](MIGRATION_GUIDE.md) for step-by-step instructions.

---

**💡 Pro Tip:** Start with just one sensor, verify it works, then add more. Much easier to debug!
