# Build Configuration Guide

This guide explains how preOBD separates compile-time configuration from runtime configuration, and how to customize builds for your hardware.

## Quick Reference

**Three layers, clear ownership:**
- **`src/profiles/profile_*.h`** = Board definition: which features are enabled AND which pins are used
- **`src/config.h`** = Application constants: timing, thresholds, calibration defaults
- **`platformio.ini`** = Build environments: which profile to use, compiler flags, libraries
- **EEPROM** = Runtime user config: sensor inputs, output settings, unit preferences

## Table of Contents

1. [Understanding the Configuration Layers](#understanding-the-configuration-layers)
2. [Board Profiles](#board-profiles)
3. [Application Constants](#application-constants)
4. [Output Module Availability](#output-module-availability-compile-time-vs-runtime)
5. [Choosing a Build Environment](#choosing-a-build-environment)
6. [Creating Custom Environments](#creating-custom-environments)
7. [Library Dependencies](#library-dependencies)
8. [Memory Optimization](#memory-optimization)
9. [Troubleshooting](#troubleshooting)

## Understanding the Configuration Layers

### Layer 1 — Board Profile (`src/profiles/profile_*.h`)

The profile is the authoritative definition of a specific hardware board. It is included at the top of every translation unit via `-include` in `platformio.ini`, so its definitions take effect before any source file is compiled.

**What goes in the profile:**
- Feature flags (`ENABLE_CAN 1`, `ENABLE_SD_LOGGING 1`, etc.)
- Hardware pin assignments (`MODE_BUTTON_PIN`, `ALARMS_PIN`, `SD_PIN`, `RGB_PIN_R/G/B`, SPI CAN pins)
- Board-specific sizing (`MAX_INPUTS`, `MAX_PID_ENTRIES`, `CLI_BUFFER_SIZE`)
- CAN controller type (`PROFILE_HAS_NATIVE_CAN`, `CAN_BUS_N_TYPE`)

**Pin convention in profiles:**

| Value | Meaning |
|---|---|
| `ENABLE_X` not defined | Feature disabled |
| `#define ENABLE_X 1` | Feature enabled (no hardware pin) |
| `#define X_PIN N` | Feature uses physical pin N |
| `X_PIN` not defined | Feature enabled but pin not wired on this board |

**Example:**
```cpp
// profile_teensy41.h
#define ENABLE_ALARMS       1
#define ALARMS_PIN          3    // buzzer wired on pin 3

#define SUPPORTS_SD         1
#define SD_PIN              254  // Teensy 4.1 built-in SD
#define ENABLE_SD_LOGGING   1

#define ENABLE_MODE_BUTTON  1
#define MODE_BUTTON_PIN     5
```

```cpp
// profile_teensy40.h — no button, no buzzer wired on this board
#define ENABLE_ALARMS       1    // software alarms still active (CAN/LCD)
// ALARMS_PIN not defined — no buzzer
// ENABLE_MODE_BUTTON not defined — no button
```

**SD hardware capability vs SD logging:**

`SUPPORTS_SD` and `ENABLE_SD_LOGGING` are independent. `SUPPORTS_SD` declares that the board physically has an SD slot and gates the SD driver itself; `ENABLE_SD_LOGGING` gates the logging feature that writes sensor CSV to that SD. JSON config file save/load (`SAVE FILE`, `LOAD FILE`) is gated on `SUPPORTS_SD` only — no logging feature required. A board with `SUPPORTS_SD` but no `ENABLE_SD_LOGGING` can still save and load JSON config files via SD.

### Layer 2 — Application Constants (`src/config.h`)

Contains values that are logic-level, not hardware-level: things that describe *how* the firmware behaves, not *what is wired where*.

**What goes in config.h:**
- Timing intervals (`SENSOR_READ_INTERVAL_MS`, `LCD_UPDATE_INTERVAL_MS`)
- Alarm behavior (`SILENCE_DURATION`, `WARNING_THRESHOLD_PERCENT`)
- Calibration defaults (`DEFAULT_BIAS_RESISTOR`, `SEA_LEVEL_PRESSURE_HPA`)
- Default display units (`DEFAULT_TEMPERATURE_UNITS`, etc.)
- Protocol constants (OBD-II rate limiting)

**What does NOT go in config.h:** hardware pin assignments. Those belong exclusively in board profiles.

### Layer 3 — Build Environments (`platformio.ini`)

Environments specify the build target. They `-include` the correct board profile, set platform flags, and list library dependencies. They do not contain feature flags or pin assignments — those live in the profile.

```ini
[env:teensy41]
platform = teensy
board = teensy41
build_flags =
    -include src/profiles/profile_teensy41.h  ; includes all feature flags and pins
    -D TEENSY_41
    -D USE_FLEXCAN_NATIVE
    -O2
    -Wall
lib_deps = ...
```

### Layer 4 — EEPROM (Runtime)

User-configurable state that survives reboots without reflashing: sensor inputs, output enable/disable, timing overrides, unit preferences. Managed via serial commands and persisted to EEPROM.

```
SET OUTPUT CAN ENABLED       # Enable CAN output
SET OUTPUT CAN INTERVAL 100  # Change update rate
SAVE                         # Persist to EEPROM
```

---

## Board Profiles

### Profiles Shipped with preOBD

| Profile | Platform | SD | Notes |
|---|---|---|---|
| `profile_teensy41.h` | Teensy 4.1 | Built-in (pin 254) | Full-featured reference profile |
| `profile_teensy40.h` | Teensy 4.0 | None (optional pin 4) | No SD in default profile; add `SUPPORTS_SD`+`SD_PIN` to enable |
| `profile_teensy36.h` | Teensy 3.6 | Built-in (pin 254) | Older platform |
| `profile_esp32s3.h` | ESP32-S3 | External (pin 4) | Native TWAI CAN |
| `profile_mega2560.h` | Arduino Mega 2560 | Disabled | SPI CAN (MCP2515), RAM-constrained |
| `profile_teensy41_hybrid.h` | Teensy 4.1 | Built-in (pin 254) | 3× FlexCAN + 1× MCP2515 |
| `profile_esp32s3_hybrid.h` | ESP32-S3 | External (pin 4) | TWAI + MCP2515 |

### Customizing a Profile

To adapt a profile to your wiring, edit the `HARDWARE PIN ASSIGNMENTS` section at the top of the profile file:

```cpp
// Change the buzzer pin
#define ALARMS_PIN     6    // was 3

// Disable mode button (no button wired)
// #define ENABLE_MODE_BUTTON  1
// #define MODE_BUTTON_PIN     5

// Use external SD on a custom CS pin
#define SD_PIN 10   // was 4
```

### Disabling a Feature in a Profile

Comment out the `ENABLE_X` define:

```cpp
//#define ENABLE_SD_LOGGING    // no SD hardware on this board
//#define ENABLE_TEST_MODE     // not needed
```

If the feature has a dedicated pin, the pin definition can be omitted — the compiler will enforce this with a `#error` if a pin is required but missing.

---

## Application Constants

`src/config.h` controls behavior that is the same regardless of which board you're using:

```cpp
#define SENSOR_READ_INTERVAL_MS  50     // 20Hz sensor reads
#define ALARM_CHECK_INTERVAL_MS  50     // 20Hz alarm checks
#define SILENCE_DURATION         30000  // 30s alarm silence
#define DEFAULT_TEMPERATURE_UNITS "CELSIUS"
```

These rarely need changing. If you do change them, they affect all board profiles equally.

---

## Output Module Availability: Compile-time vs Runtime

### Compile-time Control (Board Profile)

Feature flags in the profile control whether output code is included at all:

- `ENABLE_CAN 1` in profile → CAN implementation compiles in, CAN library linked
- `ENABLE_CAN` not defined → CAN code excluded → ~20KB flash saved

**Check what's compiled in**: Use `LIST OUTPUTS` serial command.

### Runtime Control (EEPROM)

Once compiled in, outputs can be enabled/disabled without reflashing:

```
SET OUTPUT CAN ENABLED       # Enable CAN output
SET OUTPUT CAN DISABLED      # Disable CAN output
SET OUTPUT CAN INTERVAL 100  # Change update rate
SAVE                         # Persist to EEPROM
```

### Why Two Tiers?

- **Flash optimization**: Don't compile unused features
- **Runtime flexibility**: Toggle features without reflashing
- **Default builds**: Profiles include all features appropriate for a platform; users disable at runtime what they don't need

---

## Choosing a Build Environment

### Pre-configured Environments

| Environment | Platform | Flash | Notes |
|---|---|---|---|
| **teensy41** | Teensy 4.1 | 8MB | **Recommended** — full-featured, built-in SD |
| teensy40 | Teensy 4.0 | 2MB | No built-in SD; no button/buzzer in default profile |
| teensy36 | Teensy 3.6 | 1MB | Older platform |
| mega2560 | Arduino Mega | 256KB | Good for prototyping; RAM-constrained |
| debug | Teensy 4.1 | 8MB | Debug symbols, `-Og` optimization |
| teensy41\_hybrid | Teensy 4.1 | 8MB | 3× FlexCAN + MCP2515 |
| esp32s3dev | ESP32-S3 | 3.3MB | Native TWAI CAN |

### Build Commands

```bash
pio run -e teensy41
pio run -e teensy40
pio run -e mega2560
pio run -e debug
```

### Flash Usage Estimates

| Platform | Full Build | % Used | Remaining |
|---|---|---|---|
| Teensy 4.1 | ~340KB | 4.2% | 7.7MB |
| Teensy 4.0 | ~324KB | 15.8% | 1.7MB |
| Teensy 3.6 | ~487KB | 46.4% | 561KB |
| Mega 2560 | ~172KB | 67.6% | 82KB |

---

## Creating Custom Environments

### Adding a New Board Variant

1. Copy the closest existing profile to a new file:

```bash
cp src/profiles/profile_teensy41.h src/profiles/profile_my_board.h
```

2. Edit pin assignments and feature flags in the new profile.

3. Add an environment in `platformio.ini`:

```ini
[env:my_board]
platform = teensy
board = teensy41
build_flags =
    -include src/profiles/profile_my_board.h
    -D TEENSY_41
    -D USE_FLEXCAN_NATIVE
    -O2
    -Wall
lib_deps =
    ${display_libs.lib_deps}
    ${sensor_libs.lib_deps}
    ${eeprom_libs.lib_deps}
    ${cli_libs.lib_deps}
    https://github.com/tonton81/FlexCAN_T4.git
    https://github.com/tonton81/WDT_T4.git
```

### Hybrid CAN Mode

Hybrid mode mixes native and external CAN controllers. The bus type assignments live in the profile:

```cpp
// profile_teensy41_hybrid.h
#define ENABLE_CAN_HYBRID      1
#define CAN_BUS_0_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_3_TYPE CanControllerType::MCP2515

// SPI pins for the MCP2515 bus
#define CAN_CS_0               9
#define CAN_INT_0              2
```

**Pre-configured hybrid environments:**
```bash
pio run -e esp32s3_hybrid    # ESP32-S3: TWAI + MCP2515
pio run -e teensy41_hybrid   # Teensy 4.1: 3× FlexCAN + MCP2515
```

---

## Library Dependencies

preOBD uses modular library groups in `platformio.ini`:

```ini
[display_libs]   lib_deps = marcoschwartz/LiquidCrystal_I2C
[can_libs]       lib_deps = autowp/autowp-mcp2515@^1.0.3
[sd_libs]        lib_deps = greiman/SdFat
[sensor_libs]    lib_deps = adafruit/Adafruit BME280 Library
[eeprom_libs]    lib_deps = bblanchon/ArduinoJson
```

### Platform-Specific CAN Libraries

| Platform | CAN | Library | Notes |
|---|---|---|---|
| Teensy 4.x/3.x | Native FlexCAN | FlexCAN_T4 | Built-in, supports 2–3 buses |
| ESP32 | Native TWAI | ESP32-TWAI-CAN | External transceiver required |
| Mega/Uno | External MCP2515 | autowp-mcp2515 | SPI, pins set in profile |

---

## Memory Optimization

### Flash Savings by Feature

| Feature Disabled (in profile) | Flash Saved |
|---|---|
| ArduinoJson / SD Logging | 15–20KB |
| CAN Output | ~15KB |
| RealDash | ~8KB |
| Test Mode | ~4KB |

To disable a feature, comment out its `ENABLE_X` line in the board profile.

---

## Troubleshooting

### Problem: "undefined reference to..." errors

**Cause**: Library not in `lib_deps`.

**Fix**: Add the appropriate library group to the environment.

### Problem: Feature not available (`LIST OUTPUTS` doesn't show it)

**Cause**: `ENABLE_X` not defined in the board profile.

**Fix**: Uncomment or add `#define ENABLE_X 1` in the profile. Rebuild and flash.

### Problem: `#error "X_PIN must be defined..."`

**Cause**: A feature is enabled in the profile but its pin macro is missing.

**Fix**: Add the pin definition to the profile:
```cpp
#define ENABLE_TEST_MODE  1
#define TEST_MODE_PIN     8   // ← add this
```

### Problem: Wrong pins for my board

**Cause**: Pin assignments in the profile don't match your wiring.

**Fix**: Edit the `HARDWARE PIN ASSIGNMENTS` section of your board's profile file. No other files need changing.

---

## Best Practices

1. **Start with a standard profile** — copy the closest match, edit pin assignments
2. **All hardware config in the profile** — don't scatter pin assignments across multiple files
3. **Use runtime configuration for toggles** — use serial commands to enable/disable outputs, not reflashing
4. **Document your profile** — add comments explaining what is wired and where
5. **One profile per hardware variant** — if two boards differ in wiring, they get separate profiles

## Summary

- **Board profile** = what features exist AND where hardware is wired (one file per board)
- **config.h** = application behavior constants (timing, thresholds)
- **platformio.ini** = build environments (which profile, which libraries)
- **EEPROM** = runtime user settings (sensors, outputs, units)

For most users: **pick the right profile for your board, edit the pin assignments, build and flash.**
