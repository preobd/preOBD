# Directory Setup Guide - openEMS

## Complete Directory Structure

```
openEMS/
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project overview
│
├── src/                        # All source code goes here
│   ├── main.cpp               # Main program loop
│   ├── config.h               # ⚠️ USER CONFIGURATION FILE
│   ├── sensor_types.h         # Sensor data structures
│   ├── sensors.cpp            # Sensor definitions
│   ├── sensor_read.cpp        # Sensor reading functions
│   ├── alarm.cpp              # Alarm system
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
    ├── README.md              # Full documentation
    ├── MIGRATION_GUIDE.md     # Migration from old code
    ├── REFACTORING_SUMMARY.md # What changed and why
    └── QUICK_REFERENCE.md     # Quick reference card
```

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
   ├── sensor_types.h              ← Copy here
   ├── sensors.cpp                 ← Copy here
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
   # Copy platformio.ini to project root
   cp platformio.ini openEMS/
   ```

5. **Build:**
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
├── sensor_types.h
├── sensors.cpp
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

**If using flat structure, revert the include paths:**
- Change `#include "../config.h"` back to `#include "config.h"`
- Change `#include "outputs/output_base.h"` back to `#include "output_base.h"`

#### Option B: Use Subdirectories (Advanced)

Same structure as PlatformIO, but you need to:
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
#include "sensor_types.h"
#include "outputs/output_base.h"
```

**sensors.cpp** includes:
```cpp
#include "sensor_types.h"
#include "config.h"
```

**sensor_read.cpp** includes:
```cpp
#include "sensor_types.h"
#include "config.h"
```

**alarm.cpp** includes:
```cpp
#include "sensor_types.h"
#include "config.h"
```

### Files in src/outputs/

**output_base.h** includes:
```cpp
#include "../sensor_types.h"
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
- [ ] `output_base.h` is in `src/outputs/` directory
- [ ] `display_lcd.cpp` is in `src/displays/` directory
- [ ] Include paths use `../` to go up one directory
- [ ] Include paths use `outputs/` or `displays/` to go down
- [ ] platformio.ini is in project root (not in src/)

## Common Mistakes

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
#include "config.h"        // WRONG! Can't find it
```

### ✅ Correct: Using ../ to go up
```cpp
// In output_can.cpp
#include "../config.h"     // CORRECT! Goes up to src/
```

### ❌ Wrong: platformio.ini in src/
```
src/
├── platformio.ini         # WRONG!
└── main.cpp
```

### ✅ Correct: platformio.ini in root
```
openEMS/
├── platformio.ini         # CORRECT!
└── src/
    └── main.cpp
```

## Testing Your Setup

1. **Check file locations:**
   ```bash
   ls src/
   ls src/outputs/
   ls src/displays/
   ```

2. **Try to compile:**
   ```bash
   pio run
   ```

3. **Common errors and fixes:**

   **Error:** `fatal error: config.h: No such file or directory`
   - **Fix:** Check that config.h is in src/ directory
   - **Fix:** Check include path uses `../config.h` in subdirectories

   **Error:** `fatal error: output_base.h: No such file or directory`
   - **Fix:** Make sure output_base.h is in src/outputs/
   - **Fix:** Check main.cpp uses `#include "outputs/output_base.h"`

   **Error:** `multiple definition of 'initCAN'`
   - **Fix:** Don't compile the same .cpp file twice
   - **Fix:** Make sure you don't have duplicate files

## Migration from Flat Structure

If you have all files in one directory and want to organize:

1. **Create subdirectories:**
   ```bash
   mkdir src/outputs
   mkdir src/displays
   ```

2. **Move output files:**
   ```bash
   mv src/output_*.cpp src/outputs/
   mv src/output_base.h src/outputs/
   ```

3. **Move display files:**
   ```bash
   mv src/display_*.cpp src/displays/
   ```

4. **Update includes** using the files I provided above

5. **Test compile:**
   ```bash
   pio run
   ```

## Next Steps

Once your directory structure is set up:

1. **Edit config.h** to match your hardware
2. **Compile** to verify everything works
3. **Upload** to your microcontroller
4. **Test** each sensor individually
5. **Enable** additional features as needed

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
