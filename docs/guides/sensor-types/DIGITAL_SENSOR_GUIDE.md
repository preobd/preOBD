# Digital Sensor Configuration Guide

## Overview

openEMS supports digital sensors for monitoring binary states like fluid levels, switches, and other on/off conditions. The most common application is coolant level monitoring using a float switch.

## Supported Digital Sensors

| Sensor Type | Application | Notes |
|-------------|-------------|-------|
| Float Switch (NC) | Coolant level, fuel level | Default configuration |
| Float Switch (NO) | Coolant level, fuel level | Requires INVERTED flag |
| Binary switches | Any on/off monitoring | General purpose |

## Quick Selection Guide

### Standard Float Switch (Normally Closed)

Most automotive float switches are **normally closed (NC)**:
- Float UP (normal level) = Switch CLOSED = HIGH signal
- Float DOWN (low level) = Switch OPEN = LOW signal

```cpp
#define ENABLE_COOLANT_LEVEL
#define COOLANT_LEVEL_SENSOR_TYPE  DIGITAL_FLOAT_SWITCH
#define COOLANT_LEVEL_INPUT        5     // Digital pin
#define COOLANT_LEVEL_MIN          0     // Alarm when low
#define COOLANT_LEVEL_MAX          1     // OK when normal
```

### Normally Open Float Switch

Some aftermarket switches are **normally open (NO)**:
- Float UP (normal level) = Switch OPEN = LOW signal
- Float DOWN (low level) = Switch CLOSED = HIGH signal

```cpp
#define ENABLE_COOLANT_LEVEL
#define COOLANT_LEVEL_SENSOR_TYPE  DIGITAL_FLOAT_SWITCH
#define COOLANT_LEVEL_INPUT        5
#define COOLANT_LEVEL_MIN          0
#define COOLANT_LEVEL_MAX          1
#define COOLANT_LEVEL_INVERTED     // Add this line for NO switches
```

## Understanding Float Switch Types

### Normally Closed (NC) - Standard

**How it works:**
```
┌─────────────┐
│   Sensor    │
│   Float UP  │ ──┐
│  (Normal)   │   │ Closed circuit
│             │   │
└─────────────┘   ▼
                 HIGH (1.0)

┌─────────────┐
│   Sensor    │
│  Float DOWN │ ──┐
│    (Low)    │   │ Open circuit
│             │   │
└─────────────┘   ▼
                 LOW (0.0)
```

**Configuration:**
- No special flags needed
- Default behavior works correctly
- Alarm triggers when reading = 0 (low level)

### Normally Open (NO) - Less Common

**How it works:**
```
┌─────────────┐
│   Sensor    │
│   Float UP  │ ──┐
│  (Normal)   │   │ Open circuit
│             │   │
└─────────────┘   ▼
                 LOW (0.0)

┌─────────────┐
│   Sensor    │
│  Float DOWN │ ──┐
│    (Low)    │   │ Closed circuit
│             │   │
└─────────────┘   ▼
                 HIGH (1.0)
```

**Configuration:**
- Requires `COOLANT_LEVEL_INVERTED` flag
- Reading is inverted in software
- Alarm still triggers when level is low (after inversion)

## Wiring

### Basic Float Switch Wiring

```
Float Switch
┌─────────────┐
│     NC      │───────┐
│             │       │
│     GND     │───────┼──── GND
└─────────────┘       │
                      │
              MCU Pin 5 (INPUT)
```

**Notes:**
- Most microcontrollers have internal pull-up resistors
- When switch is closed, pin reads HIGH
- When switch is open, pin reads LOW (pulled to ground)
- No external resistor typically needed

### With External Pull-up (Optional)

```
        +5V
         │
        [10kΩ]  Pull-up resistor (optional)
         │
         ├─────── MCU Pin 5 (INPUT)
         │
    Float Switch
    ┌────┴────┐
    │   NC    │
    │         │
    │   GND   │──── GND
    └─────────┘
```

**When to use:**
- If internal pull-ups are unreliable
- For longer wire runs (>1 meter)
- For noisy electrical environments

## Configuration Examples

### Example 1: Basic Coolant Level Monitoring

```cpp
// config.h
#define ENABLE_COOLANT_LEVEL
#define COOLANT_LEVEL_SENSOR_TYPE  DIGITAL_FLOAT_SWITCH
#define COOLANT_LEVEL_INPUT        5
#define COOLANT_LEVEL_MIN          0     // Alarm if low
#define COOLANT_LEVEL_MAX          1     // OK if normal
```

**Result:**
- Normal operation: Display shows "LVL: 100%"
- Low level: Display shows "LVL: 0%", alarm sounds
- LCD display: `LVL:100%` or `LVL:0%`
- OBDII PID 0xA2: Returns 255 (OK) or 0 (low)

### Example 2: Fuel Level Monitoring

```cpp
// config.h
#define ENABLE_FUEL_LEVEL
#define FUEL_LEVEL_SENSOR_TYPE     DIGITAL_FLOAT_SWITCH
#define FUEL_LEVEL_INPUT           8     // Different pin
#define FUEL_LEVEL_MIN             0     // Alarm at low fuel
#define FUEL_LEVEL_MAX             1
```

You'll also need to add the sensor to `sensors.cpp`:

```cpp
#ifdef ENABLE_FUEL_LEVEL
    const SensorConfig* fuel_level_config = getSensorConfig(DIGITAL_FLOAT_SWITCH);

    Sensor fuelLevel = {
        .input = FUEL_LEVEL_INPUT,
        .obd2pid = 0xA3,
        .obd2length = 1,
        .value = 0,
        .sensorType = fuel_level_config->internalType,
        .abbrName = "FUEL",
        .displayName = fuel_level_config->name,
        .displayUnits = PERCENT,
        .minValue = FUEL_LEVEL_MIN,
        .maxValue = FUEL_LEVEL_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = fuel_level_config->readFunction,
        .displayConvert = fuel_level_config->displayConvert,
        .obdConvert = fuel_level_config->obdConvert,
        .calibrationData = fuel_level_config->calibrationData,
        .calibrationType = fuel_level_config->calibrationType
    };
#endif
```

### Example 3: Oil Level Sensor

```cpp
// config.h
#define ENABLE_OIL_LEVEL
#define OIL_LEVEL_SENSOR_TYPE      DIGITAL_FLOAT_SWITCH
#define OIL_LEVEL_INPUT            9
#define OIL_LEVEL_MIN              0
#define OIL_LEVEL_MAX              1
```

### Example 4: Multiple Float Switches

You can monitor multiple levels simultaneously:

```cpp
// config.h - Monitor coolant AND fuel levels
#define ENABLE_COOLANT_LEVEL
#define COOLANT_LEVEL_SENSOR_TYPE  DIGITAL_FLOAT_SWITCH
#define COOLANT_LEVEL_INPUT        5
#define COOLANT_LEVEL_MIN          0
#define COOLANT_LEVEL_MAX          1

#define ENABLE_FUEL_LEVEL
#define FUEL_LEVEL_SENSOR_TYPE     DIGITAL_FLOAT_SWITCH
#define FUEL_LEVEL_INPUT           8
#define FUEL_LEVEL_MIN             0
#define FUEL_LEVEL_MAX             1
```

## Display Behavior

### LCD Display

Float switches display as percentage:
- `LVL:100%` - Normal level (switch indicates OK)
- `LVL:0%` - Low level (switch indicates low)

The abbreviation name can be customized in `sensors.cpp`:
```cpp
.abbrName = "LVL",    // For coolant level
.abbrName = "FUEL",   // For fuel level
.abbrName = "OIL",    // For oil level
```

### Serial Output

```
Sensor,Value,Units
LVL,100.00,%
LVL,0.00,%
```

### OBDII Output

Float switches use PID format:
- Value = 255 (0xFF) when OK
- Value = 0 when low/alarm condition

## Alarm Configuration

### Triggering Alarms

Alarms trigger when value is outside min/max range:

```cpp
#define COOLANT_LEVEL_MIN     0     // Alarm if value ≤ 0
#define COOLANT_LEVEL_MAX     1     // Alarm if value ≥ 1
```

Since float switches only return 0 or 1:
- Value = 1 (OK) → No alarm (within range)
- Value = 0 (low) → Alarm! (below minimum)

### Disabling Alarms

To monitor without alarming:
```cpp
// In sensors.cpp, find your sensor definition:
.alarm = false,   // Change to false to disable alarm
```

## Troubleshooting

### Reading Always Shows 0 or Always Shows 1

**Possible causes:**
1. Wrong switch type (NC vs NO)
2. Wiring issue
3. Bad ground connection

**Solutions:**
1. Try adding `COOLANT_LEVEL_INVERTED` flag
2. Check wiring with multimeter
3. Verify ground connection is solid

### Reading Intermittent or Noisy

**Possible causes:**
1. Loose connection
2. Electrical noise
3. Corroded contacts in switch

**Solutions:**
1. Check all connections, use proper crimps
2. Add 100nF capacitor from signal to ground
3. Clean or replace float switch

### Alarm Not Triggering

**Check:**
- [ ] `alarm = true` in sensor definition
- [ ] `COOLANT_LEVEL_MIN` is set to 0
- [ ] Buzzer connected to correct pin (pin 3)
- [ ] Reading actually changes when level is low

**Test:**
Manually ground the sensor pin - should trigger alarm immediately.

### Wrong Behavior (Alarm at Normal Level)

**Likely cause:** Switch is NO type but not configured

**Solution:** Add `COOLANT_LEVEL_INVERTED` flag

## Technical Details

### How the Reading Works

From `sensor_read.cpp`:

```cpp
void readDigitalFloatSwitch(Sensor *ptr) {
    // Read digital state from the pin
    float rawValue = (float)digitalRead(ptr->input);

    // Support both normally closed (NC) and normally open (NO) switches
    #ifdef COOLANT_LEVEL_INVERTED
    // Normally open: Float UP (ok) = OPEN = LOW, Float DOWN (low) = CLOSED = HIGH
    ptr->value = 1.0 - rawValue;  // Invert the reading
    #else
    // Normally closed (default): Float UP (ok) = CLOSED = HIGH, Float DOWN (low) = OPEN = LOW
    ptr->value = rawValue;
    #endif
}
```

### Data Storage

- Internal storage: 0.0 or 1.0 (float)
- Display conversion: 0% or 100%
- OBDII conversion: 0 or 255

### Pull-up Resistors

Most Arduino/Teensy boards have internal pull-up resistors (~20-50kΩ) that are automatically enabled for digital inputs. This eliminates the need for external pull-up resistors in most cases.

## Safety Considerations

⚠️ **Fluid Safety:**
- Ensure float switch is rated for the fluid (coolant, fuel, oil)
- Use proper sealing to prevent leaks
- Mount securely - vibration can cause false readings

⚠️ **Electrical Safety:**
- Never use float switches in explosive environments without proper certification
- Ensure waterproof connections
- Use proper wire gauge for automotive environments

⚠️ **Alarm Response:**
- Low coolant can lead to overheating - respond immediately
- Low oil can cause engine damage - stop engine
- Test alarm system regularly

## Installation Tips

### Mounting Location

**Coolant:**
- Mount in radiator or overflow tank
- Position float so it's submerged at normal level
- Leave room for float to move freely

**Fuel:**
- Mount in fuel tank or sending unit
- Consider fuel slosh during turns/stops
- May require multiple switches for large tanks

**Oil:**
- Mount in oil pan or sump
- Ensure switch can't be hit by rotating components
- Account for oil slosh during acceleration

### Wire Routing

1. Keep wires away from hot engine parts
2. Use proper automotive-grade wire
3. Protect with loom or conduit
4. Secure every 6-12 inches to prevent chafing
5. Use proper strain relief at sensor connection

## Common Float Switch Types

### Generic Automotive Float Switches

**Typical specifications:**
- Type: Normally Closed (NC)
- Voltage: 12V
- Current: 0.5A
- Thread: M10 or 1/8 NPT
- Cost: $5-15

### OEM Replacement Switches

Many vehicles have coolant level switches:
- BMW: 17 11 1 712 639
- VW/Audi: Various part numbers
- GM: 15-5311
- Ford: Various part numbers

These are typically NC switches and work with standard configuration.

## Getting Help

**If your float switch isn't working:**

1. **Check the type:**
   - Measure resistance with multimeter
   - Float UP: NC = 0Ω, NO = ∞Ω (open)
   - Float DOWN: NC = ∞Ω (open), NO = 0Ω

2. **Enable serial output:**
   ```cpp
   #define ENABLE_SERIAL_OUTPUT
   ```
   Watch the readings change when you move the float.

3. **Post in GitHub Discussions with:**
   - Float switch type/part number
   - Measured resistance in both positions
   - Serial output showing readings

## Future Enhancements

Possible additions for digital sensors:
- Multi-level sensors (3+ states)
- Analog level sensors (resistance-based)
- Ultrasonic level sensors
- Pressure-based level sensing

---

**Digital sensors are simple, reliable, and cheap - perfect for classic cars!**
