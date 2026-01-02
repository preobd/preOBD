# VDO Hall Effect Speed Sensor (YBE100530)

## Overview

The VDO Hall Effect Speed Sensor (part number YBE100530 / 340.214/13/4) is a 3-pin hall effect sensor used for measuring vehicle speed. This sensor generates pulses based on gear teeth or magnets passing by, which are counted and converted to vehicle speed in km/h or mph.

## ⚠️ CRITICAL: Voltage Protection Required

**WARNING:** The VDO sensor outputs a **12V signal** but Teensy 4.1 uses **3.3V logic**. You **MUST** add voltage protection before connecting the sensor signal to any Teensy pin, or you will damage your board!

### Recommended Protection Circuit (Zener Diode Clamp)

```
VDO Pin 3 (Signal) ───[1kΩ resistor]───┬─── Teensy Digital Pin
                                       │
                                   3.3V Zener Diode
                                       │
                                      GND
```

**Components:**
- 1kΩ resistor (current limiting)
- 3.3V Zener diode (BZX55C3V3 or similar)

**Why this works:**
- The 1kΩ resistor limits current through the Zener diode
- The Zener diode clamps the voltage to a safe 3.3V maximum
- When the signal is > 3.3V, the Zener conducts and shunts excess voltage to ground
- When the signal is < 3.3V, the Zener acts as an open circuit

### Alternative Protection Methods

**Option 2: Resistor Voltage Divider**
```
VDO Pin 3 ───[10kΩ]───┬───[4.7kΩ]─── GND
                       │
                    Teensy Pin
```
- Output voltage: 12V × (4.7kΩ / 14.7kΩ) ≈ 3.84V
- **Note:** May need additional clamping for safety

**Option 3: Level Shifter IC (Most Robust)**
- Use a bidirectional level shifter like TXS0108E
- Handles 12V to 3.3V conversion with built-in protection
- More expensive but safest option

## Wiring

### VDO Sensor Pinout

**YBE100530 Connector (3 pins):**
- **Pin 1:** +12V power (vehicle power, red/white wire)
- **Pin 2:** Ground (black wire)
- **Pin 3:** Signal output (12V square wave pulses, red/black wire)

### Connection to Teensy 4.1

1. Connect VDO Pin 1 to vehicle +12V (switched ignition recommended)
2. Connect VDO Pin 2 to vehicle ground AND Teensy ground (common ground essential!)
3. Connect VDO Pin 3 to voltage protection circuit, then to any Teensy digital pin (e.g., pin 2, 3, 5, 6, etc.)

**Compatible Harness:** STC4637 (TD5 tail light plug style connector) works with this sensor

## How It Works

The hall effect sensor generates one pulse per gear tooth (or magnet) that passes by. By measuring the frequency of these pulses and knowing:
- Number of pulses per revolution
- Tire circumference
- Differential/transmission ratio

The system calculates vehicle speed using:

```
freq_hz = 1000000.0 / pulse_interval_microseconds
revolutions_per_second = freq_hz / pulses_per_rev
wheel_speed_m_per_s = revolutions_per_second × (tire_circumference_mm / 1000.0) / final_drive_ratio
speed_kph = wheel_speed_m_per_s × 3.6 × calibration_mult
```

## Calibration

### Required Information

You must determine these parameters for your specific vehicle:

1. **pulses_per_rev**: Number of gear teeth or pulses per output shaft revolution
   - Count the teeth on the gear where the sensor mounts
   - OR drive at a known GPS speed and back-calculate

2. **tire_circumference_mm**: Actual tire rolling circumference in millimeters
   - Formula: circumference ≈ π × tire_diameter_mm
   - Example: 205/55R16 ≈ 2008mm
   - **Best method:** Mark tire, roll exactly one revolution, measure distance

3. **final_drive_ratio**: Differential/transmission ratio
   - Check vehicle specifications
   - Example: 3.73:1 = 3.73

4. **calibration_mult**: Fine-tuning multiplier (start with 1.0)
   - Adjust after testing against GPS speed
   - If reading 5% low, set to 1.05

5. **timeout_ms**: Zero speed timeout in milliseconds (default: 2000)
   - Time to wait without pulses before reporting 0 speed

6. **max_speed_kph**: Maximum valid speed for safety check (default: 300)
   - Readings above this will be rejected as invalid

### Calibration Procedure

1. **Initial Setup:**
   ```
   SET 2 VEHICLE_SPEED VDO_SPEED_SENSOR
   SETCAL 2 SPEED 100 2008 3.73 1.0 2000 300
   SAVE
   ```
   *(Parameters: pulses_per_rev tire_circumference_mm final_drive_ratio calibration_mult timeout_ms max_speed_kph)*

2. **Verify Basic Operation:**
   - Start vehicle
   - Drive slowly
   - Use `INFO 2` to check if speed readings appear
   - If no readings, check wiring and voltage protection

3. **Fine-Tune Calibration:**
   - Drive at steady highway speed (e.g., 100 km/h GPS-verified)
   - Compare openEMS reading to GPS
   - Adjust `calibration_mult`:
     - If reading is 95 km/h but GPS shows 100 km/h: `calibration_mult = 1.05` (100/95)
     - If reading is 105 km/h but GPS shows 100 km/h: `calibration_mult = 0.95` (100/105)

4. **Update and Save:**
   ```
   SETCAL 2 SPEED 100 2008 3.73 1.05 2000 300
   SAVE
   ```

5. **Verify at Multiple Speeds:**
   - Test at 50 km/h, 80 km/h, 100 km/h, 120 km/h
   - Speed should be accurate across the range
   - If not linear, check that sensor is reading cleanly (no missed pulses)

### Troubleshooting Calibration

**Speed reads too high:**
- Decrease `calibration_mult` or `tire_circumference_mm`
- Check `final_drive_ratio` is correct

**Speed reads too low:**
- Increase `calibration_mult` or `tire_circumference_mm`
- Check `final_drive_ratio` is correct

**Speed is erratic/jumping:**
- Check voltage protection circuit
- Verify solid ground connection
- Check for electrical noise (route sensor wire away from spark plug wires)
- Increase debounce (modify code if necessary)

**Speed stuck at zero:**
- Check voltage protection circuit (may be clamping signal too much)
- Verify sensor has power (12V at Pin 1)
- Check ground connection
- Verify sensor is close enough to gear teeth
- Test sensor with multimeter: should see pulses when wheel rotates

## Serial Commands

### Basic Configuration
```bash
# Configure input on pin 2 as vehicle speed with VDO sensor
SET 2 VEHICLE_SPEED VDO_SPEED_SENSOR

# Set calibration (example values - adjust for your vehicle)
SETCAL 2 SPEED 100 2008 3.73 1.0 2000 300

# Save configuration to EEPROM
SAVE

# Check current reading
INFO 2
```

### Display Units
```bash
# Switch between km/h and mph
SET 2 UNITS KPH   # Kilometers per hour (default)
SET 2 UNITS MPH   # Miles per hour
```

### Advanced Commands
```bash
# Check detailed calibration info
INFO 2 CALIBRATION

# Enable/disable input
ENABLE 2
DISABLE 2

# Remove configuration
CLEAR 2
```

## OBD-II Integration

The VDO speed sensor automatically integrates with OBD-II output:

- **PID:** 0x0D (Vehicle Speed)
- **Format:** Single byte (0-255 km/h)
- **Conversion:** Direct value, clamped to 255 km/h maximum

Compatible with:
- OBD-II scan tools
- RealDash
- Torque Pro
- Other automotive apps supporting standard PIDs

## Technical Specifications

| Parameter | Value |
|-----------|-------|
| Input voltage | 12V DC (vehicle power) |
| Output signal | 12V square wave (requires level shifting!) |
| Output type | Hall effect, pull-up |
| Pin count | 3 |
| Mounting | Transfer case, transmission, differential |
| Compatible tire sizes | Any (configurable) |
| Speed range | 0-300 km/h (configurable) |
| Update rate | Continuous (interrupt-driven) |
| Debounce | 500µs (allows up to ~300 km/h) |
| Timeout | 2000ms default (configurable) |

## Example Configurations

### Land Rover Defender with 205/55R16 Tires
```
SET 2 VEHICLE_SPEED VDO_SPEED_SENSOR
SETCAL 2 SPEED 100 2008 3.54 1.0 2000 250
SAVE
```

### Generic 4x4 with 285/75R16 Tires (Larger)
```
SET 2 VEHICLE_SPEED VDO_SPEED_SENSOR
SETCAL 2 SPEED 100 2436 3.73 1.0 2000 200
SAVE
```
*(285/75R16 ≈ 2436mm circumference)*

### Off-Road Vehicle with 33" Tires
```
SET 2 VEHICLE_SPEED VDO_SPEED_SENSOR
SETCAL 2 SPEED 100 2616 4.10 1.0 2000 160
SAVE
```
*(33" diameter ≈ 2616mm circumference, 4.10:1 gearing)*

## Safety Notes

1. **Electrical Safety:**
   - Always use voltage protection
   - Verify voltage levels with multimeter before connecting
   - Use proper automotive-grade wire and connectors

2. **Installation Safety:**
   - Ensure sensor cannot contact rotating parts
   - Use proper mounting hardware
   - Check clearance when suspension compresses

3. **Calibration Accuracy:**
   - Inaccurate speed readings could affect safety calculations
   - Always verify against GPS before relying on readings
   - Recheck calibration if tires are changed

4. **Legal Compliance:**
   - Some jurisdictions prohibit speedometer modifications
   - This is supplementary instrumentation only
   - Do not replace factory speedometer without proper certification

## References

- VDO part number: 340.214/13/4
- Land Rover part number: YBE100530
- Compatible harness: STC4637
- Thread: [VDO electronic speedo conversion](https://www.aulro.com/afvb/90-110-130-defender-county/254951-vdo-electronic-speedo-conversion-part-numbers-ebay-links.html)
- Hall effect sensor info: [Determining Hall Effect sensor pinouts](https://support.haltech.com/portal/en/kb/articles/determining-hall-effect-sensor-pinouts)
