# Build Configuration Guide

This guide explains how openEMS separates compile-time feature selection from runtime configuration, and how to customize builds for your specific needs.

## Quick Reference

**TL;DR**:
- **platformio.ini** = What features compile into firmware (ENABLE_CAN, ENABLE_SD_LOGGING, etc.)
- **src/config.h** = Hardware pins, timing intervals, default units (easy user customization)

## Table of Contents

1. [Understanding the Two-Tier System](#understanding-the-two-tier-system)
2. [Output Module Availability](#output-module-availability-compile-time-vs-runtime)
3. [Choosing a Build Environment](#choosing-a-build-environment)
4. [Creating Custom Environments](#creating-custom-environments)
5. [Library Dependencies](#library-dependencies)
6. [Memory Optimization](#memory-optimization)
7. [Troubleshooting](#troubleshooting)

## Understanding the Two-Tier System

openEMS uses a clear separation between compile-time and runtime configuration:

### Compile-Time Configuration (platformio.ini)

**Purpose**: Controls which code modules get compiled into the firmware

**What goes here**:
- Feature compilation flags (`ENABLE_CAN`, `ENABLE_SD_LOGGING`, etc.)
- Platform-specific flags (`USE_FLEXCAN_NATIVE`, `TEENSY_41`)
- Library dependencies (conditional per feature)
- Build optimization settings (`-O2`, `-Wall`)

**Why**: If you disable a feature at compile-time, the code and libraries are completely excluded, saving flash/RAM.

**Example**:
```ini
[env:teensy41]
build_flags =
    -D ENABLE_CAN          # CAN code compiles in
    -D ENABLE_SD_LOGGING   # SD logging compiles in
    # ENABLE_REALDASH not defined → RealDash code excluded
```

### Runtime Configuration (src/config.h)

**Purpose**: Easy user customization without understanding build systems

**What goes here**:
- Hardware pin assignments (`MODE_BUTTON`, `CAN_CS`, `SD_CS_PIN`)
- Timing intervals (`SENSOR_READ_INTERVAL_MS`, `LCD_UPDATE_INTERVAL_MS`)
- Default units (`DEFAULT_TEMPERATURE_UNITS`, `DEFAULT_PRESSURE_UNITS`)
- Physical constants (`DEFAULT_BIAS_RESISTOR`, `SEA_LEVEL_PRESSURE_HPA`)

**Why**: Most users are comfortable editing .h files with clear comments, and these settings don't affect code compilation.

**Example**:
```cpp
// config.h
#define MODE_BUTTON 5
#define BUZZER 3
#define DEFAULT_TEMPERATURE_UNITS "CELSIUS"
```

## Output Module Availability: Compile-time vs Runtime

openEMS uses a two-tier system for output modules:

### Compile-time Control (platformio.ini)

Build flags like `-D ENABLE_CAN` control whether output code is included:

- **If ENABLE_CAN is defined** → CAN implementation compiles → CAN library linked
- **If ENABLE_CAN is NOT defined** → CAN code excluded → ~20KB flash saved

**Check what's compiled in**: Use `LIST OUTPUTS` serial command

### Runtime Control (EEPROM)

Once compiled in, outputs can be enabled/disabled without reflashing:

```
SET OUTPUT CAN ENABLED       # Enable CAN output
SET OUTPUT CAN DISABLED      # Disable CAN output
SET OUTPUT CAN INTERVAL 100  # Change update rate
```

Settings persist across power cycles in EEPROM.

### Why Two Tiers?

- **Flash optimization**: Don't compile unused features (critical for Arduino Uno)
- **Runtime flexibility**: Toggle features without reflashing (Teensy/Mega)
- **Default builds**: Modern boards include all features, users disable what they don't need

### Example Workflow

1. Build with `-D ENABLE_CAN` in platformio.ini
2. Flash to Teensy 4.1
3. Use `SET OUTPUT CAN DISABLED` to temporarily disable CAN
4. Later, use `SET OUTPUT CAN ENABLED` to re-enable (no reflashing needed)

## Choosing a Build Environment

### Pre-configured Environments

| Environment | Platform | Flash | Features | Notes |
|-------------|----------|-------|----------|-------|
| **teensy41** | Teensy 4.1 | 8MB | All | **Recommended** - Built-in SD card |
| teensy40 | Teensy 4.0 | 2MB | All | External SD module needed |
| teensy36 | Teensy 3.6 | 1MB | All | Older platform |
| mega2560 | Arduino Mega | 256KB | All | Good for prototyping |
| uno_static | Arduino Uno | 32KB | LCD + Serial + Alarms only | Memory-constrained |
| debug | Teensy 4.1 | 8MB | All + Debug | Debug symbols enabled |

### Build Commands

```bash
# Recommended platform
pio run -e teensy41

# Other platforms
pio run -e teensy40
pio run -e mega2560
pio run -e debug
```

### Flash Usage Estimates

| Platform | Full Build | % Used | Remaining |
|----------|------------|--------|-----------|
| Teensy 4.1 | 145KB | 1.8% | 7.9MB |
| Teensy 4.0 | 144KB | 7.0% | 1.9MB |
| Teensy 3.6 | ~140KB | 14% | ~860KB |
| Mega 2560 | 132KB | 52% | 122KB |
| Uno (minimal) | N/A | ~77% | ~8KB |

**Conclusion**: Teensy 4.x and Mega 2560 have plenty of resources. No need for minimal builds.

## Creating Custom Environments

### Example: CAN-Only Build

If you only need CAN output (no SD logging, no RealDash):

```ini
[custom_can_features]
build_flags =
    -D ENABLE_CAN
    -D ENABLE_LCD
    -D ENABLE_SERIAL_OUTPUT
    -D ENABLE_ALARMS

[env:teensy41_can_only]
platform = teensy
board = teensy41
build_flags =
    -D TEENSY_41
    -D USE_FLEXCAN_NATIVE
    -D SD_CS_PIN=254
    ${custom_can_features.build_flags}
    -O2
    -Wall
lib_deps =
    ${core_libs.lib_deps}
    ${display_libs.lib_deps}
    ${can_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
    https://github.com/tonton81/FlexCAN_T4.git
    https://github.com/tonton81/WDT_T4.git
```

**Build**:
```bash
pio run -e teensy41_can_only
```

### Example: OLED Display Instead of LCD

```ini
[oled_features]
build_flags =
    -D ENABLE_CAN
    -D ENABLE_SERIAL_OUTPUT
    -D ENABLE_SD_LOGGING
    -D ENABLE_OLED           # Use OLED instead of LCD
    -D ENABLE_ALARMS

[env:teensy41_oled]
platform = teensy
board = teensy41
build_flags =
    -D TEENSY_41
    -D USE_FLEXCAN_NATIVE
    -D SD_CS_PIN=254
    ${oled_features.build_flags}
    -O2
    -Wall
lib_deps =
    ${core_libs.lib_deps}
    # Add OLED library here instead of LCD
    ${can_libs.lib_deps}
    ${sd_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
    https://github.com/tonton81/FlexCAN_T4.git
    https://github.com/tonton81/WDT_T4.git
```

## Library Dependencies

openEMS uses modular library dependency groups for clarity:

### Standard Feature Set

```ini
[standard_features]
build_flags =
    -D ENABLE_CAN           # CAN bus output
    -D ENABLE_REALDASH      # RealDash protocol output
    -D ENABLE_SERIAL_OUTPUT # CSV serial output
    -D ENABLE_SD_LOGGING    # SD card data logging
    -D ENABLE_LCD           # LCD display
    -D ENABLE_ALARMS        # Alarm system
    -D ENABLE_LEDS          # LED indicators
    -D ENABLE_TEST_MODE     # Test mode for development
    -D ENABLE_BME280        # BME280 environmental sensor
```

### Available Library Groups

```ini
[core_libs]        # Always needed
lib_deps = adafruit/Adafruit Unified Sensor

[display_libs]     # LCD display
lib_deps = marcoschwartz/LiquidCrystal_I2C

[can_libs]         # MCP2515 CAN (external chip for Mega/Uno/Due)
lib_deps = sandeepmistry/CAN

[sd_libs]          # SD card logging
lib_deps = greiman/SdFat

[sensor_libs]      # BME280 sensor
lib_deps = adafruit/Adafruit BME280 Library

[eeprom_libs]      # JSON configuration (not for static builds)
lib_deps = bblanchon/ArduinoJson
```

### Platform-Specific CAN Libraries

CAN support varies by platform:

| Platform | CAN Implementation | Library | Notes |
|----------|-------------------|---------|-------|
| **Teensy 4.x/3.x** | Native FlexCAN | FlexCAN_T4 | Built-in CAN controller, no external chip needed |
| **ESP32** | Native TWAI | ESP32-TWAI-CAN | Built-in CAN controller, **external transceiver required** |
| **Mega/Uno/Due** | External MCP2515 | sandeepmistry/CAN | Requires MCP2515 CAN controller via SPI |

**Example - ESP32 with native CAN:**
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
build_flags =
    ${standard_features.build_flags}  # Includes ENABLE_CAN
lib_deps =
    ${core_libs.lib_deps}
    ${display_libs.lib_deps}
    ${sd_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
    https://github.com/handmade0octopus/ESP32-TWAI-CAN.git  # ESP32 native CAN
```

**Example - Teensy 4.1 with native CAN:**
```ini
[env:teensy41]
platform = teensy
board = teensy41
build_flags =
    -D TEENSY_41
    -D USE_FLEXCAN_NATIVE  # Enable native FlexCAN
    ${standard_features.build_flags}
lib_deps =
    ${core_libs.lib_deps}
    ${display_libs.lib_deps}
    ${sd_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
    https://github.com/tonton81/FlexCAN_T4.git  # Teensy native CAN
```

**Example - Arduino Mega with MCP2515:**
```ini
[env:mega2560]
platform = atmelavr
board = megaatmega2560
build_flags =
    ${standard_features.build_flags}
lib_deps =
    ${core_libs.lib_deps}
    ${display_libs.lib_deps}
    ${can_libs.lib_deps}  # MCP2515 via SPI
    ${sd_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
```

### Platform-Specific Bluetooth Support

Bluetooth support is platform-dependent and requires no build flags:

| Platform | Bluetooth Support | Implementation | Library Required |
|----------|------------------|----------------|------------------|
| **ESP32** | Bluetooth Classic | Native ESP32 BluetoothSerial | Built-in (no external library) |
| **Teensy/Mega/AVR** | Via UART module | HC-05, HM-10, or similar | No library (uses Serial1/Serial2) |

**ESP32 Bluetooth Classic** is automatically detected and enabled at runtime if the platform is ESP32. No build flags needed.

**UART Bluetooth modules** (HC-05, HM-10) can be connected to any hardware serial port (Serial1, Serial2) on Teensy, Mega, or AVR platforms. These are transparent serial bridges - no special code or libraries required.

**Example - ESP32 with native Bluetooth:**
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
build_flags =
    -D ESP32  # Automatically enables Bluetooth Classic support
    ${standard_features.build_flags}
```

At runtime, ESP32 will initialize Bluetooth Classic with device name "openEMS" and register it as a transport for RealDash and serial commands.

**Example - Teensy with HC-05 Bluetooth module:**
No special configuration needed - just wire HC-05 TX/RX to Serial2 (pins 7/8 on Teensy 4.1). The transport system automatically makes Serial2 available for commands and RealDash output.

### Conditional Dependencies

Only include libraries your build needs:

```ini
lib_deps =
    ${core_libs.lib_deps}      # Always
    ${display_libs.lib_deps}   # If ENABLE_LCD defined
    ${can_libs.lib_deps}       # If ENABLE_CAN + external MCP2515 (Mega/Uno/Due)
    ${sd_libs.lib_deps}        # If ENABLE_SD_LOGGING defined
    ${sensor_libs.lib_deps}    # Always (BME280)
    ${eeprom_libs.lib_deps}    # If NOT USE_STATIC_CONFIG
```

## Memory Optimization

### For Memory-Constrained Boards (Arduino Uno)

1. **Use static configuration** (`USE_STATIC_CONFIG`) - Excludes JSON library (~4-8KB)
2. **Disable unused outputs** - Only enable LCD + Serial + Alarms
3. **Exclude SD and CAN** - Saves ~30KB combined
4. **Disable test mode** - Saves ~4KB

**Example**:
```ini
[env:uno_static]
build_flags =
    -D USE_STATIC_CONFIG
    -D ENABLE_LCD
    -D ENABLE_SERIAL_OUTPUT
    -D ENABLE_ALARMS
    # No CAN, SD, or RealDash
lib_deps =
    ${core_libs.lib_deps}
    ${display_libs.lib_deps}
    ${sensor_libs.lib_deps}
    # NO eeprom_libs - saves 4-8KB
```

### Flash Savings by Feature

| Feature Disabled | Flash Saved |
|------------------|-------------|
| ArduinoJson | 4-8KB |
| SD Logging | ~15KB |
| CAN Output | ~15KB |
| RealDash | ~8KB |
| Test Mode | ~4KB |

## Troubleshooting

### Problem: "undefined reference to..." errors

**Cause**: Library not included in `lib_deps`

**Fix**: Add appropriate library group to environment

**Example**:
```
undefined reference to `CAN.begin()'
```
Add `${can_libs.lib_deps}` to your environment.

### Problem: Build too large for Arduino Uno

**Cause**: Too many features enabled

**Fix**: Use `uno_static` environment with minimal features

### Problem: CAN not working

**Cause**: `ENABLE_CAN` not defined in build_flags

**Fix**: Use `teensy41` or another environment with full features, or create custom environment with `-D ENABLE_CAN`

### Problem: Output doesn't appear in LIST OUTPUTS

**Cause**: Output wasn't compiled into firmware

**Fix**: Check platformio.ini build_flags. If the output's ENABLE_* flag isn't defined, it won't be available.

**Solution**: Use an environment that includes the output, or create a custom environment.

### Problem: Pin assignments not working

**Cause**: Pin may be overridden in platformio.ini

**Check**: Look for `-D PIN_NAME=value` in platformio.ini build_flags

**Example**: Teensy 4.1 overrides SD_CS_PIN to 254 for built-in SD card

## Best Practices

1. **Start with standard environments** - Use teensy41, teensy40, or mega2560 as-is
2. **Only create custom environments if needed** - Modern boards have plenty of resources
3. **Use runtime configuration** - Toggle outputs with serial commands, not reflashing
4. **Keep pins in config.h** - Easier for users to customize
5. **Document custom environments** - Add comments explaining what they're for

## Summary

- **platformio.ini** controls what compiles (features, libraries, optimization)
- **config.h** controls runtime settings (pins, timing, units)
- All standard builds include all features (modern boards have plenty of flash)
- Use serial commands to enable/disable outputs at runtime
- Only Arduino Uno needs special minimal build due to 32KB flash limit

For most users: **Just use teensy41 environment and configure sensors via serial commands.**
