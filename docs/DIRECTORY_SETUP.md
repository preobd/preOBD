# Directory Setup Guide - openEMS

## Complete Directory Structure

```
openEMS/
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project overview
│
├── src/                        # All source code goes here
│   ├── config.h               # ⚠️ USER CONFIGURATION FILE (at root for easy access!)
│   ├── advanced_config.h      # Advanced/power-user configuration
│   ├── main.cpp               # Main program loop
│   ├── alarm.cpp              # Alarm system
│   │
│   ├── lib/                   # Framework/library code (read-only reference)
│   │   ├── platform.h             # Platform auto-detection
│   │   ├── sensor_library.h       # Sensor catalog - 30+ sensors
│   │   ├── sensor_calibration_data.h  # Calibration database
│   │   ├── sensor_types.h         # Sensor data structures
│   │   └── application_presets.h  # Application presets (CHT, OIL_TEMP, etc.)
│   │
│   ├── inputs/                # Input system (sensor management)
│   │   ├── input.h                # Core input data structure
│   │   ├── input_manager.h/cpp    # Input configuration manager
│   │   ├── sensor_read.cpp        # Sensor reading functions
│   │   └── serial_config.h/cpp    # Serial configuration interface
│   │
│   ├── outputs/               # Output module directory
│   │   ├── output_base.h      # Output interface
│   │   ├── output_manager.cpp # Output manager
│   │   ├── output_can.cpp     # CAN bus output
│   │   ├── output_realdash.cpp # RealDash output
│   │   ├── output_serial.cpp  # Serial debug output
│   │   └── output_sdlog.cpp   # SD card logging
│   │
│   └── displays/              # Display module directory
│       └── display_lcd.cpp    # LCD display
│
└── docs/                       # Documentation
    ├── README.md                       # Full documentation
    ├── QUICK_REFERENCE.md              # Quick reference card
    ├── SENSOR_SELECTION_GUIDE.md       # How to pick sensors
    ├── PRESSURE_SENSOR_GUIDE.md        # Pressure sensor details
    ├── VOLTAGE_SENSOR_GUIDE.md         # Voltage monitoring
    ├── ADVANCED_CALIBRATION_GUIDE.md   # Custom sensors
    └── DIRECTORY_SETUP.md              # This file
```

## File Descriptions

### Root Directory

**platformio.ini**
- Build configuration for different hardware platforms
- Defines board types, build flags, dependencies
- **Edit:** Only to add new board types

### src/ - Application Files (Root Level)

**config.h** ⭐ **START HERE**
- User configuration file (at root for easy access!)
- Enable/disable sensors and outputs
- Pick sensor types from catalog
- Set pins and thresholds
- **Edit:** YES - This is where you configure everything!

**advanced_config.h**
- Advanced/power-user configuration
- Custom sensor calibrations (optional)
- **Edit:** Only if using custom sensors

**main.cpp**
- Main program loop
- Initializes all subsystems
- Calls sensor read → output → alarm → display
- **Edit:** Rarely (only for major architectural changes)

**alarm.cpp**
- Alarm system implementation
- Monitors sensor thresholds
- **Edit:** Only to modify alarm behavior

### src/lib/ - Framework/Library Code (Read-Only)

**platform.h**
- Automatically detects Arduino/Teensy/ESP32/Due
- Configures ADC resolution, reference voltage
- Sets platform-specific voltage dividers
- **Edit:** NO - Automatic detection

**sensor_library.h**
- Catalog of 30+ sensor IDs
- Browse this to find your sensor type
- **Edit:** NO - Reference only (add IDs for new sensors)

**sensor_calibration_data.h**
- Centralized calibration database
- Lookup tables for VDO sensors
- Steinhart-Hart coefficients
- Pressure sensor polynomials
- **Edit:** YES - To add new sensor calibrations

**sensor_types.h**
- Data structure definitions
- Calibration structs, enum definitions
- **Edit:** NO - Core architecture

**application_presets.h**
- Application type presets (CHT, OIL_TEMP, OIL_PRESSURE, etc.)
- Default units, min/max values, OBD-II PIDs
- **Edit:** NO - Reference only

### src/inputs/ - Input System

**input.h**
- Core input data structure
- Application and Sensor enums
- **Edit:** NO - Core architecture

**input_manager.h/cpp**
- Input configuration manager
- EEPROM persistence and runtime configuration
- **Edit:** RARELY - Only for advanced features

**sensor_read.cpp**
- All sensor reading implementations
- Thermistor, thermocouple, pressure, voltage functions
- Conversion functions (temperature, pressure, etc.)
- **Edit:** RARELY - Only to add new sensor types

**serial_config.h/cpp**
- Serial command interface for runtime configuration
- **Edit:** RARELY - Only to add new commands

### src/outputs/ - Output Modules

**output_base.h**
- Output module interface definition
- Defines OutputModule structure
- **Edit:** NO - Core interface

**output_manager.cpp**
- Manages all output modules
- Iterates through enabled outputs
- **Edit:** SOMETIMES - When adding new output types

**output_can.cpp**
- CAN bus OBDII output
- Standard diagnostic PIDs
- **Edit:** RARELY - To add custom PIDs

**output_realdash.cpp**
- RealDash mobile dashboard output
- Custom framing protocol
- **Edit:** RARELY - RealDash protocol changes

**output_serial.cpp**
- Serial debugging output
- CSV format for data logging
- **Edit:** RARELY - To change output format

**output_sdlog.cpp**
- SD card data logging
- CSV file creation and writing
- **Edit:** SOMETIMES - To customize logging format

### src/displays/ - Display Modules

**display_lcd.cpp**
- 20x4 character LCD display
- I2C interface
- **Edit:** SOMETIMES - To customize display layout

### docs/ - Documentation

See "Documentation Organization" section below.

## Step-by-Step Setup

### Method 1: PlatformIO (Recommended)

1. **Create project folder:**
   ```bash
   mkdir openEMS
   cd openEMS
   ```

2. **Create directory structure:**
   ```bash
   mkdir -p src/outputs
   mkdir -p src/displays
   mkdir -p docs
   ```

3. **Add files to their locations:**
   ```
   src/
   ├── main.cpp                    ← Copy here
   ├── config.h                    ← Copy here (EDIT THIS)
   ├── platform.h                  ← Copy here (NEW v2.0)
   ├── sensor_library.h            ← Copy here (NEW v2.0)
   ├── sensor_configs.h            ← Copy here (NEW v2.0)
   ├── sensor_types.h              ← Copy here
   ├── sensors.cpp                 ← Copy here
   ├── sensors.h                   ← Copy here
   ├── sensor_read.cpp             ← Copy here
   ├── alarm.cpp                   ← Copy here
   ├── outputs/
   │   ├── output_base.h           ← Copy here
   │   ├── output_manager.cpp      ← Copy here
   │   ├── output_can.cpp          ← Copy here
   │   ├── output_realdash.cpp     ← Copy here
   │   ├── output_serial.cpp       ← Copy here
   │   └── output_sdlog.cpp        ← Copy here
   └── displays/
       └── display_lcd.cpp         ← Copy here
   ```

4. **Add platformio.ini to root:**
   ```bash
   cp platformio.ini openEMS/
   ```

5. **Configure your sensors in config.h:**
   - Open `sensor_library.h` to browse available sensors
   - Edit `config.h` and pick sensor IDs

6. **Build:**
   ```bash
   pio run
   ```

### Method 2: Arduino IDE

**Important:** Arduino IDE doesn't handle subdirectories well. You have two options:

#### Option A: Flatten Structure (Easier)

Create folder and put all files in it:
```
openEMS/
├── openEMS.ino                 # Rename main.cpp to this
├── config.h
├── platform.h
├── sensor_library.h
├── sensor_configs.h
├── sensor_types.h
├── sensors.cpp
├── sensors.h
├── sensor_read.cpp
├── alarm.cpp
├── output_base.h
├── output_manager.cpp
├── output_can.cpp
├── output_realdash.cpp
├── output_serial.cpp
├── output_sdlog.cpp
└── display_lcd.cpp
```

**If using flat structure, update include paths:**
- Change `#include "../config.h"` to `#include "config.h"`
- Change `#include "outputs/output_base.h"` to `#include "output_base.h"`

#### Option B: Use Subdirectories (Advanced)

Same structure as PlatformIO, but you need:
1. Use the updated files with `../` includes
2. Arduino IDE 1.6.6+ should handle it, but may have issues

### Method 3: Manual Build

If you're using a custom build system:

1. **Create structure** as shown above
2. **Compiler flags** to add include paths:
   ```
   -I./src
   -I./src/outputs
   -I./src/displays
   ```
3. **Compile** all .cpp files
4. **Link** together

## File Include Relationships

### Files in src/

**main.cpp** includes:
```cpp
#include "config.h"
#include "platform.h"
#include "sensor_types.h"
#include "sensors.h"
#include "outputs/output_base.h"
```

**config.h** includes:
```cpp
#include "sensor_library.h"  // For sensor IDs
```

**sensors.cpp** includes:
```cpp
#include "sensor_types.h"
#include "sensors.h"
#include "sensor_configs.h"  // For getSensorConfig()
#include "config.h"
```

**sensor_read.cpp** includes:
```cpp
#include "sensor_types.h"
#include "config.h"
#include "platform.h"  // For ADC constants
#include <SPI.h>
```

**alarm.cpp** includes:
```cpp
#include "sensor_types.h"
#include "config.h"
```

### Files in src/outputs/

**output_base.h** includes:
```cpp
#include "sensor_types.h"  // Uses ../sensor_types.h
```

**output_manager.cpp** includes:
```cpp
#include "output_base.h"
#include "../config.h"
```

**All output_*.cpp** files include:
```cpp
#include "output_base.h"
#include "../config.h"
```

### Files in src/displays/

**display_lcd.cpp** includes:
```cpp
#include "../sensor_types.h"
#include "../config.h"
```

## Verification Checklist

After setting up, verify:

- [ ] All files are in correct locations
- [ ] `config.h` is in `src/` directory
- [ ] `platform.h` is in `src/` directory
- [ ] `sensor_library.h` is in `src/` directory
- [ ] `sensor_configs.h` is in `src/` directory
- [ ] `output_base.h` is in `src/outputs/` directory
- [ ] `display_lcd.cpp` is in `src/displays/` directory
- [ ] Include paths use `../` to go up one directory
- [ ] Include paths use `outputs/` or `displays/` to go down
- [ ] platformio.ini is in project root (not in src/)

## Common Mistakes

### ❌ Wrong: Missing sensor library files
```
src/
├── main.cpp
└── config.h          # Missing platform.h, sensor_library.h, etc!
```

### ✅ Correct: All files present
```
src/
├── main.cpp
├── config.h
├── platform.h        # NEW
├── sensor_library.h  # NEW
└── sensor_configs.h  # NEW
```

### ❌ Wrong: output_base.h in src/
```
src/
├── output_base.h          # WRONG!
└── outputs/
    └── output_can.cpp
```

### ✅ Correct: output_base.h in src/outputs/
```
src/
└── outputs/
    ├── output_base.h      # CORRECT!
    └── output_can.cpp
```

### ❌ Wrong: Missing ../ in includes
```cpp
// In output_can.cpp
#include "config.h"        # WRONG! Can't find it
```

### ✅ Correct: Using ../ to go up
```cpp
// In output_can.cpp
#include "../config.h"     # CORRECT! Goes up to src/
```

## Testing Your Setup

1. **Check file locations:**
   ```bash
   ls src/
   ls src/outputs/
   ls src/displays/
   ```

   Should see all files including `platform.h`, `sensor_library.h`, `sensor_configs.h`

2. **Try to compile:**
   ```bash
   pio run
   ```

3. **Common errors and fixes:**

   **Error:** `fatal error: platform.h: No such file or directory`
   - **Fix:** Make sure platform.h is in src/ directory

   **Error:** `fatal error: sensor_library.h: No such file or directory`
   - **Fix:** Make sure sensor_library.h is in src/ directory

   **Error:** `'getSensorConfig' was not declared in this scope`
   - **Fix:** Make sure sensor_configs.h is included in sensors.cpp

   **Error:** `fatal error: config.h: No such file or directory`
   - **Fix:** Check that config.h is in src/ directory
   - **Fix:** Check include path uses `../config.h` in subdirectories

## Migration from v1.0

If you have v1.0 and want to upgrade:

1. **Backup your old config.h:**
   ```bash
   cp src/config.h src/config.h.v1.0.backup
   ```

2. **Add new v2.0 files:**
   - Copy `platform.h` to src/
   - Copy `sensor_library.h` to src/
   - Copy `sensor_configs.h` to src/

3. **Update config.h:**
   - Add sensor type definitions
   - See MIGRATION_GUIDE.md for details

4. **Update sensors.cpp:**
   - Add `getSensorConfig()` calls
   - See MIGRATION_GUIDE.md for examples

5. **Test compile:**
   ```bash
   pio run
   ```

See [MIGRATION_GUIDE.md](MIGRATION_GUIDE.md) for complete step-by-step instructions.

## Documentation Organization

### docs/README.md
Comprehensive documentation covering:
- Complete feature list
- Detailed hardware setup
- Wiring diagrams
- Troubleshooting
- Adding new sensors/outputs

### docs/QUICK_REFERENCE.md
Quick lookup for:
- Common tasks
- Sensor catalog
- Pin assignments
- Troubleshooting checklist

### docs/SENSOR_SELECTION_GUIDE.md
How to pick the right sensor:
- Sensor catalog with examples
- Lookup vs Steinhart comparison
- Complete configuration examples

### docs/MIGRATION_GUIDE.md
Upgrading from older versions:
- What changed and why
- Step-by-step migration
- Sensor conversion table
- Testing and verification

### docs/PRESSURE_SENSOR_GUIDE.md
Everything about pressure sensors:
- VDO vs generic sensors
- Wiring and calibration
- Troubleshooting pressure readings

### docs/VOLTAGE_SENSOR_GUIDE.md
Battery and voltage monitoring:
- Platform auto-configuration
- Voltage divider setup
- Calibration procedures

### docs/ADVANCED_CALIBRATION_GUIDE.md
For advanced users:
- Custom sensor calibrations
- Adding to sensor library
- Steinhart-Hart coefficients
- Lookup table creation

### docs/DIRECTORY_SETUP.md
This file - explains project structure.

## Next Steps

Once your directory structure is set up:

1. **Browse sensor catalog:**
   - Open `src/lib/sensor_library.h`
   - Find sensor IDs for your hardware

2. **Edit config.h:**
   - Pick sensor types from catalog
   - Set pin assignments
   - Configure thresholds

3. **Compile:**
   ```bash
   pio run
   ```

4. **Upload:**
   ```bash
   pio run -t upload
   ```

5. **Test:**
   - Monitor serial output
   - Verify sensor readings
   - Check platform detection

## Quick Commands Reference

```bash
# Create structure
mkdir -p src/outputs src/displays docs

# Compile
pio run

# Upload
pio run -t upload

# Monitor serial output
pio device monitor

# Clean build
pio run -t clean

# Build for specific board
pio run -e teensy40
pio run -e megaatmega2560
```

## Getting Help

**Setup issues:**
1. Check this file for correct structure
2. Verify all files are in correct locations
3. Check include paths with `../` notation

**Configuration issues:**
1. See SENSOR_SELECTION_GUIDE.md
2. See QUICK_REFERENCE.md
3. Ask in GitHub Discussions

**Migration issues:**
1. See MIGRATION_GUIDE.md
2. Post before/after config in Discussions

---

**Organized structure makes adding sensors and troubleshooting much easier!**
