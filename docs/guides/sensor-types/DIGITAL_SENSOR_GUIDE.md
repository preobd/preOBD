# Digital Sensor Configuration Guide

**Float switches, level sensors, and digital inputs for openEMS**

---

## Overview

openEMS supports digital (on/off) sensors for monitoring fluid levels, door switches, and other binary states. The most common application is coolant level monitoring using a float switch.

---

## Quick Start

### Coolant Level Float Switch

*Compile-Time:*
```cpp
#define INPUT_0_PIN            7
#define INPUT_0_APPLICATION    COOLANT_LEVEL
#define INPUT_0_SENSOR         FLOAT_SWITCH
```

*Runtime:*
```
SET 7 APPLICATION COOLANT_LEVEL
SET 7 SENSOR FLOAT_SWITCH
ENABLE 7
SAVE
```

---

## Available Digital Sensors

| Sensor ID | Description |
|-----------|-------------|
| `FLOAT_SWITCH` | Magnetic or mechanical float switch |

---

## Float Switch Types

### Normally Closed (NC)

- Circuit is **closed** when float is down (low level)
- Circuit **opens** when float rises (normal level)
- **Fail-safe:** If wire breaks, alarm triggers

### Normally Open (NO)

- Circuit is **open** when float is down (low level)
- Circuit **closes** when float rises (normal level)
- Not fail-safe: broken wire looks like low level

**Recommendation:** Use NC (Normally Closed) switches for safety-critical applications like coolant level.

---

## Wiring

### Normally Closed Float Switch (Recommended)

```
MCU Pin 7 ─────────────── Float Switch Terminal 1
                                   │
                          Float Switch Terminal 2 ─── GND
```

The microcontroller's internal pull-up resistor is used. When the float rises (normal level), the switch opens and the pin reads HIGH.

### External Pull-Up (More Reliable)

```
+5V ──[10kΩ]──┬── MCU Pin 7
              │
              └── Float Switch ── GND
```

Using an external 10kΩ pull-up provides more reliable operation in noisy environments.

### With Noise Filtering

```
+5V ──[10kΩ]──┬── MCU Pin 7
              │
           [100nF]
              │
              └── Float Switch ── GND
```

The 100nF capacitor filters electrical noise from the engine bay.

---

## Configuration Examples

### Example 1: Basic Coolant Level

*Compile-Time:*
```cpp
#define INPUT_0_PIN            7
#define INPUT_0_APPLICATION    COOLANT_LEVEL
#define INPUT_0_SENSOR         FLOAT_SWITCH
```

*Runtime:*
```
SET 7 APPLICATION COOLANT_LEVEL
SET 7 SENSOR FLOAT_SWITCH
ENABLE 7
SAVE
```

### Example 2: Multiple Level Sensors

*Compile-Time:*
```cpp
// Coolant level
#define INPUT_0_PIN            7
#define INPUT_0_APPLICATION    COOLANT_LEVEL
#define INPUT_0_SENSOR         FLOAT_SWITCH

// Fuel level (if using float switch)
#define INPUT_1_PIN            8
#define INPUT_1_APPLICATION    COOLANT_LEVEL  // Reuse for fuel
#define INPUT_1_SENSOR         FLOAT_SWITCH
```

*Runtime:*
```
SET 7 APPLICATION COOLANT_LEVEL
SET 7 SENSOR FLOAT_SWITCH
SET 7 NAME COOL
ENABLE 7

SET 8 APPLICATION COOLANT_LEVEL
SET 8 SENSOR FLOAT_SWITCH
SET 8 NAME FUEL
ENABLE 8

SAVE
```

---

## Display and Alarms

### Display Output

Digital sensors display as:
- **OK** - Normal level (switch open for NC)
- **LOW** - Low level (switch closed for NC)

### Alarm Behavior

When the float switch indicates low level:
1. Display shows "LOW"
2. Alarm triggers (if enabled)
3. OBD-II diagnostic code can be set

---

## Hardware Selection

### Recommended Float Switches

**For coolant expansion tank:**
- Brass or stainless steel body (heat resistant)
- Normally closed (fail-safe)
- M10 or M12 thread
- 100°C+ temperature rating

**For fuel tank:**
- Stainless steel or plastic body
- Fuel-resistant materials
- Appropriate thread for tank fitting

### Mounting Considerations

- Mount vertically for accurate level detection
- Ensure float moves freely
- Consider thermal expansion of fluids
- Allow for coolant level changes when hot vs cold

---

## Troubleshooting

### Always shows LOW

**Check:**
1. Wiring connections
2. Float switch orientation (may be upside down)
3. Float movement (debris blocking?)
4. Measure resistance with multimeter:
   - NC switch should show ~0Ω when float down
   - NC switch should show open circuit when float up

### Always shows OK

**Check:**
1. Switch is actually connected
2. Pin number is correct
3. Switch is NC (not NO)
4. Float is not stuck up

### Intermittent/Flickering

**Causes:**
- Loose connections
- Fluid sloshing
- Electrical noise

**Solutions:**
- Add 100nF capacitor
- Use external pull-up resistor
- Secure all connections
- Add debounce delay in code (advanced)

---

## Alternative Applications

Float switches can be used for:
- Coolant level monitoring
- Washer fluid level
- Fuel level (low warning)
- Oil level (in sump)
- Brake fluid level
- Power steering fluid level

---

## Safety Considerations

⚠️ **Coolant System:**
- Never open radiator cap when hot
- Use appropriate temperature-rated switches
- Mount securely to prevent leaks

⚠️ **Fuel System:**
- Use fuel-rated switches and wiring
- Keep electrical connections away from fuel
- Ground properly to prevent static

⚠️ **Fail-Safe Design:**
- Use NC switches for safety-critical applications
- A broken wire should trigger alarm, not mask problem

---

## Custom Digital Inputs

For other digital sensors (door switches, limit switches, etc.), use the same FLOAT_SWITCH sensor type:

```cpp
#define INPUT_0_PIN            9
#define INPUT_0_APPLICATION    COOLANT_LEVEL  // Reuse application
#define INPUT_0_SENSOR         FLOAT_SWITCH
```

Or via runtime:
```
SET 9 APPLICATION COOLANT_LEVEL
SET 9 SENSOR FLOAT_SWITCH
SET 9 NAME DOOR
ENABLE 9
SAVE
```

The display name can be customized to match your application.

---

**For the classic car community.**
