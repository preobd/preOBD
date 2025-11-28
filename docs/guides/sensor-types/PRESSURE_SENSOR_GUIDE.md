# Pressure Sensor Configuration Guide

**Complete guide to pressure sensor setup in openEMS**

---

## Overview

openEMS supports two types of pressure sensors:

1. **VDO Polynomial Sensors** - VDO brand sensors with non-linear characteristics
2. **Linear Sensors** - Generic 0.5-4.5V output sensors

---

## Quick Selection Guide

### VDO Sensors

**VDO 5-bar oil pressure sensor:**

*Compile-Time:*
```cpp
#define INPUT_0_PIN            A3
#define INPUT_0_APPLICATION    OIL_PRESSURE
#define INPUT_0_SENSOR         VDO_5BAR
```

*Runtime:*
```
SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
ENABLE A3
SAVE
```

**VDO 2-bar boost/intake pressure sensor:**

*Compile-Time:*
```cpp
#define INPUT_1_PIN            A4
#define INPUT_1_APPLICATION    BOOST_PRESSURE
#define INPUT_1_SENSOR         VDO_2BAR
```

*Runtime:*
```
SET A4 APPLICATION BOOST_PRESSURE
SET A4 SENSOR VDO_2BAR
ENABLE A4
SAVE
```

### Generic Linear Sensors

**Generic 0-5 bar (0.5-4.5V output):**

*Compile-Time:*
```cpp
#define INPUT_2_PIN            A5
#define INPUT_2_APPLICATION    BOOST_PRESSURE
#define INPUT_2_SENSOR         GENERIC_BOOST
```

*Runtime:*
```
SET A5 APPLICATION BOOST_PRESSURE
SET A5 SENSOR GENERIC_BOOST
ENABLE A5
SAVE
```

**Freescale MPX4250AP MAP sensor:**

*Compile-Time:*
```cpp
#define INPUT_3_PIN            A6
#define INPUT_3_APPLICATION    BOOST_PRESSURE
#define INPUT_3_SENSOR         MPX4250AP
```

---

## Available Pressure Sensors

### VDO Sensors (Polynomial Calibration)

| Sensor ID | Range | Notes |
|-----------|-------|-------|
| `VDO_5BAR` | 0-5 bar (0-73 PSI) | Oil pressure, fuel pressure |
| `VDO_2BAR` | 0-2 bar (0-29 PSI) | Boost/intake pressure |

**Characteristics:**
- Non-linear voltage output
- Factory calibrated with polynomial equation
- Very accurate (±1% typical)
- Common in European vehicles

### Generic Linear Sensors

| Sensor ID | Range | Voltage |
|-----------|-------|---------|
| `GENERIC_BOOST` | 0-5 bar | 0.5-4.5V |
| `MPX4250AP` | 20-250 kPa | 0.2-4.7V |

**Characteristics:**
- Linear voltage-to-pressure relationship
- Industry standard pinout
- Available from many manufacturers

---

## Wiring

### VDO Pressure Sensors (1-wire)

```
VDO Sensor:
  Signal wire → Analog pin (e.g., A3)
  Ground → Chassis ground (sensor body)

Required: Pull-down resistor (VDO_BIAS_RESISTOR in config.h) from pin → resistor → GND
```

**Note:** VDO pressure sensors are 1-wire resistive sensors that ground through the chassis when properly mounted. Only the signal wire needs to be connected to the microcontroller. Like VDO temperature sensors, they require a pull-down resistor (configurable via VDO_BIAS_RESISTOR in config.h) to measure the resistance changes.

### Generic 3-Wire Sensors

```
Generic Sensor:
  Pin 1 (Signal) → Analog pin
  Pin 2 (+5V)    → 5V supply
  Pin 3 (Ground) → GND

No external resistors needed!
```

### Protection Circuit (Recommended)

For noisy automotive environments:

```
Sensor Signal ----[100Ω]----+---- Analog pin
                            |
                         [100nF]
                            |
                           GND
```

The 100Ω resistor and 100nF capacitor form a low-pass filter to reduce noise.

---

## Understanding Pressure Units

openEMS stores pressure internally in **bar** and converts for display.

**Conversion reference:**
- 1 bar = 14.5038 PSI
- 1 bar = 100 kPa
- 1 bar = 29.53 inHg

**Set display units per sensor:**

*Compile-Time (in advanced_config.h):*
```cpp
#define INPUT_0_UNITS          PSI
```

*Runtime:*
```
SET A3 UNITS PSI
SAVE
```

---

## Typical Pressure Ranges

### Oil Pressure
- **Idle:** 0.5-1 bar (7-15 PSI)
- **Cruise:** 2-3 bar (30-45 PSI)
- **Maximum:** 5-7 bar (70-100 PSI)

**Alarm settings:**
```cpp
// Compile-time
#define INPUT_0_APPLICATION    OIL_PRESSURE
// Default alarm: warn if below 1 bar at operating temperature
```

### Boost Pressure (Turbocharged)
- **Naturally aspirated:** ~1 bar (atmospheric)
- **Low boost:** 0-0.5 bar gauge (0-7 PSI)
- **High boost:** 1-2 bar gauge (15-30 PSI)

### Fuel Pressure
- **Carbureted:** 0.1-0.2 bar (1.5-3 PSI)
- **TBI:** 0.5-1 bar (7-15 PSI)
- **Port injection:** 2.5-4 bar (35-60 PSI)

---

## Configuration Examples

### Example 1: VDO Oil Pressure with Alarm

*Compile-Time:*
```cpp
#define INPUT_0_PIN            A3
#define INPUT_0_APPLICATION    OIL_PRESSURE
#define INPUT_0_SENSOR         VDO_5BAR
```

*Runtime:*
```
SET A3 APPLICATION OIL_PRESSURE
SET A3 SENSOR VDO_5BAR
SET A3 ALARM 1 5
ENABLE A3
SAVE
```

The alarm will trigger if oil pressure drops below 1 bar.

### Example 2: Turbo Boost Pressure

*Compile-Time:*
```cpp
#define INPUT_1_PIN            A4
#define INPUT_1_APPLICATION    BOOST_PRESSURE
#define INPUT_1_SENSOR         VDO_2BAR
```

*Runtime:*
```
SET A4 APPLICATION BOOST_PRESSURE
SET A4 SENSOR VDO_2BAR
SET A4 ALARM -1 1.5
ENABLE A4
SAVE
```

The alarm will trigger if boost exceeds 1.5 bar (-1 means no low alarm).

### Example 3: Multiple Pressure Sensors

*Compile-Time:*
```cpp
// Oil pressure
#define INPUT_0_PIN            A3
#define INPUT_0_APPLICATION    OIL_PRESSURE
#define INPUT_0_SENSOR         VDO_5BAR

// Boost pressure
#define INPUT_1_PIN            A4
#define INPUT_1_APPLICATION    BOOST_PRESSURE
#define INPUT_1_SENSOR         VDO_2BAR

// Fuel pressure
#define INPUT_2_PIN            A5
#define INPUT_2_APPLICATION    FUEL_PRESSURE
#define INPUT_2_SENSOR         VDO_5BAR
```

---

## Troubleshooting

### Sensor reads 0 or maximum constantly

**Check:**
1. Wiring connections - most common issue
2. Verify sensor has power (+5V or +12V as required)
3. Measure voltage at analog pin with multimeter

**Debug with serial output:**
```
INFO A3
```
This shows the raw ADC value and calculated pressure.

### Readings are unstable/noisy

**Solutions:**
1. Add 100nF capacitor at analog pin (signal to ground)
2. Add 100Ω series resistor before capacitor
3. Check ground connections
4. Route wires away from ignition components
5. Use shielded cable for long runs

### Readings are offset

**For VDO sensors:**
This is normal - VDO sensors have a non-zero voltage at 0 pressure. The polynomial calibration handles this correctly.

**For linear sensors:**
Verify the sensor's actual output range matches the calibration:
- Measure voltage at 0 pressure (should be ~0.5V for 0.5-4.5V sensors)
- Measure voltage at known pressure
- If different, may need custom calibration

### Pressure reads high at low values

**Possible causes:**
- Using linear calibration for VDO sensor (use VDO_5BAR, not GENERIC_BOOST)
- Sensor ground not connected properly
- Wrong sensor type selected

---

## Hardware Installation

### Mounting Location
- Mount sensor close to measurement point
- Use appropriate fittings (1/8" NPT typical for oil pressure)
- Use thread sealant on fittings

### Thread Adapters
Common adapters:
- 1/8" NPT (most common)
- M10x1.0 (some European)
- M14x1.5 (older vehicles)

### Pressure Range Selection
- Choose sensor with range 20-50% above expected maximum
- Too large a range reduces resolution
- Too small may damage sensor

---

## Safety Considerations

⚠️ **High Pressure Warning:**
- Fuel and oil systems are under significant pressure
- Always relieve pressure before disconnecting
- Use appropriate rated fittings and hoses
- Test for leaks before operating

⚠️ **Electrical Safety:**
- Verify voltage range matches your microcontroller (5V vs 3.3V)
- Never apply more than 5.5V to sensor
- Ground sensor properly to avoid noise

⚠️ **Fire Hazard:**
- Fuel pressure sensors must use fuel-rated fittings
- Keep electrical connections away from fuel
- Use proper automotive connectors

---

## Custom Calibration

If your pressure sensor isn't in the library, see [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) for:
- Creating linear calibrations
- Polynomial calibrations for non-linear sensors
- Contributing calibrations to the library

---

**For the classic car community.**
