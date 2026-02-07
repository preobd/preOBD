# RGB LED Status Indicator Guide

The RGB LED status indicator provides visual feedback about system state using a single LED with PWM-controlled color mixing and effects.

## Overview

Starting with version 0.7.0, preOBD uses a single RGB LED for status indication instead of three separate LEDs. This provides:

- **Color-coded states**: Green (normal), Yellow (warning), Red (alarm), Blue (CONFIG mode)
- **Visual effects**: Solid colors, blinking, and breathing/pulsing
- **Priority system**: Alarms always override mode indication
- **Customization**: All colors and effects configurable in code

## Hardware Requirements

### RGB LED Type

Use a **common-cathode** RGB LED (recommended for 3.3V systems):
- 4 pins: Red, Green, Blue, Common (GND)
- Forward voltage: Red ~2.0V, Green/Blue ~3.0V
- Forward current: 20mA per channel (typical)

**Common-anode** LEDs are also supported via the `RGB_COMMON_ANODE` config flag.

### Pin Assignment

Default pins on Teensy 4.1:
- **Pin 6**: Red channel (PWM)
- **Pin 7**: Green channel (PWM)
- **Pin 8**: Blue channel (PWM)

**Important**: These pins **must** be PWM-capable. The default pins (6, 7, 8) work on Teensy 4.1, Teensy 4.0, and most Arduino boards.

### Wiring Diagram

```
Common-Cathode RGB LED:

  RGB LED          Teensy 4.1
  ┌─────┐
  │  R  ├───[220Ω]───► Pin 6 (PWM)
  │  G  ├───[220Ω]───► Pin 7 (PWM)
  │  B  ├───[220Ω]───► Pin 8 (PWM)
  │ GND ├──────────────► GND
  └─────┘
```

### Current Limiting Resistors

Calculate resistor values based on your LED specs:

```
R = (V_supply - V_forward) / I_forward

Teensy 4.1 (3.3V):
- Red:   R = (3.3V - 2.0V) / 0.020A = 65Ω  → use 68Ω or 100Ω
- Green: R = (3.3V - 3.0V) / 0.020A = 15Ω  → use 22Ω or 33Ω
- Blue:  R = (3.3V - 3.0V) / 0.020A = 15Ω  → use 22Ω or 33Ω

Arduino 5V:
- Red:   R = (5.0V - 2.0V) / 0.020A = 150Ω → use 150Ω or 180Ω
- Green: R = (5.0V - 3.0V) / 0.020A = 100Ω → use 100Ω or 120Ω
- Blue:  R = (5.0V - 3.0V) / 0.020A = 100Ω → use 100Ω or 120Ω
```

**Safe default**: Use 220Ω for all channels (slightly dimmer but works universally)

## LED States and Colors

### Alarm System States

| State | Color | Effect | Meaning |
|-------|-------|--------|---------|
| **NORMAL** | Green | Solid | All sensors within normal range |
| **WARNING** | Yellow/Orange | Blinking (1Hz) | One or more sensors at warning level (90% of alarm threshold) |
| **ALARM** | Red | Fast blink (2.5Hz) | One or more sensors in alarm state |

### System Mode States

| Mode | Color | Effect | Priority |
|------|-------|--------|----------|
| **RUN** | (Alarm color) | (Alarm effect) | Alarms control the LED |
| **CONFIG** | Blue | Breathing/Pulse | Lower priority - overridden by alarms |

**Priority order**: ALARM (highest) > WARNING > MODE > IDLE (lowest)

This means if you're in CONFIG mode (blue pulsing) and a sensor triggers a warning, the LED will change to yellow blinking and stay that way until the warning clears.

## Configuration

### Pin Assignment

Edit [src/config.h](../../config.h):

```cpp
#ifdef ENABLE_LED
    #define RGB_PIN_R 6       // Red channel (PWM capable)
    #define RGB_PIN_G 7       // Green channel (PWM capable)
    #define RGB_PIN_B 8       // Blue channel (PWM capable)

    #define RGB_COMMON_ANODE false  // false = common cathode (recommended)
                                     // true = common anode
#endif
```

### Color Customization

Edit [src/lib/rgb_led.h](../../lib/rgb_led.h) to customize colors:

```cpp
// Normal operation (default: green)
#define RGB_COLOR_NORMAL_R   0
#define RGB_COLOR_NORMAL_G   255
#define RGB_COLOR_NORMAL_B   0

// Warning alarm (default: yellow/orange)
#define RGB_COLOR_WARNING_R  255
#define RGB_COLOR_WARNING_G  180
#define RGB_COLOR_WARNING_B  0

// Critical alarm (default: red)
#define RGB_COLOR_ALARM_R    255
#define RGB_COLOR_ALARM_G    0
#define RGB_COLOR_ALARM_B    0

// CONFIG mode (default: blue)
#define RGB_COLOR_CONFIG_R   0
#define RGB_COLOR_CONFIG_G   0
#define RGB_COLOR_CONFIG_B   255
```

**Use cases for customization**:
- Colorblind-friendly palettes
- Personal preferences
- Matching vehicle interior lighting

### Effect Control

Disable effects if blinking is undesirable (photosensitivity, preferences):

```cpp
#define RGB_ALARM_USE_BLINK  true   // false = solid colors for alarms
#define RGB_CONFIG_USE_PULSE true   // false = solid blue in CONFIG mode
```

### Timing Adjustment

Adjust blink/pulse speeds in [src/lib/rgb_led.h](../../lib/rgb_led.h):

```cpp
#define RGB_BLINK_PERIOD_MS 500     // Standard blink (1Hz)
#define RGB_FAST_BLINK_MS   200     // Fast blink (2.5Hz)
#define RGB_PULSE_PERIOD_MS 2000    // Breathing cycle (0.5Hz)
```

## Build Configuration

Enable the LED indicator in [platformio.ini](../../../platformio.ini):

```ini
[standard_features]
build_flags =
    -D ENABLE_LED  # RGB LED status indicator
```

Compile and flash:
```bash
pio run -e teensy41 -t upload
```

## Troubleshooting

### LED doesn't light up

**Check wiring**:
- Common pin connected to GND (common-cathode) or VCC (common-anode)
- Current limiting resistors in place
- Correct pin assignments in config.h

**Check pin conflicts**:
- At startup, preOBD validates pins don't conflict
- Check serial output for "RGB LED Red pin X conflict" warnings

**Verify PWM capability**:
- Pins 6, 7, 8 are PWM on Teensy 4.1
- Check your board's PWM pin list if using different hardware

### Wrong colors or dim

**Common-anode vs common-cathode**:
- If colors are inverted, change `RGB_COMMON_ANODE` in config.h

**Resistor values**:
- Too high = dim LED
- Too low = excessive current (may damage LED or pin)
- Try 220Ω as a safe starting point

**Power supply**:
- Ensure adequate current available (60mA max for all channels on)
- Check voltage at LED pins (should be close to 3.3V on Teensy)

### Effects not working (LED stays solid)

**Verify build flag**:
```bash
pio run -e teensy41 -v | grep ENABLE_LED
```

**Check loop timing**:
- `updateRGBLed()` must be called in main loop
- Verify no blocking delays in your code

**Effect disabled**:
- Check `RGB_ALARM_USE_BLINK` and `RGB_CONFIG_USE_PULSE` settings

### LED flickers

**PWM frequency**:
- ESP32: Configured at 5kHz (should be smooth)
- Teensy/Arduino: Uses default PWM frequency
- Some people are sensitive to certain frequencies

**CPU overload**:
- Check loop execution time isn't excessive
- Reduce other output rates if needed

## Platform-Specific Notes

### Teensy 4.1 / 4.0 / 3.6
- Pins 6, 7, 8 all support PWM
- 3.3V output
- Use ~68Ω (red) and ~22Ω (green/blue) resistors for full brightness
- Or 220Ω for all channels (dimmer but safe)

### Arduino Mega 2560
- Most pins support PWM
- 5V output - use higher value resistors
- Check pinout diagram for PWM pins

### ESP32-S3
- LEDC peripheral provides 16 channels
- Configurable PWM frequency (default: 5kHz)
- 3.3V output

### Arduino Uno
- **Limited PWM pins** - may need to change pin assignments
- Only pins 3, 5, 6, 9, 10, 11 support PWM
- Consider pins 9, 10, 11 if available

## Future Extensions

The RGB LED module supports additional uses via the priority system:

```cpp
// Example: SD card write indicator
rgbLedSolid(RGB_COLOR_ACTIVITY, PRIORITY_ACTIVITY);  // Brief cyan flash
delay(10);
rgbLedRelease(PRIORITY_ACTIVITY);  // Release control
```

Potential future features:
- **SD write indicator**: Brief flash during logging
- **Bluetooth pairing**: Cyan pulse during pairing
- **Communication activity**: Quick flashes on data transmission
- **Battery status**: Color indication of voltage level

## See Also

- [Build Configuration Guide](../configuration/BUILD_CONFIGURATION_GUIDE.md) - Feature flags
- [Alarm System Guide](../configuration/ALARM_SYSTEM_GUIDE.md) - Alarm thresholds
- [Pin Requirements Guide](PIN_REQUIREMENTS_GUIDE.md) - Pin conflict detection
