# Adding Custom Sensors to openEMS

This guide shows you how to add custom sensors or multiple instances of the same sensor type.

## Table of Contents
1. [Using Preset Calibrations](#using-preset-calibrations)
2. [Custom Calibrations for Existing Sensors](#custom-calibrations-for-existing-sensors)
3. [Adding a Second Instance of an Existing Sensor](#adding-a-second-instance)
4. [Adding a Completely New Sensor Type](#adding-a-completely-new-sensor-type)

---

## Using Preset Calibrations

**Easiest option!** Just pick a sensor from `src/lib/sensor_library.h` in `config.h`:

```cpp
// In config.h
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_STEINHART
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100
```

**That's it!** No other files need to be modified.

---

## Custom Calibrations for Existing Sensors

If you want to use a different calibration for an existing sensor slot (CHT, EGT, Coolant, Oil, etc.):

### Step 1: Define the custom calibration in `advanced_config.h`

```cpp
// In src/advanced_config.h
#define COOLANT_CUSTOM_CALIBRATION
#ifdef COOLANT_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_THERMISTOR(coolant,
        2200.0,         // bias_resistor
        1.764e-03,      // steinhart_a
        2.499e-04,      // steinhart_b
        6.773e-08       // steinhart_c
    )
#endif
```

### Step 2: Enable the sensor in `config.h`

```cpp
// In config.h
#define ENABLE_COOLANT_TEMP
#define COOLANT_SENSOR_TYPE   VDO_120C_STEINHART  // Still needed for type info
#define COOLANT_TEMP_INPUT    A2
#define COOLANT_TEMP_MIN      -1
#define COOLANT_TEMP_MAX      100
```

**That's it!** The custom calibration will automatically override the preset.

### Available Helper Macros

```cpp
// Thermistor (Steinhart-Hart)
DEFINE_CUSTOM_THERMISTOR(name, bias_resistor, steinhart_a, steinhart_b, steinhart_c)

// Linear pressure sensor
DEFINE_CUSTOM_PRESSURE_LINEAR(name, voltage_min, voltage_max, pressure_min, pressure_max)

// Polynomial pressure sensor (VDO style)
DEFINE_CUSTOM_PRESSURE_POLY(name, bias_resistor, poly_a, poly_b, poly_c)

// RPM sensor
DEFINE_CUSTOM_RPM(name, poles, timeout_ms, min_rpm, max_rpm)
```

---

## Adding a Second Instance of an Existing Sensor

Example: Adding a second coolant temperature sensor

### Method 1: Using a Preset (Simplest)

#### Step 1: Define in `config.h`

```cpp
// In config.h
#define ENABLE_COOLANT_TEMP_2
#define COOLANT_TEMP_2_SENSOR_TYPE  VDO_150C_STEINHART
#define COOLANT_TEMP_2_INPUT        A5
#define COOLANT_TEMP_2_MIN          -1
#define COOLANT_TEMP_2_MAX          150
```

#### Step 2: Add the sensor definition to `sensors.cpp`

```cpp
// In src/inputs/input_manager.cpp, after the other sensor definitions:

#ifdef ENABLE_COOLANT_TEMP_2
    const SensorConfig* coolant2_config = getSensorConfig(COOLANT_TEMP_2_SENSOR_TYPE);

    Sensor coolantTemp2 = {
        .input = COOLANT_TEMP_2_INPUT,
        .obd2pid = 0xCE,  // Pick an unused PID
        .obd2length = 1,
        .value = 0,
        .sensorType = READ_SENSOR_TYPE(coolant2_config),
        .abbrName = "WTR2",
        .displayName = READ_NAME(coolant2_config),
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        .minValue = COOLANT_TEMP_2_MIN,
        .maxValue = COOLANT_TEMP_2_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = READ_READ_FUNC(coolant2_config),
        .displayConvert = READ_DISPLAY_CONV(coolant2_config),
        .obdConvert = READ_OBD_CONV(coolant2_config),
        .calibrationData = READ_CAL_DATA(coolant2_config),
        .calibrationType = READ_CAL_TYPE(coolant2_config)
    };
#endif
```

#### Step 3: Add to sensor array in `sensors.cpp`

```cpp
// In src/inputs/input_manager.cpp, find the sensors[] array and add:

Sensor *sensors[] = {
    #ifdef ENABLE_CHT
    &CHT,
    #endif
    #ifdef ENABLE_EGT
    &EGT,
    #endif
    #ifdef ENABLE_COOLANT_TEMP
    &coolantTemp,
    #endif
    #ifdef ENABLE_COOLANT_TEMP_2    // ADD THIS
    &coolantTemp2,
    #endif
    // ... rest of sensors ...
};
```

### Method 2: Using Custom Calibration

Same as Method 1, but also add to `advanced_config.h`:

```cpp
// In src/advanced_config.h
#ifdef ENABLE_COOLANT_TEMP_2
    #define COOLANT_TEMP_2_CUSTOM_CALIBRATION
    #ifdef COOLANT_TEMP_2_CUSTOM_CALIBRATION
        DEFINE_CUSTOM_THERMISTOR(coolant_temp_2,
            2200.0,         // bias_resistor
            1.764e-03,      // steinhart_a
            2.499e-04,      // steinhart_b
            6.773e-08       // steinhart_c
        )
    #endif
#endif
```

Then in `sensors.cpp`, use the custom calibration:

```cpp
#ifdef ENABLE_COOLANT_TEMP_2
    const SensorConfig* coolant2_config = getSensorConfig(COOLANT_TEMP_2_SENSOR_TYPE);

    #ifdef COOLANT_TEMP_2_CUSTOM_CALIBRATION
        // Custom cal already defined in advanced_config.h
    #endif

    Sensor coolantTemp2 = {
        .input = COOLANT_TEMP_2_INPUT,
        .obd2pid = 0xCE,
        .obd2length = 1,
        .value = 0,
        .sensorType = READ_SENSOR_TYPE(coolant2_config),
        .abbrName = "WTR2",
        .displayName = READ_NAME(coolant2_config),
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        .minValue = COOLANT_TEMP_2_MIN,
        .maxValue = COOLANT_TEMP_2_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = READ_READ_FUNC(coolant2_config),
        .displayConvert = READ_DISPLAY_CONV(coolant2_config),
        .obdConvert = READ_OBD_CONV(coolant2_config),
        #ifdef COOLANT_TEMP_2_CUSTOM_CALIBRATION
        .calibrationData = &coolant_temp_2_custom_cal,
        .calibrationType = CAL_THERMISTOR_STEINHART
        #else
        .calibrationData = READ_CAL_DATA(coolant2_config),
        .calibrationType = READ_CAL_TYPE(coolant2_config)
        #endif
    };
#endif
```

---

## Adding a Completely New Sensor Type

For sensors that don't fit existing patterns:

### Step 1: Define in `config.h`

```cpp
#define ENABLE_MY_CUSTOM_SENSOR
#define MY_CUSTOM_SENSOR_INPUT  A7
#define MY_CUSTOM_SENSOR_MIN    0
#define MY_CUSTOM_SENSOR_MAX    100
```

### Step 2: Define calibration and sensor in `sensors.cpp`

```cpp
// In src/inputs/input_manager.cpp, after other sensor definitions:

#ifdef ENABLE_MY_CUSTOM_SENSOR
    // Define calibration (stays in RAM, no PROGMEM needed)
    static ThermistorSteinhartCalibration my_custom_cal = {
        .bias_resistor = 2200.0,
        .steinhart_a = 1.764e-03,
        .steinhart_b = 2.499e-04,
        .steinhart_c = 6.773e-08
    };

    Sensor myCustomSensor = {
        .input = MY_CUSTOM_SENSOR_INPUT,
        .obd2pid = 0xCF,  // Pick an unused PID (0xCE-0xFF are free)
        .obd2length = 1,
        .value = 0,
        .sensorType = THERMISTOR_STEINHART,  // Or your type
        .abbrName = "CUST",
        .displayName = "My Custom Sensor",
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        .minValue = MY_CUSTOM_SENSOR_MIN,
        .maxValue = MY_CUSTOM_SENSOR_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = readThermistorSteinhart,  // Or your read function
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationData = &my_custom_cal,
        .calibrationType = CAL_THERMISTOR_STEINHART
    };
#endif
```

### Step 3: Add to sensor array in `sensors.cpp`

```cpp
Sensor *sensors[] = {
    // ... existing sensors ...
    #ifdef ENABLE_MY_CUSTOM_SENSOR
    &myCustomSensor,
    #endif
};
```

---

## RAM Usage Notes

- **Preset calibrations**: Stored in PROGMEM (flash), use ~0 bytes RAM ✅
- **Custom calibrations via macros**: Stored in RAM, use ~20-40 bytes each ⚠️
- **Fully custom sensors**: Stored in RAM, use ~20-40 bytes for calibration ⚠️

For Arduino Uno (2KB RAM total), you can add a few custom sensors without issues. If you need many custom sensors, consider using preset calibrations where possible.

---

## Available Sensor Types

See `src/lib/sensor_library.h` for the full list of preset sensors:

### Thermocouples
- `K_TYPE_THERMOCOUPLE_MAX6675`
- `K_TYPE_THERMOCOUPLE_MAX31855`

### Thermistors
- `VDO_120C_LOOKUP`, `VDO_120C_STEINHART`
- `VDO_150C_LOOKUP`, `VDO_150C_STEINHART`
- `GENERIC_NTC_10K_3950`, `GENERIC_NTC_10K_3435`, `GENERIC_NTC_10K_3380`

### Pressure Sensors
- `VDO_5BAR_PRESSURE`, `VDO_2BAR_PRESSURE`
- `GENERIC_0_5V_5BAR`, `GENERIC_0_5V_10BAR`, `GENERIC_0_5V_100PSI`
- `MPX4250AP_PRESSURE`

### RPM Sensors
- `W_PHASE_RPM_12_POLE`, `W_PHASE_RPM_14_POLE`, `W_PHASE_RPM_16_POLE`

### Environmental Sensors
- `BME280_AMBIENT_TEMPERATURE`
- `BME280_BAROMETRIC_PRESSURE`
- `BME280_RELATIVE_HUMIDITY`
- `BME280_ESTIMATED_ALTITUDE`

### Digital Sensors
- `DIGITAL_FLOAT_SWITCH`

### Voltage Sensors
- `STANDARD_12V_DIVIDER`

---

## Need Help?

- Check `src/advanced_config.h` for more examples
- Look at existing sensor definitions in `src/inputs/input_manager.cpp`
- See `src/lib/sensor_library.h` for available preset sensors
