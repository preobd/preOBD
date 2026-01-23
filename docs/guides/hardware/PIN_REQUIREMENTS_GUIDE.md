# Pin Type Requirements Guide

**Version:** 0.5.0-alpha (Unreleased)
**Last Updated:** 2025-12-19

---

## Overview

Different sensors in openEMS require different **pin types** based on their hardware interface. This guide explains the pin type system, validation rules, platform-specific constraints, and common troubleshooting scenarios.

Understanding pin types is critical for:
- **Hardware design** - Choosing correct pins during PCB layout
- **Configuration** - Avoiding invalid pin assignments
- **Troubleshooting** - Diagnosing "sensor not working" issues

---

## Table of Contents

1. [Pin Type Categories](#pin-type-categories)
2. [Pin Type Requirements by Sensor](#pin-type-requirements-by-sensor)
3. [Platform-Specific Pin Limits](#platform-specific-pin-limits)
4. [Reserved Pins](#reserved-pins)
5. [Pin Validation Rules](#pin-validation-rules)
6. [Common Pin Assignment Examples](#common-pin-assignment-examples)
7. [Troubleshooting Pin Errors](#troubleshooting-pin-errors)
8. [CHT Pin Assignment Changes (v0.4.0+)](#cht-pin-assignment-changes-v040)

---

## Pin Type Categories

openEMS categorizes pins into three types based on the **hardware interface** required by the sensor:

### PIN_ANALOG

**What it means:**
- Sensor uses Arduino's `analogRead()` function
- Requires a physical analog input pin (A0-A15 on Mega)
- Reads continuous voltage levels (0-5V → 0-1023 ADC value)

**Typical sensors:**
- Thermistors (VDO_120C, VDO_150C, etc.)
- Analog pressure sensors (MPX4250AP, VDO_2BAR_CURVE, GENERIC_BOOST)
- Voltage dividers (battery voltage monitoring)

**Pin format:**
- `A0`, `A1`, `A2`, ..., `A15` (Arduino Mega 2560)
- `A0`, `A1`, ..., `A5` (Arduino Uno)

**Hardware characteristics:**
- 10-bit ADC resolution (0-1023)
- 0-5V input range (typical)
- High impedance input (~100 MΩ)

---

### PIN_DIGITAL

**What it means:**
- Sensor uses digital I/O functions (`digitalWrite`, `digitalRead`, `attachInterrupt`)
- Can use any general-purpose digital pin (0-53 on Mega, excluding reserved)
- Reads discrete HIGH/LOW states or generates chip-select signals

**Typical sensors:**
- **SPI thermocouples** (MAX6675, MAX31855) - Uses pin as SPI chip-select
- **RPM sensors** (W_PHASE_RPM) - Uses pin for interrupt-based pulse counting
- **Float switches** (FLOAT_SWITCH) - Uses pin for digital HIGH/LOW detection

**Pin format:**
- `0`, `1`, `2`, ..., `53` (Arduino Mega 2560)
- `0`, `1`, ..., `13` (Arduino Uno)

**Hardware characteristics:**
- TTL/CMOS logic levels (0V = LOW, 5V = HIGH)
- Can configure as INPUT, OUTPUT, INPUT_PULLUP
- Some pins support hardware interrupts (required for RPM sensors)

---

### PIN_I2C

**What it means:**
- Sensor communicates via I2C bus (Two-Wire Interface)
- Uses shared SDA/SCL pins (A4/A5 on Uno, 20/21 on Mega)
- **Special handling:** Pin field must be set to the string `"I2C"` (not a physical pin number)

**Typical sensors:**
- BME280 sensors (BME280_TEMP, BME280_PRESSURE, BME280_HUMIDITY, BME280_ELEVATION)

**Pin format:**
- `I2C` (case-insensitive string literal)

**Hardware characteristics:**
- Shared bus (multiple sensors on same SDA/SCL pins)
- Requires pull-up resistors (typically 4.7kΩ on SDA/SCL)
- Addressable (devices identified by I2C address, not pin number)

**Important notes:**
- Only one "I2C" pin assignment needed per I2C sensor
- Multiple I2C sensors share the same physical pins (SDA/SCL)
- openEMS handles I2C bus initialization automatically

---

## Pin Type Requirements by Sensor

This table maps each sensor to its required pin type:

| Sensor Name           | Pin Type Requirement | Example Pin | Notes                                |
|-----------------------|----------------------|-------------|--------------------------------------|
| **Thermocouples**     |                      |             |                                      |
| MAX6675               | PIN_DIGITAL          | 6           | SPI chip-select pin                  |
| MAX31855              | PIN_DIGITAL          | 7           | SPI chip-select pin                  |
| **Thermistors**       |                      |             |                                      |
| VDO_120C_TABLE       | PIN_ANALOG           | A0          | Analog voltage from thermistor       |
| VDO_150C_TABLE       | PIN_ANALOG           | A1          | Analog voltage from thermistor       |
| VDO_120C_STEINHART    | PIN_ANALOG           | A2          | Analog voltage from thermistor       |
| VDO_150C_STEINHART    | PIN_ANALOG           | A3          | Analog voltage from thermistor       |
| THERMISTOR_TABLE     | PIN_ANALOG           | A4          | Custom thermistor (table)     |
| THERMISTOR_STEINHART  | PIN_ANALOG           | A5          | Custom thermistor (Steinhart-Hart)   |
| **Pressure Sensors**  |                      |             |                                      |
| GENERIC_BOOST         | PIN_ANALOG           | A6          | Analog boost/vacuum sensor           |
| MPX4250AP             | PIN_ANALOG           | A7          | Freescale analog pressure sensor     |
| MPX5700AP             | PIN_ANALOG           | A8          | Freescale analog pressure sensor     |
| VDO_2BAR_CURVE              | PIN_ANALOG           | A8          | VDO 2-bar sender                     |
| VDO_5BAR_CURVE              | PIN_ANALOG           | A9          | VDO 5-bar sender                     |
| **Voltage Sensors**   |                      |             |                                      |
| VOLTAGE_DIVIDER       | PIN_ANALOG           | A10         | Voltage divider circuit              |
| **RPM Sensors**       |                      |             |                                      |
| W_PHASE_RPM           | PIN_DIGITAL          | 2           | Interrupt-capable pin required       |
| **Environmental Sensors**       |                      |             |                                      |
| BME280_TEMP           | PIN_I2C              | I2C         | Uses shared I2C bus (SDA/SCL)        |
| BME280_PRESSURE       | PIN_I2C              | I2C         | Uses shared I2C bus (SDA/SCL)        |
| BME280_HUMIDITY       | PIN_I2C              | I2C         | Uses shared I2C bus (SDA/SCL)        |
| BME280_ELEVATION      | PIN_I2C              | I2C         | Uses shared I2C bus (SDA/SCL)        |
| **Digital Sensors**   |                      |             |                                      |
| FLOAT_SWITCH          | PIN_DIGITAL          | 8           | Digital HIGH/LOW detection           |

---

## Platform-Specific Pin Limits

Different Arduino platforms have different numbers of analog and digital pins. The configuration tool validates against these limits:

### Arduino Mega 2560 (default)

```
Platform: megaatmega2560
Analog pins:  A0 - A15  (16 total)
Digital pins: 0 - 53    (54 total)
I2C pins:     SDA=20, SCL=21
```

**Key characteristics:**
- Large pin count suitable for complex installations
- Default platform for openEMS
- Supports up to 16 analog inputs

---

### Arduino Uno

```
Platform: uno
Analog pins:  A0 - A5   (6 total)
Digital pins: 0 - 13    (14 total)
I2C pins:     SDA=A4, SCL=A5
```

**Key characteristics:**
- Limited analog pins (max 6 sensors requiring analog input)
- Note: A4/A5 shared between analog input and I2C functions
- Suitable for smaller installations (≤8 inputs total)

---

### Arduino Nano

```
Platform: nanoatmega328
Analog pins:  A0 - A7   (8 total)
Digital pins: 0 - 13    (14 total)
I2C pins:     SDA=A4, SCL=A5
```

**Key characteristics:**
- Similar to Uno but with 2 additional analog pins (A6/A7)
- Compact form factor
- Note: A6/A7 are analog-input-only on Nano

---

## Reserved Pins

Certain pins are **reserved** for critical system functions and cannot be used for sensor inputs:

| Pin(s) | Reserved For           | Reason                                      |
|--------|------------------------|---------------------------------------------|
| 0, 1   | Serial (USB)           | Serial communication with PC/serial monitor |
| 20, 21 | I2C (SDA/SCL)          | I2C bus communication (Mega only)           |
| 50-53  | SPI (MISO/MOSI/SCK/SS) | SPI bus for SD card, CAN, thermocouples     |

**Important notes:**
- These pins are automatically excluded by `configure.py`
- Attempting to assign a reserved pin will result in validation error
- I2C sensors (BME280) use `"I2C"` as pin identifier, not "20" or "21"

---

## Pin Validation Rules

The configuration tool (`configure.py`) validates pins against these rules:

### 1. Pin Format Validation

✅ **Valid pin formats:**
```
A0, A1, A15          # Analog pins (uppercase A)
0, 6, 22, 53         # Digital pins (plain numbers)
I2C                  # I2C pseudo-pin (case-insensitive)
```

❌ **Invalid pin formats:**
```
a0                   # Lowercase 'a' not accepted (use A0)
D6                   # 'D' prefix not used (use 6)
PIN_A0               # No 'PIN_' prefix (use A0)
54                   # Exceeds Mega digital pin limit (0-53)
```

---

### 2. Platform Limit Validation

The tool checks that pins are within platform-specific limits:

**Example errors:**
```
❌ Analog pin A16 exceeds platform limit of 15 (Mega 2560)
❌ Digital pin 14 exceeds platform limit of 13 (Uno)
❌ Analog pin A6 exceeds platform limit of 5 (Uno)
```

---

### 3. Reserved Pin Validation

The tool prevents assignment of reserved pins:

**Example errors:**
```
❌ Pin 0 is reserved for Serial (RX)
❌ Pin 1 is reserved for Serial (TX)
❌ Pin 50 is reserved for SPI (MISO)
```

---

### 4. Pin Type Compatibility Validation

The tool ensures sensor pin type requirements match the assigned pin:

**Example errors:**
```
❌ Sensor MAX6675 requires a digital pin, but A0 is an analog pin
   → Solution: Use digital pin like 6 or 7

❌ Sensor VDO_120C_TABLE requires an analog pin, but 6 is a digital pin
   → Solution: Use analog pin like A0 or A1

❌ I2C sensor must use 'I2C' as pin (not 20)
   → Solution: Set pin to "I2C"

❌ Only I2C sensors can use 'I2C' as pin
   → Solution: Use physical pin number (A0, 6, etc.)
```

---

### 5. Duplicate Pin Validation

The tool prevents assigning the same pin to multiple inputs:

**Example error:**
```
❌ Pin A0 is already in use.
   → Solution: Choose a different unused pin
```

**Exception:** Multiple I2C sensors can share the `"I2C"` pin (I2C is a shared bus).

---

## Common Pin Assignment Examples

### Example 1: Mixed Sensor Configuration (Arduino Mega 2560)

```
Input 0: Pin 6,  Application CHT,          Sensor MAX6675        (PIN_DIGITAL - SPI CS)
Input 1: Pin A0, Application OIL_TEMP,     Sensor VDO_150C       (PIN_ANALOG - Thermistor)
Input 2: Pin A1, Application COOLANT,      Sensor VDO_120C       (PIN_ANALOG - Thermistor)
Input 3: Pin A2, Application OIL_PRESSURE, Sensor VDO_5BAR_CURVE       (PIN_ANALOG - Pressure)
Input 4: Pin 2,  Application ENGINE_RPM,   Sensor W_PHASE_RPM    (PIN_DIGITAL - Interrupt)
Input 5: Pin I2C, Application AMBIENT,     Sensor BME280_TEMP    (PIN_I2C - Shared bus)
```

**Pin usage summary:**
- Digital pins: 2, 6
- Analog pins: A0, A1, A2
- I2C bus: SDA/SCL (implicit via "I2C")

---

### Example 2: Thermocouple-Heavy Configuration

```
Input 0: Pin 6,  Application CHT,      Sensor MAX6675
Input 1: Pin 7,  Application EGT_1,    Sensor MAX31855
Input 2: Pin 8,  Application EGT_2,    Sensor MAX31855
Input 3: Pin 9,  Application EGT_3,    Sensor MAX31855
Input 4: Pin A0, Application OIL_TEMP, Sensor VDO_150C
```

**Why this works:**
- MAX6675/MAX31855 require **digital pins** for SPI chip-select
- Each thermocouple needs its own unique CS pin
- Thermistors use **analog pins** separately

---

### Example 3: I2C Multi-Sensor Configuration

```
Input 0: Pin I2C, Application AMBIENT,        Sensor BME280_TEMP
Input 1: Pin I2C, Application BAROMETER,      Sensor BME280_PRESSURE
Input 2: Pin I2C, Application HUMIDITY,       Sensor BME280_HUMIDITY
Input 3: Pin I2C, Application ELEVATION,      Sensor BME280_ELEVATION
```

**Why this works:**
- All four BME280 readings share the same I2C bus
- Each uses `"I2C"` as the pin identifier
- Physically connected to SDA=20, SCL=21 (Mega) or SDA=A4, SCL=A5 (Uno)

---

### Example 4: Arduino Uno (6 analog pins max)

```
Input 0: Pin 6,  Application CHT,          Sensor MAX6675        (PIN_DIGITAL)
Input 1: Pin A0, Application OIL_TEMP,     Sensor VDO_150C       (PIN_ANALOG)
Input 2: Pin A1, Application COOLANT,      Sensor VDO_120C       (PIN_ANALOG)
Input 3: Pin A2, Application OIL_PRESSURE, Sensor VDO_5BAR_CURVE       (PIN_ANALOG)
Input 4: Pin 2,  Application ENGINE_RPM,   Sensor W_PHASE_RPM    (PIN_DIGITAL)
```

**Constraint:** Uno has only A0-A5 available (6 analog pins), so max 6 analog sensors.

---

## Troubleshooting Pin Errors

### Problem: "Sensor requires analog pin, but X is digital"

**Cause:** You assigned a digital pin (e.g., `6`) to a sensor requiring analog input (e.g., VDO thermistor).

**Solution:**
1. Identify sensor's pin type requirement (see [Pin Type Requirements by Sensor](#pin-type-requirements-by-sensor))
2. Change pin to analog format: `A0`, `A1`, etc.
3. Re-run configuration

**Example:**
```
❌ Input 0: Pin 6, Sensor VDO_120C_TABLE
   Error: Sensor requires an analog pin, but 6 is a digital pin

✅ Input 0: Pin A0, Sensor VDO_120C_TABLE
   Success!
```

---

### Problem: "Sensor requires digital pin, but X is analog"

**Cause:** You assigned an analog pin (e.g., `A0`) to a sensor requiring digital I/O (e.g., MAX6675 thermocouple).

**Solution:**
1. Change pin to digital format: `6`, `7`, etc.
2. Avoid reserved pins (0, 1, 20, 21, 50-53)

**Example:**
```
❌ Input 0: Pin A0, Application CHT, Sensor MAX6675
   Error: Sensor requires a digital pin, but A0 is an analog pin

✅ Input 0: Pin 6, Application CHT, Sensor MAX6675
   Success!
```

---

### Problem: "I2C sensor must use 'I2C' as pin (not 20)"

**Cause:** You assigned a physical pin number (e.g., `20`) to an I2C sensor instead of the string `"I2C"`.

**Solution:**
1. Change pin to the literal string `I2C` (case-insensitive)
2. Do not use physical pin numbers for I2C sensors

**Example:**
```
❌ Input 0: Pin 20, Sensor BME280_TEMP
   Error: I2C sensor must use 'I2C' as pin (not 20)

✅ Input 0: Pin I2C, Sensor BME280_TEMP
   Success!
```

---

### Problem: "Only I2C sensors can use 'I2C' as pin"

**Cause:** You used `"I2C"` as pin for a non-I2C sensor (e.g., thermistor).

**Solution:**
1. I2C pin identifier is reserved for BME280 sensors only
2. Use physical pin number for non-I2C sensors

**Example:**
```
❌ Input 0: Pin I2C, Sensor VDO_120C_TABLE
   Error: Only I2C sensors can use 'I2C' as pin

✅ Input 0: Pin A0, Sensor VDO_120C_TABLE
   Success!
```

---

### Problem: "Pin X is already in use"

**Cause:** You assigned the same physical pin to multiple inputs.

**Solution:**
1. Each non-I2C input needs a unique physical pin
2. Check your configuration for duplicate pin assignments
3. Choose a different available pin

**Example:**
```
❌ Input 0: Pin A0, Sensor VDO_120C
   Input 1: Pin A0, Sensor VDO_150C
   Error: Pin A0 is already in use.

✅ Input 0: Pin A0, Sensor VDO_120C
   Input 1: Pin A1, Sensor VDO_150C
   Success!
```

**Exception:** Multiple I2C sensors can share the `"I2C"` pin:
```
✅ Input 0: Pin I2C, Sensor BME280_TEMP
   Input 1: Pin I2C, Sensor BME280_PRESSURE
   Success! (I2C is a shared bus)
```

---

### Problem: "Pin X exceeds platform limit"

**Cause:** You assigned a pin number that doesn't exist on your platform.

**Solution:**
1. Check platform limits (see [Platform-Specific Pin Limits](#platform-specific-pin-limits))
2. Arduino Uno: A0-A5 (analog), 0-13 (digital)
3. Arduino Mega: A0-A15 (analog), 0-53 (digital)

**Example (Arduino Uno):**
```
❌ Input 0: Pin A6, Sensor VDO_120C
   Error: Analog pin A6 exceeds platform limit of 5

✅ Input 0: Pin A0, Sensor VDO_120C
   Success!
```

---

### Problem: "Sensor not reading after upload"

**Symptom:** Configuration compiles, but sensor reads `NaN` or incorrect values.

**Possible causes & solutions:**

1. **Wrong pin type assigned:**
   - Verify sensor's pin type requirement matches assigned pin
   - Example: MAX6675 needs digital pin, not analog

2. **Hardware wiring mismatch:**
   - Verify physical sensor is wired to the pin specified in config
   - Example: Config says "Pin A0" but sensor wired to A1

3. **I2C pull-up resistors missing:**
   - BME280 sensors require 4.7kΩ pull-ups on SDA/SCL
   - Check hardware schematic

4. **Reserved pin conflict:**
   - Pin 50-53 used for sensor but also needed for SPI bus
   - Use different pin outside reserved range

---

## CHT Pin Assignment Changes (v0.4.0+)

**Important:** Prior to v0.4.0, CHT (Cylinder Head Temperature) was commonly assigned to analog pins `A0` or `A1`. This was **incorrect** for MAX6675/MAX31855 sensors, which require **digital pins**.

### What Changed

| Version    | CHT Pin Assignment | Correct? | Reason                                         |
|------------|--------------------|----------|------------------------------------------------|
| ≤ v0.3.x   | A0 or A1           | ❌ Wrong | MAX6675/MAX31855 require **digital** pins      |
| ≥ v0.4.0   | 6 or 7             | ✅ Right | Pin type validation enforces digital pin usage |

### Why This Matters

MAX6675 and MAX31855 are **SPI thermocouple chips** that use the assigned pin as a **chip-select (CS) line**. This requires:
- `digitalWrite()` to toggle CS HIGH/LOW
- Digital I/O capabilities (not analog input)

**Using analog pins (A0/A1) worked accidentally** because:
- Arduino allows `digitalWrite()` on analog pins (analog pins have dual digital/analog function)
- However, this is **semantically incorrect** and causes confusion

### Migration Path

If you have an existing configuration using `A0` or `A1` for CHT with MAX6675/MAX31855:

**Old configuration (incorrect):**
```cpp
// config.h (v0.3.x)
static InputConfig inputConfigs[MAX_INPUTS] = {
    {A0, APP_CHT, SENSOR_MAX6675},  // ❌ Incorrect: analog pin for digital sensor
};
```

**New configuration (correct):**
```cpp
// config.h (v0.4.0+)
static InputConfig inputConfigs[MAX_INPUTS] = {
    {6, APP_CHT, SENSOR_MAX6675},  // ✅ Correct: digital pin for SPI CS
};
```

**Hardware note:** You must physically move the sensor's CS wire from pin A0 to pin 6 (or another available digital pin).

### Using configure.py

The configuration tool (`configure.py`) now enforces pin type validation:

```bash
$ python3 tools/configure.py

Select sensor: MAX6675
Pin (e.g., A0, 6): A0
❌ Invalid pin: Sensor requires a digital pin, but A0 is an analog pin

Pin (e.g., A0, 6): 6
✅ Valid pin assignment!
```

---

## See Also

- **[configure.py Documentation](../../../tools/README.md)** - Detailed usage of the configuration tool
- **[Sensor Selection Guide](../sensor-types/SENSOR_SELECTION_GUIDE.md)** - Choosing the right sensor for your application
- **[Adding Sensors Guide](../configuration/ADDING_SENSORS.md)** - Step-by-step sensor integration
- **[EEPROM Structure Documentation](../../architecture/EEPROM_STRUCTURE.md)** - How pin configurations are stored
- **[Registry System Documentation](../../architecture/REGISTRY_SYSTEM.md)** - Understanding sensor library architecture

---

## Technical Reference

### PinTypeRequirement Enum (C/C++)

Defined in [src/lib/sensor_library/sensor_types.h](../../../src/lib/sensor_library/sensor_types.h):

```cpp
enum PinTypeRequirement {
    PIN_ANALOG,     // Sensor requires analog pin (uses analogRead)
    PIN_DIGITAL,    // Sensor requires digital pin (uses digitalWrite, digitalRead, interrupts)
    PIN_I2C         // Sensor uses I2C bus (pin field must be "I2C")
};
```

Each sensor entry in `sensor_library/sensors/*.h` specifies its required pin type as the last X_SENSOR parameter.

### Pin Validation Logic (Python)

Implemented in [tools/openems_config/platform.py](../../../tools/openems_config/platform.py):

```python
def validate_pin(pin_str: str, platform: str, used_pins: List[str], sensor_pin_requirement: Optional[str] = None) -> Optional[str]:
    """
    Validates a pin against platform limits, reserved pins, used pins, and sensor requirements.
    Returns an error message string if invalid, otherwise None.
    """
    # Parse pin format (analog/digital/i2c)
    parsed_pin = parse_pin(pin_str)

    # Check platform limits
    limits = get_platform_limits(platform)

    # Check reserved pins
    if pin_num in RESERVED_PINS:
        return f"Pin {pin_num} is reserved for {RESERVED_PINS[pin_num]}"

    # Check pin type compatibility with sensor requirements
    if sensor_pin_requirement:
        requirement = sensor_pin_requirement.replace("PIN_", "").lower()
        # Validation logic for analog/digital/i2c compatibility

    return None  # Valid pin
```

---

**End of Guide**
