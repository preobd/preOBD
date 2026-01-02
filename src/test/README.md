# Test Mode for openEMS

Test mode allows comprehensive testing of all openEMS outputs (LCD, CAN bus, alarms, SD logging, RealDash, Serial) **without requiring physical sensors**. It uses function pointer substitution to inject simulated sensor values while preserving all existing sensor reading code.

## Features

- ✅ **5 Pre-defined Test Scenarios** with realistic sensor behavior
- ✅ **Dynamic Value Generation**: Static, ramps, sine waves, square waves, random walks
- ✅ **Tests All Outputs**: LCD, CAN, Serial, SD, RealDash, Alarms
- ✅ **Mode Agnostic**: Works with both EEPROM and compile-time configs
- ✅ **Zero Overhead**: Completely removed when `ENABLE_TEST_MODE` is not defined
- ✅ **Non-invasive**: No changes to existing sensor reading code

## Memory Footprint

| Configuration | Flash | RAM |
|---------------|-------|-----|
| Test mode enabled | +4.3 KB | +185 bytes |
| Test mode disabled | 0 bytes | 0 bytes |

## Quick Start

### 1. Enable Test Mode

Edit `src/config.h`:

```cpp
#define ENABLE_TEST_MODE
```

### 2. Compile and Upload

```bash
pio run -t upload
```

### 3. Activate Test Mode

**Hardware Method (Recommended):**
- Connect `TEST_MODE_TRIGGER_PIN` (defined in `config.h`) to GND
- Power on or reset the device
- Test mode activates automatically

**Serial Method (EEPROM mode only):**
```
> TEST 0
Starting test scenario 0: Normal Operation
```

### 4. Monitor Output

Open serial monitor to see test mode status:

```bash
pio device monitor
```

Expected output:
```
========================================
  TEST MODE TRIGGER DETECTED!
========================================

Available Test Scenarios:
========================================
0. Normal Operation (120s, 7 inputs)
1. Alarm Test - Overheating (30s, 3 inputs)
2. Sensor Fault Simulation (45s, 4 inputs)
3. Engine Startup Sequence (120s, 5 inputs)
4. Dynamic Driving Conditions (180s, 6 inputs)
========================================

========================================
Starting test scenario 0: Normal Operation
Duration: 120 seconds
Input overrides: 7
========================================
```

### 5. Observe Outputs

While test mode is active:
- **LCD**: Shows simulated sensor values
- **Serial**: Outputs CSV data (if enabled)
- **CAN Bus**: Transmits OBDII frames with test data
- **Alarms**: Trigger when values exceed thresholds (in alarm scenarios)
- **SD Card**: Logs test data (if enabled)

### 6. Exit Test Mode

- Remove the GND jumper from `TEST_MODE_TRIGGER_PIN`
- Reset the device
- Normal sensor operation resumes

## Test Scenarios

### Scenario 0: Normal Operation (120s)
**Purpose:** Verify all outputs work with typical operating values

**Simulated Sensors:**
- CHT: 180°C steady
- EGT: 350°C with sinusoidal oscillation (340-360°C, 5s period)
- Coolant: 85°C steady
- Oil Temp: Warming from 40°C to 90°C over 60 seconds
- Ambient: 22°C steady
- Barometric: 1.013 bar with slight oscillation
- Humidity: 45% with slow drift

**What to Check:**
- LCD displays all values correctly
- Values update smoothly
- No alarms triggered
- CAN frames transmit properly
- SD logging works (if enabled)

### Scenario 1: Alarm Test - Overheating (30s)
**Purpose:** Test alarm buzzer, display warnings, threshold checking

**Simulated Sensors:**
- CHT: Rising from 180°C to 270°C (exceeds threshold)
- EGT: Rising from 400°C to 650°C (exceeds threshold)
- Coolant: Rising from 85°C to 115°C (exceeds threshold)

**What to Check:**
- Alarm buzzer activates when thresholds exceeded
- LCD shows warning indicators
- Silence button works (pin 4)
- Multiple simultaneous alarms handled correctly

### Scenario 2: Sensor Fault Simulation (45s)
**Purpose:** Test NaN handling, error displays, fault detection

**Simulated Sensors:**
- CHT: Normal value (180°C)
- EGT: Always returns NaN (simulated fault)
- Coolant: Normal (85°C)
- Oil Temp: Normal (88°C)

**What to Check:**
- LCD shows "ERR" or similar for failed sensor
- CAN frames handle NaN correctly
- Other sensors continue working normally
- No crash or lockup

### Scenario 3: Engine Startup Sequence (120s)
**Purpose:** Simulate realistic cold start with all sensors changing

**Simulated Sensors:**
- CHT: Cold start to operating temp (20°C → 180°C over 90s)
- EGT: Rises faster (25°C → 350°C over 60s)
- Coolant: Gradual warmup (18°C → 85°C over 120s)
- Oil Temp: Slower warmup (15°C → 88°C over 120s)
- Ambient: Steady (8°C - cold morning!)

**What to Check:**
- All sensors ramp smoothly
- Different rates handled correctly
- LCD updates reflect changing values
- Data logging captures startup sequence

### Scenario 4: Dynamic Driving Conditions (180s)
**Purpose:** Test rapid value changes, LCD refresh rate, stress testing

**Simulated Sensors:**
- CHT: Oscillating (170-190°C, 30s period)
- EGT: High variation (300-450°C, 15s period)
- Coolant: Moderate oscillation (82-92°C, 25s period)
- Oil Temp: Slow variation (85-95°C, 40s period)
- Ambient: Steady (28°C)
- Barometric: Slight variation (altitude changes)

**What to Check:**
- LCD refresh handles rapid changes
- CAN bus not overwhelmed
- SD logging keeps up
- No display flickering or artifacts

## Configuration Options

In `src/config.h`:

```cpp
#define ENABLE_TEST_MODE              // Enable/disable test mode
#define TEST_MODE_TRIGGER_PIN 8       // Pin to pull LOW for activation
#define DEFAULT_TEST_SCENARIO 0       // Scenario to run on trigger (0-4, or 0xFF for none)
```

## Value Generation Types

Test mode supports various dynamic value types:

| Type | Description | Example Use |
|------|-------------|-------------|
| `TEST_STATIC` | Constant value | Oil pressure at 3.5 bar |
| `TEST_RAMP_UP` | Linear increase | Coolant warming 20°C → 85°C |
| `TEST_RAMP_DOWN` | Linear decrease | Oil pressure drop on failure |
| `TEST_SINE_WAVE` | Smooth oscillation | EGT varying with load (340-360°C) |
| `TEST_SQUARE_WAVE` | Step changes | Intermittent sensor fault |
| `TEST_RANDOM` | Random walk | Boost pressure variations |
| `TEST_NAN` | Always NaN | Disconnected sensor |

## How It Works

1. **Function Pointer Backup**: Original sensor `readFunction` pointers saved
2. **Pointer Substitution**: All enabled inputs redirected to `readTestInput()`
3. **Value Generation**: `readTestInput()` looks up test config, generates time-based value
4. **Normal Operation**: Rest of system (outputs, alarms, display) operates normally
5. **Restoration**: On exit, original function pointers restored

This approach is **completely non-invasive** - no sensor reading code is modified!

## Architecture

```
src/test/
├── test_mode.h              - Public API and data structures
├── test_mode.cpp            - Core implementation
├── test_scenarios.h         - Pre-defined scenarios (PROGMEM)
├── test_value_generator.cpp - Time-based value generation
└── README.md                - This file
```

## Adding Custom Scenarios

To add your own test scenario, edit `src/test/test_scenarios.h`:

```cpp
// Define input configurations
const PROGMEM InputTestConfig myScenario_configs[] = {
    { .inputIndex = 0, .valueType = TEST_STATIC, .value1 = 200.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },
    // ... more inputs
};

// Define scenario
const PROGMEM TestScenario scenario_myCustomTest = {
    .name = "My Custom Test",
    .duration_ms = 60000,  // 60 seconds
    .numInputOverrides = 1,
    .inputConfigs = myScenario_configs
};

// Add to registry
const TestScenario* const TEST_SCENARIOS[] PROGMEM = {
    &scenario1_normalOperation,
    &scenario2_alarmTest,
    &scenario3_sensorFault,
    &scenario4_engineStartup,
    &scenario5_drivingConditions,
    &scenario_myCustomTest  // <-- Add here
};
```

## Troubleshooting

### Test Mode Not Activating

**Check:**
- `TEST_MODE_TRIGGER_PIN` is actually connected to GND (use multimeter)
- `ENABLE_TEST_MODE` is defined in `config.h`
- Firmware was recompiled and uploaded after enabling test mode
- Serial monitor shows "TEST MODE TRIGGER DETECTED"

### Values Not Changing

**Check:**
- Correct input indices in scenario config
- Scenario duration hasn't expired
- Serial monitor for scenario completion message
- Input is actually enabled (`flags.isEnabled`)

### Compilation Errors

**Check:**
- All test files present in `src/test/`
- `MAX_INPUTS` defined in `input_manager.h`
- No syntax errors in custom scenarios

### Memory Issues

**Check:**
- Current platform has enough RAM (test mode adds ~185 bytes)
- Flash usage not exceeding board limits (test mode adds ~4.3 KB)
- Consider using EEPROM mode instead of compile-time if RAM is tight

## FAQ

**Q: Can I leave test mode enabled in production?**
A: Yes! With `TEST_MODE_TRIGGER_PIN` floating HIGH (not pulled LOW), test mode is initialized but not activated. The overhead is minimal (~4.3 KB flash). However, for absolute minimal footprint, comment out `ENABLE_TEST_MODE` for production builds.

**Q: Can I change scenarios without rebooting?**
A: Currently no - test mode is activated on boot. Future enhancement could add serial commands to switch scenarios at runtime.

**Q: Why do some inputs show NaN during test mode?**
A: Only inputs explicitly configured in the scenario are simulated. Other inputs return NaN unless their original read functions are being called.

**Q: Can I use test mode with EEPROM configuration?**
A: Yes! Test mode is mode-agnostic and works with both EEPROM and compile-time configuration.

**Q: How do I test specific input indices?**
A: Use serial command `LIST INPUTS` (EEPROM mode) or check your `config.h` / `sensors_config.h` to see input indices. Input 0 is the first enabled input, Input 1 is the second, etc.

## Support

For issues or questions:
- Check the main [README.md](../../../README.md)
- Review the [testing plan](../../../TESTING_PLAN.md)
- Open an issue on GitHub
