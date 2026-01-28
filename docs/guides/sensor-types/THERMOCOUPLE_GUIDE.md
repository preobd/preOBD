# Thermocouple Sensor Guide

**K-Type thermocouple setup with MAX6675 and MAX31855 amplifiers**

---

## Overview

Thermocouples are ideal for high-temperature measurements like Cylinder Head Temperature (CHT) and Exhaust Gas Temperature (EGT). openEMS supports K-type thermocouples with two amplifier chips:

| Sensor | Temperature Range | Resolution | Notes |
|--------|------------------|------------|-------|
| `MAX6675` | 0 to 1024°C | 0.25°C | Common, inexpensive |
| `MAX31855` | -200 to 1350°C | 0.25°C | Higher range, cold junction compensation |

---

## Quick Start

### CHT (Cylinder Head Temperature)
```
SET 6 CHT MAX6675
SAVE
```

### EGT (Exhaust Gas Temperature)
```
SET 7 EGT MAX31855
SAVE
```

---

## Sensor Selection

### MAX6675

**Best for:**
- CHT (Cylinder Head Temperature)
- General high-temperature monitoring
- Budget-conscious builds

**Specifications:**
- Range: 0°C to 1024°C (32°F to 1875°F)
- Resolution: 0.25°C
- Accuracy: ±3°C typical
- Conversion time: ~220ms
- Interface: SPI (3-wire)

### MAX31855

**Best for:**
- EGT (Exhaust Gas Temperature)
- Applications requiring cold junction compensation
- Extended temperature range

**Specifications:**
- Range: -200°C to 1350°C (-328°F to 2462°F)
- Resolution: 0.25°C
- Accuracy: ±2°C typical
- Conversion time: ~100ms
- Interface: SPI (3-wire)
- Built-in cold junction compensation

---

## Wiring

### SPI Connections

Both chips use SPI communication. The SPI bus is shared, but each thermocouple needs its own CS (Chip Select) pin.

```
MAX6675/MAX31855 Module:
  VCC → 5V (or 3.3V for 3.3V boards)
  GND → GND
  SCK → Pin 13 (SPI Clock - shared)
  SO  → Pin 12 (SPI MISO - shared)
  CS  → Your configured pin (unique per sensor)
```

### Multiple Thermocouples

Each thermocouple needs a unique CS pin, but they share SCK and SO:

```
                    Arduino/Teensy
                    +------------+
MAX6675 #1  CS  →  | Pin 6      |
MAX31855 #1 CS  →  | Pin 7      |
MAX31855 #2 CS  →  | Pin 8      |
                    |            |
All modules SCK →  | Pin 13     |  (shared)
All modules SO  →  | Pin 12     |  (shared)
                    +------------+
```

### Pin Selection

Use any digital pin for CS. The pin you specify in configuration is the CS pin:

```
SET 6 CHT MAX6675      # Pin 6 is CS for this thermocouple
SET 7 EGT MAX31855     # Pin 7 is CS for this thermocouple
```

**Avoid these pins for CS:**
- Pin 12 (MISO - used for data)
- Pin 13 (SCK - used for clock)
- Pin 10 (SS - can cause issues on some boards)

---

## Thermocouple Selection

### K-Type Thermocouple Specs

K-type thermocouples use chromel-alumel alloy and are the most common type for automotive use.

**Wire colors (standard):**
- **Positive (+):** Yellow
- **Negative (-):** Red

**Important:** Polarity matters! Reversed wires give incorrect readings.

### Probe Types

| Type | Best For | Notes |
|------|----------|-------|
| **Ring terminal** | CHT (under spark plug) | Most common for CHT |
| **Exposed tip** | Fast response (EGT) | Fastest response, less durable |
| **Grounded tip** | General purpose | Good balance of speed/durability |
| **Ungrounded tip** | Electrically isolated | Slower response, better noise immunity |

### Temperature Limits

| Application | Typical Range | Recommended Sensor |
|-------------|--------------|-------------------|
| CHT | 100-300°C | MAX6675 or MAX31855 |
| EGT | 300-900°C | MAX31855 (higher range) |
| Oil cooler | 80-150°C | Either |

---

## Configuration Examples

### Single CHT Sensor

```
SET 6 CHT MAX6675
SET 6 ALARM 50 260
SAVE
```

Alarm triggers below 50°C (cold) or above 260°C (overheating).

### Multiple EGT Sensors

For multi-cylinder EGT monitoring:

```
SET 6 EGT MAX31855
SET 7 EGT MAX31855
SET 8 EGT MAX31855
SET 9 EGT MAX31855
SAVE
```

### CHT + EGT Combination

```
SET 6 CHT MAX6675
SET 7 EGT MAX31855
SET 6 ALARM 50 260
SET 7 ALARM 200 850
SAVE
```

---

## Troubleshooting

### Reading Shows 0°C or -1°C

**Possible causes:**
1. Thermocouple not connected
2. Wiring reversed (swap + and -)
3. Broken thermocouple wire
4. Bad solder joint on module

**Check:**
- Verify thermocouple continuity with multimeter
- Check all solder joints
- Swap + and - wires and test

### Reading is Erratic or Jumpy

**Possible causes:**
1. Electrical noise pickup
2. Poor ground connection
3. Long thermocouple wires acting as antenna

**Solutions:**
1. Use shielded thermocouple cable
2. Keep wires away from ignition components
3. Add 100nF capacitor across thermocouple inputs
4. Improve ground connection

### Reading is Offset by Fixed Amount

**Possible causes:**
1. Cold junction compensation issue (MAX31855)
2. Thermocouple type mismatch

**Solutions:**
1. Ensure module is at stable ambient temperature
2. Verify you're using K-type thermocouple (not J, T, or E type)

### Reading Stuck at Maximum (1024°C or 1350°C)

**Possible causes:**
1. Open thermocouple circuit
2. Thermocouple burned out
3. CS pin conflict

**Check:**
- Test thermocouple continuity
- Verify CS pin is unique (not shared with another device)

### SPI Communication Errors

**Symptoms:**
- Reading shows NaN or error value
- Intermittent readings

**Solutions:**
1. Check VCC voltage (5V for 5V boards, 3.3V for 3.3V boards)
2. Keep SPI wires short (<30cm)
3. Add 10µF capacitor near module VCC
4. Check for loose connections

---

## Hardware Recommendations

### Recommended Modules

**Budget option:**
- Generic MAX6675 module (~$3-5)
- Works well for CHT applications

**Quality option:**
- Adafruit MAX31855 breakout (#269)
- Better build quality, proper ESD protection

### Thermocouple Recommendations

**CHT (ring terminal):**
- Standard 14mm spark plug ring thermocouple
- 1-2 meter cable length typical

**EGT (exposed tip):**
- 1/8" NPT fitting for exhaust manifold
- High-temperature wire insulation (fiberglass)

---

## Read Timing

openEMS automatically handles thermocouple read timing:

| Sensor | Min Read Interval | openEMS Default |
|--------|------------------|-----------------|
| MAX6675 | 220ms | 250ms |
| MAX31855 | 100ms | 100ms |

The system won't read faster than the chip can convert, preventing invalid readings.

---

## Platform Notes

### 3.3V Boards (Teensy, Due, ESP32)

Both MAX6675 and MAX31855 work with 3.3V logic:

```
VCC → 3.3V
```

### 5V Boards (Arduino Uno, Mega)

Use 5V power:

```
VCC → 5V
```

---

## See Also

- [SENSOR_SELECTION_GUIDE.md](SENSOR_SELECTION_GUIDE.md) - Complete sensor catalog
- [ADVANCED_CALIBRATION_GUIDE.md](../configuration/ADVANCED_CALIBRATION_GUIDE.md) - Custom sensor setup
- [PIN_REQUIREMENTS_GUIDE.md](../hardware/PIN_REQUIREMENTS_GUIDE.md) - Pin type requirements

