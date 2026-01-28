# Adding Sensors to openEMS

**Developer guide for adding new sensor types to the sensor library**

---

## Overview

This guide is for **developers and contributors** who want to add new sensor types to the openEMS sensor library. If you just want to configure an existing sensor or use custom calibration values, see:

- **[QUICK_REFERENCE.md](../../getting-started/QUICK_REFERENCE.md)** - Configure existing sensors
- **[ADVANCED_CALIBRATION_GUIDE.md](ADVANCED_CALIBRATION_GUIDE.md)** - Custom calibration for existing sensor types

openEMS uses a **registry-based architecture** where sensors are defined in PROGMEM arrays with hash-based lookups. This allows adding new sensors without breaking existing EEPROM configurations.

---

## Quick Reference: Using a Pre-Defined Sensor

If your sensor is already in the library, configure it via serial commands:

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SAVE
```

Or for static builds, use `tools/configure.py` to generate the configuration.

---

## Architecture Overview

### Registry-Based System

openEMS uses **hash-based registries** instead of enums. This provides:

- **EEPROM portability** - Sensor names are stored as hashes, not array indices
- **Runtime flexibility** - New sensors don't break existing configurations
- **Memory efficiency** - All data stored in PROGMEM (flash), not RAM

### The Three Registries

1. **Sensor Library** (`src/lib/sensor_library.h` + `src/lib/sensor_library/`)
   - Defines physical sensors (MAX6675, VDO_120C_TABLE, etc.)
   - Links sensors to read functions and calibration data
   - Modular structure: sensors organized by type in `sensor_library/sensors/`
   - ~28 sensors, ~3KB in PROGMEM

2. **Application Presets** (`src/lib/application_presets.h`)
   - Defines what you're measuring (CHT, OIL_PRESSURE, etc.)
   - Sets default sensors, units, alarm thresholds, OBD-II PIDs
   - ~16 applications, ~2KB in PROGMEM

3. **Units Registry** (`src/lib/units_registry.h`)
   - Defines display units (CELSIUS, BAR, PSI, etc.)
   - Contains conversion factors
   - ~11 units, ~500 bytes in PROGMEM

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

---

## Adding a New Sensor Type

### Step 1: Compute the Name Hash

The hash is used for EEPROM storage and lookups. Compute it first:

```bash
python3 -c "h=5381; s='MY_NEW_SENSOR'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
```

Example output: `0xABCD`

### Step 2: Add Calibration Data (if needed)

In the appropriate manufacturer file under `src/lib/sensor_calibration_data/`, add calibration data if your sensor needs it.

**For VDO sensors**: Edit `src/lib/sensor_calibration_data/vdo/vdo_calibrations.h`
**For AEM sensors**: Edit `src/lib/sensor_calibration_data/aem/aem_calibrations.h`
**For NXP sensors**: Edit `src/lib/sensor_calibration_data/nxp/nxp_calibrations.h`
**For generic sensors**: Edit `src/lib/sensor_calibration_data/generic/generic_calibrations.h`
**For system defaults**: Edit `src/lib/sensor_calibration_data/system/system_calibrations.h`
**For new manufacturers**: Create a new directory and header file (see below)

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

**For a Lookup Table Thermistor:**
```cpp
// Lookup table: ADC value → temperature (°C)
static const PROGMEM LookupEntry my_thermistor_lookup[] = {
    {50,  150.0},   // ADC 50  → 150°C
    {100, 120.0},
    {200, 100.0},
    {300,  80.0},
    {400,  60.0},
    {500,  40.0},
    {600,  25.0},
    {700,  10.0},
    {800,  -5.0},
    {900, -20.0},
};
#define MY_THERMISTOR_TABLE_SIZE 10

static const PROGMEM ThermistorLookupCalibration my_thermistor_lookup_cal = {
    .bias_resistor = 1000.0,
    .lookup_table = my_thermistor_lookup,
    .table_size = MY_THERMISTOR_TABLE_SIZE
};
```

**For a Linear Pressure Sensor:**
```cpp
// Linear 0.5-4.5V sensor, 0-10 bar range
static const PROGMEM PressureLinearCalibration my_pressure_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .pressure_min = 0.0,
    .pressure_max = 10.0
};
```

### Step 3: Find the Appropriate Category File

The sensor library is organized by sensor type in `src/lib/sensor_library/sensors/`:

| File | Sensor Types |
|------|--------------|
| `thermocouples.h` | MAX6675, MAX31855 |
| `thermistors.h` | VDO thermistors, generic NTC, linear temperature |
| `pressure.h` | Linear, polynomial, table-based pressure |
| `voltage.h` | Voltage divider |
| `frequency.h` | RPM and speed sensors |
| `environmental.h` | BME280 and other Environmental sensors |
| `digital.h` | Float switch and digital inputs |

Choose the file that matches your sensor type.

### Step 4: Add PROGMEM Strings and X_SENSOR Entry

Edit the appropriate category file (e.g., `src/lib/sensor_library/sensors/thermistors.h`):

```cpp
// ===== PROGMEM STRINGS =====
// Add near other string definitions in the file
static const char PSTR_MY_NEW_SENSOR[] PROGMEM = "MY_NEW_SENSOR";
static const char PSTR_MY_NEW_SENSOR_LABEL[] PROGMEM = "My New 10K NTC Sensor";

// ===== SENSOR ENTRIES (X-MACRO) =====
// Add to the category's macro (e.g., THERMISTOR_SENSORS)
#define THERMISTOR_SENSORS \
    /* ... existing sensors ... */ \
    X_SENSOR(PSTR_MY_NEW_SENSOR, PSTR_MY_NEW_SENSOR_LABEL, nullptr, readThermistorSteinhart, nullptr, \
             MEASURE_TEMPERATURE, CAL_THERMISTOR_STEINHART, &my_custom_thermistor_cal, \
             SENSOR_READ_INTERVAL_MS, -40.0, 150.0, 0xABCD, PIN_ANALOG)
```

The X_SENSOR macro parameters are:
1. `name` - PROGMEM name string
2. `label` - PROGMEM label string
3. `description` - PROGMEM description (or nullptr)
4. `readFunction` - Function to read the sensor
5. `initFunction` - Init function (or nullptr)
6. `measurementType` - MEASURE_TEMPERATURE, MEASURE_PRESSURE, etc.
7. `calibrationType` - CAL_THERMISTOR_STEINHART, CAL_LINEAR, etc.
8. `defaultCalibration` - Pointer to calibration data
9. `minReadInterval` - Minimum ms between reads
10. `minValue` - Sensor's physical minimum
11. `maxValue` - Sensor's physical maximum
12. `nameHash` - Precomputed hash from Step 1
13. `pinTypeRequirement` - PIN_ANALOG, PIN_DIGITAL, or PIN_I2C

### Step 5: Validate

Run the validation tool to check for hash collisions:

```bash
python3 tools/validate_registries.py
```

Expected output:
```
Validating sensor_library.h...
  ✓ No hash collisions detected
  ✓ All hashes match djb2 algorithm
```

### Step 6: Test

1. Build and upload:
   ```bash
   pio run -t upload
   pio device monitor
   ```

2. Verify sensor appears:
   ```
   LIST SENSORS
   ```

3. Configure and test:
   ```
   CONFIG
   SET A0 OIL_TEMP MY_NEW_SENSOR
   SAVE
   RUN
   INFO A0
   ```

---

## SensorInfo Field Reference

| Field | Type | Description |
|-------|------|-------------|
| `name` | `const char*` | PROGMEM string - sensor name for commands |
| `label` | `const char*` | PROGMEM string - human-readable label |
| `description` | `const char*` | PROGMEM string - optional description (can be nullptr) |
| `readFunction` | `void (*)(Input*)` | Function that reads the sensor |
| `initFunction` | `void (*)(Input*)` | Optional init function (nullptr if not needed) |
| `measurementType` | `MeasurementType` | MEASURE_TEMPERATURE, MEASURE_PRESSURE, etc. |
| `calibrationType` | `CalibrationType` | CAL_NONE, CAL_THERMISTOR_STEINHART, etc. |
| `defaultCalibration` | `const void*` | Pointer to calibration data in PROGMEM |
| `minReadInterval` | `uint16_t` | Minimum ms between reads |
| `minValue` | `float` | Minimum valid reading |
| `maxValue` | `float` | Maximum valid reading |
| `nameHash` | `uint16_t` | Precomputed djb2 hash of name |
| `pinTypeRequirement` | `PinType` | PIN_ANALOG, PIN_DIGITAL, PIN_I2C, etc. |

### initFunction Values

| Value | Use Case |
|-------|----------|
| `nullptr` | Most sensors (analog, I2C like BME280) |
| `initThermocoupleCS` | MAX6675, MAX31855 (sets up SPI CS pin) |
| `initWPhaseRPM` | W_PHASE_RPM (attaches interrupt) |
| `initFloatSwitch` | FLOAT_SWITCH (sets INPUT_PULLUP mode) |

### minReadInterval Values

| Value | Use Case |
|-------|----------|
| `250` | MAX6675 (needs ~220ms conversion time) |
| `100` | MAX31855 (faster than MAX6675) |
| `SENSOR_READ_INTERVAL_MS` | Fast analog/digital sensors (50ms default) |

---

## Adding a New Manufacturer

If you're adding sensors from a new manufacturer not currently in the calibration database, create a new manufacturer directory:

### Step 1: Create Directory and Header

```bash
mkdir -p src/lib/sensor_calibration_data/bosch/
```

Create `src/lib/sensor_calibration_data/bosch/bosch_calibrations.h`:

```cpp
/*
 * bosch_calibrations.h - Bosch Sensor Calibration Data
 *
 * Contains calibrations for Bosch sensors.
 * Add description of sensor types covered.
 */

#ifndef BOSCH_CALIBRATIONS_H
#define BOSCH_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== BOSCH SENSOR CALIBRATIONS =====

// Add your calibrations here
static const PROGMEM LinearCalibration bosch_example_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = 0.0,
    .output_max = 5.0
};

#endif // BOSCH_CALIBRATIONS_H
```

### Step 2: Register the Manufacturer

Edit `src/lib/sensor_calibration_data.h` to include your new manufacturer file:

```cpp
#include "sensor_calibration_data/vdo/vdo_calibrations.h"
#include "sensor_calibration_data/aem/aem_calibrations.h"
#include "sensor_calibration_data/nxp/nxp_calibrations.h"
#include "sensor_calibration_data/generic/generic_calibrations.h"
#include "sensor_calibration_data/system/system_calibrations.h"
#include "sensor_calibration_data/bosch/bosch_calibrations.h"  // Add this line
```

### Step 3: Add Sensors to Library

Add your sensors to the appropriate category file in `src/lib/sensor_library/sensors/`, referencing the calibrations from your new manufacturer file.

---

## Adding a New Read Function

If your sensor needs a custom read function, add it to the appropriate subdirectory under `src/inputs/sensors/`.

### Step 1: Determine the Sensor Category

Choose the appropriate subdirectory based on sensor type:
- **thermistors/** - NTC thermistor sensors
- **thermocouples/** - Thermocouple sensors (MAX chips, etc.)
- **pressure/** - Pressure sensors
- **voltage/** - Voltage measurement
- **linear/** - Generic linear sensors
- **rpm/** - RPM/frequency sensors
- **digital/** - Digital inputs
- **environmental/** - Environmental sensors

### Step 2: Create Implementation File

For example, adding a new thermistor algorithm to `src/inputs/sensors/thermistors/beta.cpp`:

```cpp
#include "../../input.h"
#include "../sensor_utils.h"
#include <Arduino.h>

/**
 * Read thermistor using simplified beta equation
 * @param input Pointer to Input structure
 */
void readThermistorBeta(Input* input) {
    // Get calibration data from PROGMEM
    ThermistorBetaCalibration cal;
    memcpy_P(&cal, input->customCalibration, sizeof(ThermistorBetaCalibration));

    // Read ADC and convert to resistance
    int rawValue = analogRead(input->pin);
    float resistance = calculateResistance(rawValue, cal.bias_resistor);

    // Beta equation: 1/T = 1/T0 + (1/B)*ln(R/R0)
    float invT = (1.0 / (cal.t0 + 273.15)) +
                 (1.0 / cal.beta) * log(resistance / cal.r0);
    float tempC = (1.0 / invT) - 273.15;

    // Validate and store result
    if (tempC < input->minValue || tempC > input->maxValue) {
        input->value = NAN;
    } else {
        input->value = tempC;
    }
}
```

### Step 3: Declare in Header

Add the declaration to the corresponding header (create if needed). For thermistors, you might add to `src/inputs/sensors/thermistors/thermistor_read.h`:

```cpp
#ifndef THERMISTOR_READ_H
#define THERMISTOR_READ_H

#include "../../input.h"

void readThermistorSteinhart(Input* input);
void readThermistorLookup(Input* input);
void readThermistorBeta(Input* input);  // Add your new function

#endif
```

### Step 4: Include in Main Build

Make sure the header is included in `src/inputs/sensor_read.h` (the main orchestrator):

```cpp
// Thermistor sensors
#include "sensors/thermistors/thermistor_read.h"
```

### Benefits of Modular Organization

- **Clear organization**: Related sensor types grouped together
- **Easier maintenance**: Changes isolated to specific sensor type
- **Reusable utilities**: Shared functions in sensor_utils.*
- **Scalability**: Easy to add new sensor categories

---

## Adding a New Application Type

Applications define what you're measuring. To add a new application:

### Step 1: Compute Hash

```bash
python3 -c "h=5381; s='MY_NEW_APP'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
```

### Step 2: Add PROGMEM Strings

In `src/lib/application_presets.h`:

```cpp
static const char PSTR_MY_NEW_APP[] PROGMEM = "MY_NEW_APP";
static const char PSTR_MY_NEW_APP_LABEL[] PROGMEM = "My New Application";
static const char PSTR_MY_NEW_APP_ABBR[] PROGMEM = "NEW";
```

### Step 3: Add to APPLICATION_PRESETS Array

```cpp
static const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    // ... existing applications ...
    
    {
        .name = PSTR_MY_NEW_APP,
        .label = PSTR_MY_NEW_APP_LABEL,
        .abbreviation = PSTR_MY_NEW_APP_ABBR,
        .measurementType = MEASURE_TEMPERATURE,
        .defaultSensorHash = 0xABCD,      // Hash of default sensor name
        .defaultUnitsHash = 0x1234,       // Hash of default units
        .minValue = -40.0,
        .maxValue = 150.0,
        .obd2Pid = 0x00,                  // No OBD-II mapping (or assign a PID)
        .obd2DataLength = 0,
        .warmupTime_ms = 30000,           // 30 second warmup for alarms
        .persistTime_ms = 2000,           // 2 second persistence
        .nameHash = 0x5678                // From Step 1
    },
};
```

---

## Calibration Types Reference

| Type | Structure | Use Case |
|------|-----------|----------|
| `CAL_NONE` | None | Sensors that don't need calibration (thermocouples) |
| `CAL_THERMISTOR_STEINHART` | `ThermistorSteinhartCalibration` | NTC thermistors with S-H coefficients |
| `CAL_THERMISTOR_TABLE` | `ThermistorLookupCalibration` | Thermistors with lookup tables |
| `CAL_PRESSURE_LINEAR` | `PressureLinearCalibration` | Linear voltage-to-pressure sensors |
| `CAL_PRESSURE_POLYNOMIAL` | `PressurePolynomialCalibration` | VDO-style resistive pressure sensors |
| `CAL_VOLTAGE_DIVIDER` | `VoltageDividerCalibration` | Voltage divider circuits |
| `CAL_RPM` | `RPMCalibration` | RPM sensors (W-phase, hall effect) |

---

## Read Functions Reference

Read functions are organized in `src/inputs/sensors/` by sensor type.

| Function | Input Type | Output | Location | Sensors |
|----------|------------|--------|----------|---------|
| `readThermistorSteinhart` | Analog | °C | sensors/thermistors/ | NTC thermistors |
| `readThermistorLookup` | Analog | °C | sensors/thermistors/ | Thermistors with tables |
| `readThermistorBeta` | Analog | °C | sensors/thermistors/ | Beta equation thermistors |
| `readLinearThermistor` | Analog | °C | sensors/thermistors/ | Linear thermistors |
| `readMAX6675` | SPI | °C | sensors/thermocouples/ | MAX6675 K-type |
| `readMAX31855` | SPI | °C | sensors/thermocouples/ | MAX31855 K-type |
| `readLinearSensor` | Analog | varies | sensors/linear/ | 0.5-4.5V sensors |
| `readPressureLinear` | Analog | bar | sensors/pressure/ | Linear voltage pressure |
| `readPressurePolynomial` | Analog | bar | sensors/pressure/ | VDO resistive pressure |
| `readVoltageDirect` | Analog | V | sensors/voltage/ | Direct voltage |
| `readVoltageDivider` | Analog | V | sensors/voltage/ | Battery voltage divider |
| `readWPhaseRPM` | Interrupt | RPM | sensors/rpm/ | Alternator W-phase |
| `readDigitalFloatSwitch` | Digital | 0/1 | sensors/digital/ | Float switches |
| `readBME280Temp` | I2C | °C | sensors/environmental/ | BME280 environmental |

---

## Testing Checklist

- [ ] Hash computed and added to SensorInfo
- [ ] Sensor appears in `LIST SENSORS`
- [ ] Can configure via `SET <pin> <app> <sensor>`
- [ ] Readings are reasonable at room temperature
- [ ] Readings respond to changes
- [ ] Min/max values are enforced
- [ ] NaN returned for invalid readings
- [ ] No RAM increase on Arduino Uno (calibration in PROGMEM)
- [ ] `validate_registries.py` passes (no hash collisions)
- [ ] EEPROM save/load works correctly

---

## Contributing Your Sensor

If you've added a useful sensor type, consider contributing it back:

1. **Test thoroughly** on actual hardware
2. **Document** the sensor (datasheet, wiring, calibration source)
3. **Follow code style** of existing sensors
4. **Run validation** tools before submitting
5. **Submit a pull request** with:
   - Code changes
   - Test results
   - Datasheet or calibration reference

---

## See Also

- **[REGISTRY_SYSTEM.md](../../architecture/REGISTRY_SYSTEM.md)** - Hash-based registry architecture
- **[ADVANCED_CALIBRATION_GUIDE.md](ADVANCED_CALIBRATION_GUIDE.md)** - Custom calibration for existing sensors
- **[SENSOR_SELECTION_GUIDE.md](../sensor-types/SENSOR_SELECTION_GUIDE.md)** - Available sensors

