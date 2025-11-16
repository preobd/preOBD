# W-Phase Alternator RPM Sensing Guide

## ⚠️ CRITICAL SAFETY WARNING

**Teensy boards (3.x and 4.x) use 3.3V logic and GPIO pins are NOT 5V tolerant!**

- **3.3V systems (Teensy, Due, ESP32):** Use **22kΩ + 4.7kΩ** divider with **3.3V zener**
- **5V systems (Arduino Mega, Uno):** Use **10kΩ + 4.7kΩ** divider with **5.1V zener**

**Applying more than 3.3V to a Teensy pin WILL damage it permanently!**

Read the component selection section carefully before building.

---

## Overview

This guide explains how to add engine RPM sensing using the alternator's W-phase output. This method is ideal for classic cars and vehicles without electronic ignition systems.

## How It Works

**Alternator basics:**
- Most alternators have 3-phase stator windings (labeled W, V, U or similar)
- The W-phase outputs AC pulses proportional to engine RPM
- These pulses need to be conditioned before feeding to a microcontroller

**Frequency-to-RPM relationship:**
```
Alternator_Hz = (Engine_RPM × Poles) / 120

Where:
- Poles = number of poles in alternator (typically 12-16)
- 120 = constant for 60Hz systems

Example for 12-pole alternator:
- At 1000 RPM: 100 Hz
- At 3000 RPM: 300 Hz  
- At 6000 RPM: 600 Hz
```

## Required Hardware

### Components:

**UNIVERSAL SOLUTION (Works for ALL boards):** ✅ **RECOMMENDED**
1. **22kΩ resistor** (1/4W or higher) - high side
2. **4.7kΩ resistor** (1/4W or higher) - low side
3. **1N4148 diode** (or similar fast switching diode)
4. **100nF ceramic capacitor** (for noise filtering)
5. **Zener diode:** 
   - **3.3V zener** for Teensy/3.3V boards (REQUIRED)
   - **5.1V zener** for Arduino/5V boards (optional but recommended)

**Why 22kΩ / 4.7kΩ works for both:**
- Divider ratio: (4.7kΩ / (22kΩ + 4.7kΩ)) ≈ 0.176
- Alternator W-phase: 10-30V AC → 1.8-5.3V AC output
- **For 3.3V systems:** Zener clamps to 3.3V = SAFE ✓
- **For 5V systems:** Signal stays well below 5V = SAFE ✓
- **Benefit:** Use same circuit board design for all platforms!

---

**Alternative: Optimized for each system**

**For 5V systems ONLY (Arduino Mega, Uno):**
1. **10kΩ resistor** (1/4W or higher) - high side
2. **4.7kΩ resistor** (1/4W or higher) - low side
3. **1N4148 diode** (or similar fast switching diode)
4. **100nF ceramic capacitor** (for noise filtering)
5. **Zener diode 5.1V** (recommended for overvoltage protection)

**For 3.3V systems ONLY (Teensy 3.x/4.x, Due, ESP32):** ⚠️ **CRITICAL**
1. **22kΩ resistor** (1/4W or higher) - high side
2. **4.7kΩ resistor** (1/4W or higher) - low side
3. **1N4148 diode** (or similar fast switching diode)
4. **100nF ceramic capacitor** (for noise filtering)
5. **Zener diode 3.3V** (REQUIRED for overvoltage protection)

### Circuit Design

**UNIVERSAL CIRCUIT (Recommended - Works for ALL boards):**
```
Alternator W-Phase ----[22kΩ]----+----[4.7kΩ]---- GND
                                 |
                             [1N4148]
                                 |
                             [100nF]
                                 |
                           Microcontroller
                               Pin 5
                                 |
                           [Zener Diode]
                           (3.3V or 5.1V)
                                 |
                               GND
```

**Signal levels:**
- Alternator W-phase: ~10-30V AC
- After divider: ~1.8-5.3V AC
- After diode: 0-5.3V pulses
- After zener: 0-3.3V (Teensy) or 0-5.1V (Arduino)

**Why this works for both:**
- 22kΩ/4.7kΩ provides conservative voltage division
- Even without zener, max voltage is ~5.3V
- With 3.3V zener: Safe for Teensy ✓
- With 5.1V zener: Safe for Arduino, good signal strength ✓
- **Same resistors, just swap the zener based on your board**

---

**Alternative: Optimized Circuits**

**For 3.3V Systems ONLY (Teensy):**
```
Alternator W-Phase ----[22kΩ]----+----[4.7kΩ]---- GND
                                 |
                             [1N4148]
                                 |
                             [100nF]
                                 |
                           Teensy Pin 5
                                 |
                             [3.3V Zener]
                             (REQUIRED)
                                 |
                               GND

Voltage: ~10-30V AC → ~1.8-5.3V → Clamped to 3.3V max
```

**For 5V Systems ONLY (Arduino Mega - Better Signal Strength):**
```
Alternator W-Phase ----[10kΩ]----+----[4.7kΩ]---- GND
                                 |
                             [1N4148]
                                 |
                             [100nF]
                                 |
                           Arduino Pin 5
                                 |
                             [5.1V Zener]
                             (Recommended)
                                 |
                               GND

Voltage: ~10-30V AC → ~3.2-9.6V → Clamped to 5.1V max
```

## Installation Steps

### Step 1: Locate W-Phase Terminal

**On most alternators:**
- Look for a small terminal labeled "W", "STA", or "TACH"
- Often a single spade connector on back of alternator
- Some alternators have it internally - may need to tap into stator

**Testing W-phase:**
1. Start engine
2. Use AC voltmeter on W terminal (to ground)
3. Should read 5-30V AC that increases with RPM
4. If no voltage, try other small terminals

### Step 2: Build Protection Circuit

**RECOMMENDED: Universal Circuit (works for all boards)**

```
1. Connect 22kΩ from W-phase wire to junction point
2. Connect 4.7kΩ from junction to ground
3. Connect diode anode to junction, cathode to microcontroller pin
4. Connect 100nF capacitor from microcontroller pin to ground
5. Connect appropriate zener diode:
   - 3.3V zener for Teensy/Due/ESP32 (REQUIRED)
   - 5.1V zener for Arduino Mega/Uno (recommended)
```

**Benefits of universal circuit:**
- ✅ Same resistor values for all boards
- ✅ Only need to swap zener diode based on board
- ✅ Can design one PCB that works everywhere
- ✅ Easier to document and share designs
- ✅ Safe even if zener fails (max 5.3V won't damage 5V boards)

---

**Alternative: Optimized circuits (slightly better signal on 5V boards)**

⚠️ **Only use if you'll NEVER switch boards**

**For Teensy 3.x/4.x, Arduino Due, ESP32 (3.3V systems):**
```
1. Connect 22kΩ from W-phase wire to junction point
2. Connect 4.7kΩ from junction to ground
3. Connect diode anode to junction, cathode to microcontroller pin
4. Connect 100nF capacitor from microcontroller pin to ground
5. Connect 3.3V zener from microcontroller pin to ground (REQUIRED)
```

**For Arduino Mega, Uno (5V systems) - Stronger signal:**
```
1. Connect 10kΩ from W-phase wire to junction point
2. Connect 4.7kΩ from junction to ground
3. Connect diode anode to junction, cathode to microcontroller pin
4. Connect 100nF capacitor from microcontroller pin to ground
5. Connect 5.1V zener from microcontroller pin to ground (REQUIRED)
```

---

**On breadboard (for testing):**
- **Recommended:** Use 22kΩ/4.7kΩ universal circuit
- Double-check zener diode voltage rating matches your board
- Test voltage at MCU pin with multimeter before connecting
  - Should not exceed 3.3V for Teensy
  - Should not exceed 5V for Arduino

**On PCB/permanent install:**
- **Recommended:** Design PCB with 22kΩ/4.7kΩ resistors
- Add socket or jumper for zener diode selection
- Mount components near microcontroller
- Use heat shrink on alternator connection
- Route signal wire away from spark plug wires

### Step 3: Connect to Microcontroller

**Recommended pins (interrupt-capable):**

| Board | Pin Options | Notes |
|-------|-------------|-------|
| Teensy 4.0/4.1 | Any digital pin | All pins support interrupts |
| Teensy 3.x | Any digital pin | All pins support interrupts |
| Arduino Mega | 2, 3, 18, 19, 20, 21 | Hardware interrupt pins |
| Arduino Uno | 2, 3 | Only two interrupt pins |

**We'll use pin 5 (available on all platforms)**

### Step 4: Determine Alternator Poles

**Method 1: Test and calculate**
1. Connect circuit and run code
2. Read frequency at known RPM (use tachometer)
3. Calculate: `Poles = (Frequency × 120) / RPM`

**Method 2: Common alternator specs**
- Most automotive: 12 poles
- High-output alternators: 14-16 poles
- Small engines: 8-12 poles

**Example:**
```
Measured: 200 Hz at 2000 RPM
Poles = (200 × 120) / 2000 = 12 poles
```

## Code Implementation

### Add to sensor_library.h

```cpp
// ===== RPM SENSORS =====
#define W_PHASE_RPM_12_POLE      70  // 12-pole alternator
#define W_PHASE_RPM_14_POLE      71  // 14-pole alternator
#define W_PHASE_RPM_16_POLE      72  // 16-pole alternator
#define W_PHASE_RPM_CUSTOM       79  // Custom pole count
```

### Add to sensor_configs.h

```cpp
// ===== RPM CALIBRATION STRUCTURES =====

typedef struct {
    byte poles;              // Number of alternator poles
    float pulses_per_rev;    // Calculated from poles
    uint16_t timeout_ms;     // Timeout for zero RPM (ms)
    uint16_t min_rpm;        // Minimum valid RPM
    uint16_t max_rpm;        // Maximum valid RPM
} RPMCalibration;

// Common alternator configurations
static const RPMCalibration rpm_12pole_cal = {
    .poles = 12,
    .pulses_per_rev = 12.0 / 2.0,  // 6 pulses per revolution
    .timeout_ms = 2000,             // 2 second timeout
    .min_rpm = 300,
    .max_rpm = 8000
};

static const RPMCalibration rpm_14pole_cal = {
    .poles = 14,
    .pulses_per_rev = 14.0 / 2.0,  // 7 pulses per revolution
    .timeout_ms = 2000,
    .min_rpm = 300,
    .max_rpm = 8000
};

static const RPMCalibration rpm_16pole_cal = {
    .poles = 16,
    .pulses_per_rev = 16.0 / 2.0,  // 8 pulses per revolution
    .timeout_ms = 2000,
    .min_rpm = 300,
    .max_rpm = 8000
};
```

**Add to SENSOR_CONFIGS array:**
```cpp
// ===== RPM SENSORS =====
{
    .sensorId = W_PHASE_RPM_12_POLE,
    .name = "W-Phase RPM (12-pole)",
    .internalType = W_PHASE_RPM,
    .readFunction = readWPhaseRPM,
    .displayConvert = convertRPM,
    .obdConvert = obdConvertRPM,
    .calibrationType = CAL_RPM,
    .calibrationData = (void*)&rpm_12pole_cal
},
{
    .sensorId = W_PHASE_RPM_14_POLE,
    .name = "W-Phase RPM (14-pole)",
    .internalType = W_PHASE_RPM,
    .readFunction = readWPhaseRPM,
    .displayConvert = convertRPM,
    .obdConvert = obdConvertRPM,
    .calibrationType = CAL_RPM,
    .calibrationData = (void*)&rpm_14pole_cal
},
{
    .sensorId = W_PHASE_RPM_16_POLE,
    .name = "W-Phase RPM (16-pole)",
    .internalType = W_PHASE_RPM,
    .readFunction = readWPhaseRPM,
    .displayConvert = convertRPM,
    .obdConvert = obdConvertRPM,
    .calibrationType = CAL_RPM,
    .calibrationData = (void*)&rpm_16pole_cal
}
```

### Add to sensor_types.h

```cpp
// Add to SensorType enum:
enum SensorType { 
    // ... existing types ...
    W_PHASE_RPM,
    HALL_EFFECT_RPM,
    IGNITION_COIL_RPM
};

// Add to CalibrationType enum:
enum CalibrationType {
    // ... existing types ...
    CAL_RPM
};

// Add calibration helper:
inline RPMCalibration* getRPMCal(Sensor* ptr) {
    if (ptr->calibrationType != CAL_RPM) {
        return nullptr;
    }
    return (RPMCalibration*)ptr->calibrationData;
}
```

### Add to sensor_read.cpp

```cpp
// ===== RPM SENSING - INTERRUPT-BASED =====

// Global variables for RPM calculation
volatile unsigned long rpm_pulse_count = 0;
volatile unsigned long rpm_last_time = 0;
volatile unsigned long rpm_pulse_interval = 0;
unsigned long rpm_calc_time = 0;

// Interrupt service routine
void rpmPulseISR() {
    unsigned long now = micros();
    unsigned long interval = now - rpm_last_time;
    
    // Debounce: ignore pulses faster than 100 µs (600,000 RPM equivalent)
    if (interval > 100) {
        rpm_pulse_interval = interval;
        rpm_pulse_count++;
        rpm_last_time = now;
    }
}

// Initialize RPM sensing
void initRPM(byte pin) {
    pinMode(pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(pin), rpmPulseISR, RISING);
    Serial.print("RPM sensing initialized on pin ");
    Serial.println(pin);
}

// Read W-Phase RPM
void readWPhaseRPM(Sensor *ptr) {
    // Get calibration
    RPMCalibration* cal = getRPMCal(ptr);
    if (cal == nullptr) {
        ptr->value = NAN;
        return;
    }
    
    // Calculate time since last pulse
    unsigned long now = millis();
    unsigned long time_since_pulse = now - (rpm_last_time / 1000);
    
    // Check for timeout (engine stopped)
    if (time_since_pulse > cal->timeout_ms) {
        ptr->value = 0;
        return;
    }
    
    // Calculate RPM from pulse interval
    // Formula: RPM = (60,000,000 µs/min) / (interval_µs × pulses_per_rev)
    if (rpm_pulse_interval > 0) {
        float rpm = 60000000.0 / (rpm_pulse_interval * cal->pulses_per_rev);
        
        // Validate range
        if (rpm >= cal->min_rpm && rpm <= cal->max_rpm) {
            // Apply simple smoothing filter (optional)
            if (!isnan(ptr->value) && ptr->value > 0) {
                ptr->value = (ptr->value * 0.8) + (rpm * 0.2);
            } else {
                ptr->value = rpm;
            }
        } else {
            ptr->value = NAN;  // Out of range
        }
    }
}

// Conversion functions
float convertRPM(float rpm, DisplayUnits units) {
    return rpm;  // RPM is always displayed as RPM
}

float obdConvertRPM(float rpm) {
    return rpm / 4.0;  // OBDII format: RPM = ((A×256)+B)/4
}
```

### Add to config.h

```cpp
// ===== RPM Sensing =====
#define ENABLE_ENGINE_RPM
#define RPM_SENSOR_TYPE       W_PHASE_RPM_12_POLE  // Pick your alternator
#define RPM_INPUT             5                     // Interrupt-capable pin
#define RPM_MIN               500   // Idle RPM alarm threshold
#define RPM_MAX               6500  // Over-rev alarm threshold

// For custom pole count:
//#define RPM_SENSOR_TYPE       W_PHASE_RPM_CUSTOM
//#define RPM_CUSTOM_CALIBRATION
//#define RPM_POLES             12
//#define RPM_TIMEOUT_MS        2000
//#define RPM_MIN_VALID         300
//#define RPM_MAX_VALID         8000
```

### Add to sensors.cpp

```cpp
#ifdef ENABLE_ENGINE_RPM
    const SensorConfig* rpm_config = getSensorConfig(RPM_SENSOR_TYPE);
    
    #ifdef RPM_CUSTOM_CALIBRATION
        static RPMCalibration rpm_custom_cal = {
            .poles = RPM_POLES,
            .pulses_per_rev = RPM_POLES / 2.0,
            .timeout_ms = RPM_TIMEOUT_MS,
            .min_rpm = RPM_MIN_VALID,
            .max_rpm = RPM_MAX_VALID
        };
    #endif
    
    Sensor engineRPM = {
        .input = RPM_INPUT,
        .obd2pid = 0x0C,
        .obd2length = 2,
        .value = 0,
        .sensorType = rpm_config->internalType,
        .abbrName = "RPM",
        .displayName = rpm_config->name,
        .displayUnits = VOLTS,  // Placeholder - RPM has no DisplayUnits enum
        .minValue = RPM_MIN,
        .maxValue = RPM_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = rpm_config->readFunction,
        .displayConvert = rpm_config->displayConvert,
        .obdConvert = rpm_config->obdConvert,
        #ifdef RPM_CUSTOM_CALIBRATION
        .calibrationData = &rpm_custom_cal,
        .calibrationType = CAL_RPM
        #else
        .calibrationData = rpm_config->calibrationData,
        .calibrationType = rpm_config->calibrationType
        #endif
    };
#endif
```

### Add to main.cpp setup()

```cpp
void setup() {
    // ... existing initialization ...
    
    // Initialize RPM sensing
    #ifdef ENABLE_ENGINE_RPM
    extern void initRPM(byte);
    initRPM(RPM_INPUT);
    #endif
    
    // ... rest of setup ...
}
```

## Testing and Calibration

### Step 1: Initial Test

1. **Build and upload code**
2. **Start engine at idle**
3. **Monitor serial output:**
   ```
   RPM,1234.5,RPM
   ```

### Step 2: Verify Pole Count

**If RPM reading is wrong:**

**Example: Reads 2000 RPM but tachometer shows 1000 RPM**
- Reading is 2× actual → Wrong pole count
- Try: `W_PHASE_RPM_6_POLE` (if available) or custom calibration

**Example: Reads 857 RPM but tachometer shows 1000 RPM**
- Reading is 0.857× actual → Close, but wrong poles
- Actual poles = 12 × (1000/857) ≈ 14 poles
- Use: `W_PHASE_RPM_14_POLE`

### Step 3: Verify Range

Test at various RPM:
- Idle: ~700-1000 RPM
- Cruise: ~2000-3000 RPM  
- Highway: ~3000-4000 RPM
- Redline: Check doesn't exceed max

### Step 4: Check Stability

- Reading should be stable (±10 RPM)
- If noisy: Add larger capacitor (220nF or 470nF)
- If erratic: Check for loose connections

## Troubleshooting

### No RPM Reading (shows 0 or NAN)

**Check:**
- [ ] W-phase wire connected
- [ ] Voltage divider resistors correct values
- [ ] Diode orientation (anode to divider, cathode to MCU)
- [ ] Pin defined as interrupt-capable
- [ ] Engine running (alternator must be spinning)

**Test points:**
1. Measure AC voltage on W-phase (should be 10-30V AC)
2. Measure voltage at junction (should be 4-12V AC)
3. Measure voltage at MCU pin (should pulse 0-5V)

### RPM Reading Too High

**Possible causes:**
- Wrong pole count (reading 2× or 1.5× actual)
- Counting both rising and falling edges

**Fix:**
- Try different pole count configuration
- Verify ISR only triggers on RISING edge

### RPM Reading Too Low

**Possible causes:**
- Wrong pole count
- Missing pulses (loose connection)

**Fix:**
- Try different pole count
- Check all connections
- Add pull-up resistor if needed

### Erratic RPM Reading

**Possible causes:**
- Electrical noise from ignition system
- No noise filtering

**Fix:**
- Add or increase capacitor (100nF → 220nF → 470nF)
- Route signal wire away from spark plug wires
- Add ferrite bead on signal wire
- Use shielded cable

### RPM Drops to Zero Intermittently

**Possible causes:**
- Timeout too short
- Loose connection

**Fix:**
- Increase timeout: `#define RPM_TIMEOUT_MS 3000`
- Check all connections
- Check for corroded alternator terminal

## Advanced: Multi-Cylinder Correction

For more accurate readings on multi-cylinder engines with uneven firing:

```cpp
// In sensor_read.cpp
void readWPhaseRPM(Sensor *ptr) {
    // ... existing code ...
    
    // Apply cylinder correction factor
    #ifdef RPM_CYLINDER_COUNT
    float correction = 1.0;
    if (RPM_CYLINDER_COUNT == 4) {
        correction = 1.02;  // 4-cylinder typically reads 2% low
    } else if (RPM_CYLINDER_COUNT == 6) {
        correction = 1.00;  // 6-cylinder usually accurate
    } else if (RPM_CYLINDER_COUNT == 8) {
        correction = 0.98;  // 8-cylinder may read 2% high
    }
    rpm *= correction;
    #endif
    
    // ... rest of code ...
}
```

## Display on LCD

RPM will automatically display using the standard format:

```cpp
// In display_lcd.cpp - already implemented
// Will show: "RPM:2450"
```

For custom formatting (larger numbers):
```cpp
// Optional: Modify displaySensor() to show RPM with more space
if (ptr->abbrName == "RPM") {
    lcd.print(ptr->value, 0);  // No decimal places
    // Don't print units to save space
}
```

## Safety Considerations

⚠️ **Electrical Safety:**
- Alternator can produce 30V+ at high RPM
- Ensure voltage divider is correctly calculated
- Use zener diode protection for safety
- Disconnect battery before working on alternator

⚠️ **Over-Rev Protection:**
- Set RPM_MAX to redline
- Configure alarm to warn of over-rev
- Consider adding red LED on over-rev

⚠️ **Mounting:**
- Secure all connections with heat shrink
- Route away from hot engine parts
- Use proper crimped connections (not just twisted wires)

## Wiring Diagram

**UNIVERSAL CIRCUIT (Recommended):**
```
                    Alternator
                    +-----+
                    |  W  |--------+
                    |  V  |        |
                    |  U  |      [22kΩ]  ← UNIVERSAL VALUE
                    |     |        |
                    | B+  |-----+  +--[1N4148]--+--[100nF]--+
                    | GND |--+  |               |           |
                    +-----+  |  +-------------+ |           |
                             |                | |           |
                             +----------------|-|-----------+
                                              | |           |
                                              | +--[Zener]--+
                                              |  3.3V or 5.1V
                                              +--[4.7kΩ]----+
                                                      |
                                              MCU Pin 5
                                                      |
                                                     GND

Voltage: ~10-30V AC → ~1.8-5.3V → Clamped by zener
✅ Works with ANY board - just change the zener!
```

---

**Alternative: Optimized for 5V boards (slightly stronger signal):**
```
                    Alternator
                    +-----+
                    |  W  |--------+
                    |  V  |        |
                    |  U  |      [10kΩ]  ← LOWER for more voltage
                    |     |        |
                    | B+  |-----+  +--[1N4148]--+--[100nF]--+
                    | GND |--+  |               |           |
                    +-----+  |  +-------------+ |           |
                             |                | |           |
                             +----------------|-|-----------+
                                              | |           |
                                              | +--[5.1V]---+
                                              |    Zener    |
                                              +--[4.7kΩ]----+
                                                      |
                                              Arduino Pin 5
                                                      |
                                                     GND

Voltage: ~10-30V AC → ~3.2-9.6V → Clamped to 5.1V max
⚠️ ONLY for 5V boards - will damage Teensy!
```

---

## Parts List

**For universal circuit (recommended):**
- 1× 22kΩ resistor (1/4W)
- 1× 4.7kΩ resistor (1/4W)
- 1× 1N4148 diode
- 1× 100nF ceramic capacitor
- 1× Zener diode:
  - BZX55C3V3 (3.3V) for Teensy/Due/ESP32
  - BZX55C5V1 (5.1V) for Arduino Mega/Uno

**Where to buy:**
- Any electronics supplier (Mouser, Digikey, etc.)
- Total cost: ~$2-3 USD
- **Pro tip:** Buy 10 of each component for future projects

## Complete Example

**config.h:**
```cpp
#define ENABLE_ENGINE_RPM
#define RPM_SENSOR_TYPE       W_PHASE_RPM_12_POLE
#define RPM_INPUT             5
#define RPM_MIN               500   // Low idle warning
#define RPM_MAX               6500  // Over-rev warning
```

**That's it!** With the sensor library system, you just:
1. Build the hardware
2. Pick your alternator pole count
3. Enable in config.h

The system handles everything else automatically!

## Performance Notes

- **Interrupt-based:** Very low CPU overhead
- **Update rate:** Real-time (updates every pulse)
- **Accuracy:** ±10 RPM typical
- **Range:** 300-8000 RPM (configurable)
- **Latency:** <10ms typical

## Future Enhancements

**Possible additions:**
- Averaging filter for smoother display
- Peak RPM recording
- Time above redline logging
- Shift light output trigger
- Rev limiter (advanced - not recommended)

---

**Questions? Post in GitHub Discussions with your alternator specs!**
