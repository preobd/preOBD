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

The LED is enabled by wiring its pins in your board's wiring file,
`boards/<board>/wiring.ini` (copy `wiring.ini.example`):

```ini
[teensy41]
wiring =
    -D RGB_PIN_R=6       ; Red channel (PWM capable)
    -D RGB_PIN_G=5       ; Green channel (PWM capable)
    -D RGB_PIN_B=4       ; Blue channel (PWM capable)
;   -D RGB_COMMON_ANODE  ; add for a common-anode LED (default: common cathode)
```

Omit the pins and the LED code is left out of the build. Rebuild after changing
`wiring.ini` (a clean build — PlatformIO doesn't notice the change on its own).

### Color Customization

Edit [src/drivers/rgb_led.h](../../../src/drivers/rgb_led.h) to customize colors:

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

Adjust blink/pulse speeds in [src/drivers/rgb_led.h](../../../src/drivers/rgb_led.h):

```cpp
#define RGB_BLINK_PERIOD_MS 500     // Standard blink (1Hz)
#define RGB_FAST_BLINK_MS   200     // Fast blink (2.5Hz)
#define RGB_PULSE_PERIOD_MS 2000    // Breathing cycle (0.5Hz)
```

## Build Configuration

There is no separate flag to set: wiring the `RGB_PIN_*` pins in `wiring.ini`
(above) includes the LED code. Compile and flash:
```bash
pio run -e teensy41 -t upload
```

## Troubleshooting

### LED doesn't light up

**Check wiring**:
- Common pin connected to GND (common-cathode) or VCC (common-anode)
- Current limiting resistors in place
- Correct pin assignments in your `wiring.ini`

**Check pin conflicts**:
- At startup, preOBD validates pins don't conflict
- Check serial output for "RGB LED Red pin X conflict" warnings

**Verify PWM capability**:
- Pins 4, 5 and 6 are PWM on Teensy 4.1
- Check your board's PWM pin list if using different hardware

### Wrong colors or dim

**Common-anode vs common-cathode**:
- If colors are inverted, add or remove `-D RGB_COMMON_ANODE` in your `wiring.ini`

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
pio run -e teensy41 -v | grep INCLUDE_LED
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
