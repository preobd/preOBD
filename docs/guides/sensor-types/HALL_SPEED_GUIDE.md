# Hall Effect Speed Sensor (HALL_SPEED)

## Overview

The HALL_SPEED sensor type supports any 3-wire hall effect speed sensor for measuring vehicle speed. These sensors generate pulses based on gear teeth or magnets passing by, which are counted and converted to vehicle speed in km/h or mph.

### Compatible Sensors

This implementation works with:
- **VDO sensors** (YBE100530, 340.214/13/4)
- **OEM sensors** (GM, Ford, Chrysler, Land Rover, etc.)
- **Generic 3-wire hall effect sensors** (power, ground, signal)

The configuration and calibration process is the same regardless of manufacturer.

## ⚠️ CRITICAL: Voltage Protection Required

**WARNING:** Many hall effect sensors (including VDO) output a **12V signal** which can damage microcontroller inputs. You **MUST** verify your sensor's output voltage matches your board's logic level. If the sensor outputs higher voltage than your board can handle, add voltage protection or you will damage your board!

### Recommended Protection Circuit (Zener Diode Clamp)

**For 12V sensors to 3.3V logic (e.g., Teensy 4.1):**
```
Sensor Signal ───[1kΩ resistor]───┬─── Microcontroller Digital Pin
                                  │
                              3.3V Zener Diode
                                  │
                                 GND
```

**Components:**
- 1kΩ resistor (current limiting)
- Zener diode matching your logic level (3.3V for Teensy 4.1, 5V for Arduino Uno, etc.)
  - For 3.3V: BZX55C3V3 or similar
  - For 5V: BZX55C5V1 or similar

**Why this works:**
- The 1kΩ resistor limits current through the Zener diode
- The Zener diode clamps the voltage to a safe level matching your board's logic
- When the signal exceeds the Zener voltage, it conducts and shunts excess to ground
- When the signal is below the Zener voltage, it acts as an open circuit

### Alternative Protection Methods

**Option 2: Resistor Voltage Divider**
```
Sensor Signal ───[R1]───┬───[R2]─── GND
                         │
                   Board Input Pin
```
- Calculate resistors based on sensor voltage and board logic level
- Example for 12V→3.3V: R1=10kΩ, R2=4.7kΩ gives ~3.8V
- **Note:** May need additional clamping diode for safety

**Option 3: Level Shifter IC (Most Robust)**
- Use a bidirectional level shifter (e.g., TXS0108E)
- Handles voltage conversion with built-in protection
- More expensive but safest option
- Recommended for production use

## Wiring

### Generic 3-Wire Hall Effect Sensor Pinout

Most hall effect speed sensors use a standard 3-wire configuration:
- **Pin 1:** Power (+5V to +12V, typically vehicle power)
- **Pin 2:** Ground (common ground with microcontroller)
- **Pin 3:** Signal output (square wave pulses matching power voltage)

**Note:** Pin numbering and wire colors vary by manufacturer. Always verify with your sensor's datasheet.

### VDO Sensor Specific Pinout (YBE100530)

**YBE100530 Connector:**
- **Pin 1:** +12V power (red/white wire)
- **Pin 2:** Ground (black wire)
- **Pin 3:** Signal output - 12V pulses (red/black wire)
- **Compatible Harness:** STC4637

### Connection to Microcontroller

1. Connect sensor power pin to appropriate voltage source (typically vehicle +12V or +5V)
2. Connect sensor ground to vehicle ground AND microcontroller ground (common ground is essential!)
3. Connect sensor signal to voltage protection circuit (if needed), then to any digital input pin with interrupt capability

**Important:** Ensure common ground between sensor and microcontroller. Without shared ground, signals will be unreliable.

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
   SET 2 VEHICLE_SPEED HALL_SPEED
   SET 2 SPEED 100 2008 3.73 1.0 2000 300
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
   SET 2 SPEED 100 2008 3.73 1.05 2000 300
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
# Configure input on pin 2 as vehicle speed with hall effect sensor
SET 2 VEHICLE_SPEED HALL_SPEED

# Set calibration (example values - adjust for your vehicle)
SET 2 SPEED 100 2008 3.73 1.0 2000 300

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

The HALL_SPEED sensor automatically integrates with OBD-II output:

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

### Land Rover Defender with 205/55R16 Tires (VDO YBE100530)
```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 SPEED 100 2008 3.54 1.0 2000 250
SAVE
```

### Generic 4x4 with 285/75R16 Tires (Larger)
```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 SPEED 100 2436 3.73 1.0 2000 200
SAVE
```
*(285/75R16 ≈ 2436mm circumference)*

### Off-Road Vehicle with 33" Tires
```
SET 2 VEHICLE_SPEED HALL_SPEED
SET 2 SPEED 100 2616 4.10 1.0 2000 160
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
