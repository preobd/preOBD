# Adding Sensors to openEMS

**Guide for adding new sensor types to the sensor library**

---

## Overview

openEMS uses a unified input-based architecture where sensors are defined in the sensor library (`src/lib/sensor_library.h`) with their calibration data in a separate file (`src/lib/sensor_calibration_data.h`). This separation keeps calibration data in flash memory (PROGMEM) rather than RAM, which is critical for memory-constrained platforms like Arduino Uno.

---

## Quick Reference: Adding a Pre-Defined Sensor

If your sensor is already in the library, you just need to configure it:

**Compile-Time (config.h):**
```cpp
#define INPUT_0_PIN            A2
#define INPUT_0_APPLICATION    COOLANT_TEMP
#define INPUT_0_SENSOR         VDO_120C_LOOKUP
```

**Runtime (serial commands):**
```
SET A2 APPLICATION COOLANT_TEMP
SET A2 SENSOR VDO_120C_LOOKUP
ENABLE A2
SAVE
```

---

## Architecture Overview

### The Three Layers

1. **Application Layer** (`application_presets.h`)
   - Defines what you're measuring (CHT, OIL_PRESSURE, etc.)
   - Sets default sensors, units, alarm thresholds
   - Maps to OBD-II PIDs

2. **Sensor Layer** (`sensor_library.h`)
   - Defines physical sensors (MAX6675, VDO_120C_LOOKUP, etc.)
   - Links sensors to read functions
   - Points to calibration data

3. **Calibration Layer** (`sensor_calibration_data.h`)
   - Contains actual calibration values
   - Lookup tables, Steinhart-Hart coefficients, polynomial coefficients
   - All stored in PROGMEM (flash memory)

### Data Flow

```
Physical Sensor → Read Function → Calibration → Engineering Units
       ↓               ↓               ↓              ↓
    VDO 120C    readThermistorLookup  vdo120_lookup  85.3°C
```

### Time-Sliced Architecture

openEMS uses a **non-blocking time-sliced loop** where each sensor reads at its own optimal interval:

- **MAX6675 thermocouple:** 250ms (respects ~220ms conversion time)
- **MAX31855 thermocouple:** 100ms (faster chip)
- **Analog sensors:** 50ms (fast for responsive alarms)
- **Digital inputs:** 50ms (fast polling)

This architecture ensures:
- Slow sensors don't block fast sensors
- No blocking `delay()` calls in the main loop
- Serial commands processed immediately
- Each sensor operates at peak efficiency

---

## Adding a New Sensor Type

### Step 1: Add the Sensor Enum

In `src/inputs/input.h`, add your sensor to the Sensor enum:

```cpp
enum Sensor {
    SENSOR_NONE = 0,
    
    // Thermocouples
    MAX6675,
    MAX31855,
    
    // VDO Thermistors
    VDO_120C_LOOKUP,
    VDO_120C_STEINHART,
    // ... existing sensors ...
    
    // Add your new sensor here
    MY_CUSTOM_THERMISTOR,    // <-- New sensor
    
    SENSOR_COUNT  // Keep this last
};
```

### Step 2: Add Calibration Data

In `src/lib/sensor_calibration_data.h`, add the calibration data:

**For a Thermistor (Steinhart-Hart):**
```cpp
// My Custom Thermistor - 10K NTC with specific coefficients
static const PROGMEM ThermistorSteinhartCalibration my_custom_thermistor_cal = {
    .bias_resistor = 10000.0,      // 10kΩ bias resistor
    .steinhart_a = 1.125e-03,      // Steinhart-Hart A coefficient
    .steinhart_b = 2.347e-04,      // Steinhart-Hart B coefficient
    .steinhart_c = 8.566e-08       // Steinhart-Hart C coefficient
};
```

**For a Thermistor (Lookup Table):**
```cpp
// My Custom Thermistor - Lookup table
// Format: {resistance_ohms, temperature_celsius}
static const PROGMEM ThermistorLookupPoint my_custom_lookup_table[] = {
    {32650, -40},
    {15000, 0},
    {5000, 25},
    {1800, 50},
    {750, 75},
    {350, 100},
    {180, 120}
};

static const PROGMEM ThermistorLookupCalibration my_custom_lookup_cal = {
    .bias_resistor = 2200.0,
    .table = my_custom_lookup_table,
    .table_size = sizeof(my_custom_lookup_table) / sizeof(ThermistorLookupPoint)
};
```

**For a Pressure Sensor (Linear):**
```cpp
// My Custom Pressure Sensor - 0.5-4.5V output, 0-10 bar range
static const PROGMEM PressureLinearCalibration my_pressure_cal = {
    .voltage_min = 0.5,    // Voltage at minimum pressure
    .voltage_max = 4.5,    // Voltage at maximum pressure
    .pressure_min = 0.0,   // Minimum pressure (bar)
    .pressure_max = 10.0   // Maximum pressure (bar)
};
```

**For a Pressure Sensor (Polynomial):**
```cpp
// My Custom Pressure Sensor - Polynomial fit
// pressure = a + b*v + c*v^2 where v is voltage
static const PROGMEM PressurePolynomialCalibration my_poly_pressure_cal = {
    .bias_offset = 0.0,    // Offset adjustment
    .coeff_a = -0.5,       // Constant term
    .coeff_b = 1.25,       // Linear term
    .coeff_c = 0.05        // Quadratic term
};
```

### Step 3: Add to Sensor Library

In `src/lib/sensor_library.h`, add the sensor entry:

```cpp
static const PROGMEM SensorInfo SENSOR_LIBRARY[] = {
    // ... existing sensors ...

    // My Custom Thermistor (Steinhart-Hart)
    {
        .sensor = MY_CUSTOM_THERMISTOR,
        .name = "My Custom 10K NTC",
        .readFunction = readThermistorSteinhart,
        .initFunction = nullptr,  // No special init needed (analog sensors)
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = &my_custom_thermistor_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS  // Fast analog read (50ms default)
    },

    // ... rest of sensors ...
};
```

**Note on initFunction:**
- Set to `nullptr` for most sensors (analog inputs, I2C sensors like BME280)
- Only needed for sensors requiring special initialization:
  - Thermocouples (MAX6675/MAX31855): Use `initThermocoupleCS` (sets up SPI CS pin)
  - RPM sensors (W_PHASE_RPM): Use `initWPhaseRPM` (attaches interrupt)
  - Digital inputs (FLOAT_SWITCH): Use `initFloatSwitch` (sets INPUT_PULLUP mode)

**Note on minReadInterval:**
- Specifies minimum milliseconds between reads for this sensor type
- Prevents reading sensors faster than their conversion time
- Common values:
  - `250` - MAX6675 thermocouple (needs ~220ms for conversion)
  - `100` - MAX31855 thermocouple (faster than MAX6675)
  - `SENSOR_READ_INTERVAL_MS` - Fast analog/digital sensors (50ms default)
- Each sensor reads at its own optimal interval without blocking others

### Step 4: Add to Serial Parser (Runtime Mode)

In `src/inputs/serial_config.cpp`, add parsing for your sensor:

```cpp
static Sensor parseSensor(const char* sensorStr) {
    // ... existing sensors ...
    
    if (streq(sensorStr, "MY_CUSTOM_THERMISTOR")) return MY_CUSTOM_THERMISTOR;
    
    return SENSOR_NONE;
}
```

---

## Using Custom Calibration at Runtime

If you need to override the default calibration for a specific installation, you can use custom calibration in `advanced_config.h`:

```cpp
// In advanced_config.h

// Custom calibration for Input 0
#define INPUT_0_CUSTOM_CALIBRATION

// Define the custom calibration data
static ThermistorSteinhartCalibration input_0_custom_cal = {
    .bias_resistor = 2200.0,
    .steinhart_a = 1.764e-03,
    .steinhart_b = 2.499e-04,
    .steinhart_c = 6.773e-08
};
```

This allows you to use an existing sensor type but with modified calibration values.

---

## Calibration Types Reference

### CAL_THERMISTOR_STEINHART

For NTC thermistors using the Steinhart-Hart equation:
```
1/T = A + B*ln(R) + C*(ln(R))^3
```

Structure:
```cpp
struct ThermistorSteinhartCalibration {
    float bias_resistor;   // Bias resistor value in ohms
    float steinhart_a;     // Steinhart-Hart A coefficient
    float steinhart_b;     // Steinhart-Hart B coefficient
    float steinhart_c;     // Steinhart-Hart C coefficient
};
```

### CAL_THERMISTOR_LOOKUP

For thermistors with manufacturer-provided resistance/temperature tables:

Structure:
```cpp
struct ThermistorLookupPoint {
    float resistance;      // Resistance in ohms
    float temperature;     // Temperature in Celsius
};

struct ThermistorLookupCalibration {
    float bias_resistor;
    const ThermistorLookupPoint* table;
    uint8_t table_size;
};
```

### CAL_PRESSURE_LINEAR

For pressure sensors with linear voltage output:

Structure:
```cpp
struct PressureLinearCalibration {
    float voltage_min;     // Voltage at minimum pressure
    float voltage_max;     // Voltage at maximum pressure
    float pressure_min;    // Minimum pressure (bar)
    float pressure_max;    // Maximum pressure (bar)
};
```

### CAL_PRESSURE_POLYNOMIAL

For pressure sensors requiring polynomial correction:

Structure:
```cpp
struct PressurePolynomialCalibration {
    float bias_offset;     // Offset adjustment
    float coeff_a;         // Constant term
    float coeff_b;         // Linear coefficient
    float coeff_c;         // Quadratic coefficient
};
```

### CAL_VOLTAGE_DIVIDER

For voltage measurements through a resistor divider:

Structure:
```cpp
struct VoltageDividerCalibration {
    float r_high;          // Upper resistor (to voltage source)
    float r_low;           // Lower resistor (to ground)
    float offset;          // Calibration offset
};
```

### CAL_NONE

For sensors that don't require calibration (e.g., thermocouples with built-in linearization):
- Set `.calibrationType = CAL_NONE`
- Set `.defaultCalibration = nullptr`

---

## Read Functions Reference

Available read functions in `sensor_read.cpp`:

| Function | Sensor Types | Notes |
|----------|--------------|-------|
| `readMAX6675` | K-type thermocouple | SPI, CS pin required |
| `readMAX31855` | K-type thermocouple | SPI, CS pin required |
| `readThermistorSteinhart` | NTC thermistors | Uses Steinhart-Hart |
| `readThermistorLookup` | NTC thermistors | Uses lookup table |
| `readPressureLinear` | Pressure sensors | Linear V to P mapping |
| `readPressurePolynomial` | Pressure sensors | Polynomial correction |
| `readVoltageDivider` | Voltage monitoring | With divider compensation |
| `readWPhaseRPM` | Alternator RPM | Frequency measurement |
| `readBME280Temp` | BME280 | I2C temperature |
| `readBME280Pressure` | BME280 | I2C pressure |
| `readBME280Humidity` | BME280 | I2C humidity |
| `readDigitalSwitch` | Float switches | Digital input |

---

## Testing Your New Sensor

### 1. Compile and Upload

```bash
pio run -e megaatmega2560 -t upload
```

### 2. Verify in Serial Monitor

```bash
pio device monitor
```

For runtime mode:
```
LIST SENSORS
```

Your sensor should appear in the list.

### 3. Configure and Test

**Runtime mode:**
```
SET A0 APPLICATION COOLANT_TEMP
SET A0 SENSOR MY_CUSTOM_THERMISTOR
ENABLE A0
INFO A0
```

**Compile-time mode:**
```cpp
#define INPUT_0_PIN            A0
#define INPUT_0_APPLICATION    COOLANT_TEMP
#define INPUT_0_SENSOR         MY_CUSTOM_THERMISTOR
```

### 4. Validate Readings

Compare readings against a known reference:
- Use a calibrated thermometer for temperature sensors
- Use a calibrated gauge for pressure sensors
- Check multiple points across the range

---

## RAM Usage Notes

**Preset calibrations (PROGMEM):** Stored in flash, use ~0 bytes RAM ✅
**Custom calibrations via macros:** Stored in RAM, use ~20-40 bytes each ⚠️

For Arduino Uno (2KB RAM total), you can add a few custom calibrations without issues. For many custom sensors, consider adding them to the library in PROGMEM.

---

## Contributing Your Sensor

If you've calibrated a sensor that others might use:

1. Add the calibration to `sensor_calibration_data.h`
2. Add the sensor to `sensor_library.h`
3. Add parsing in `serial_config.cpp`
4. Test thoroughly
5. Submit a pull request with:
   - Sensor datasheet or manufacturer link
   - Calibration method used
   - Test results

---

## Need Help?

- Check `src/advanced_config.h` for examples
- Look at existing sensor definitions in `src/lib/sensor_library.h`
- See `src/lib/sensor_calibration_data.h` for calibration examples
- Ask in GitHub Discussions

---

**For the classic car community.**
