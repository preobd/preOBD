/*
 * sensor_read.cpp - All sensor reading implementations
 */

#include "../config.h"
#include "../lib/platform.h"
#include "input.h"
#include "../lib/sensor_types.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// Shared BME280 object and state (lazy initialization)
static Adafruit_BME280* bme280_ptr = nullptr;
static bool bme280_initialized = false;
static uint8_t bme280_i2c_address = 0x00;  // 0 = not yet detected

// Helper macros to read calibration data from PROGMEM
#define READ_FLOAT_PROGMEM(addr) pgm_read_float(&(addr))
#define READ_BYTE_PROGMEM(addr) pgm_read_byte(&(addr))
#define READ_WORD_PROGMEM(addr) pgm_read_word(&(addr))
#define READ_PTR_PROGMEM(addr) ((const float*)pgm_read_ptr(&(addr)))

// ===== UTILITY FUNCTIONS =====

/**
 * Linear interpolation in a PROGMEM lookup table.
 *
 * Performs linear interpolation to find a Y value for a given X value in a
 * pair of lookup tables stored in flash memory (PROGMEM).
 *
 * Commonly used for thermistor resistance-to-temperature conversion, where
 * resistance values are stored in descending order (high resistance = low temp).
 *
 * @param value       The X value to look up
 * @param tableSize   Number of entries in the lookup tables
 * @param xTable      X values in PROGMEM (must be sorted, typically descending)
 * @param yTable      Corresponding Y values in PROGMEM
 * @return Interpolated Y value, or NAN if lookup fails
 *
 * @note X table is traversed backwards assuming descending order.
 *       For ascending tables, modify the iteration direction.
 */
float interpolate(float value, byte tableSize, const float* xTable, const float* yTable) {
    // Handle edge cases - read from PROGMEM
    float x0 = READ_FLOAT_PROGMEM(xTable[0]);
    float xLast = READ_FLOAT_PROGMEM(xTable[tableSize-1]);

    if (value >= x0) return READ_FLOAT_PROGMEM(yTable[0]);
    if (value <= xLast) return READ_FLOAT_PROGMEM(yTable[tableSize-1]);

    // Find the right segment
    // Iterate backwards since tables are typically in descending order
    for (int i = tableSize-1; i >= 0; i--) {
        float xi = READ_FLOAT_PROGMEM(xTable[i]);
        float xi_prev = READ_FLOAT_PROGMEM(xTable[i-1]);

        if (value >= xi && value <= xi_prev) {
            // Linear interpolation: y = y1 + ((x – x1) / (x2 – x1)) * (y2 – y1)
            float yi = READ_FLOAT_PROGMEM(yTable[i]);
            float xi_next = READ_FLOAT_PROGMEM(xTable[i+1]);
            float yi_next = READ_FLOAT_PROGMEM(yTable[i+1]);

            return yi + ((value - xi) / (xi_next - xi)) * (yi_next - yi);
        }
    }
    return NAN;
}

// Readings within this margin of 0 or ADC_MAX are considered "railed"
// (sensor disconnected, shorted, or out of range)
#define ADC_RAIL_MARGIN 3

// Centralized ADC reading with validation
// Reads analog pin twice (discarding first reading) and validates range
// Returns: ADC reading value, sets isValid to false if reading is out of range
int readAnalogPin(int pin, bool* isValid) {
    // First reading after switching pins may be inaccurate due to ADC multiplexer
    // settling and sample-and-hold capacitor charging. Read twice, keep second.
    // Note: 10ms delay between reads improves stability but is currently disabled
    // to minimize loop time. Uncomment if experiencing noisy readings.
    analogRead(pin);              // Discard first reading (multiplexer settling)
    //delay(10);                  // Optional: Allow ADC input to stabilize
    int reading = analogRead(pin); // Actual measurement

    // Check if reading is within valid range (not stuck at rails)
    *isValid = (reading < (ADC_MAX_VALUE - ADC_RAIL_MARGIN) && reading > ADC_RAIL_MARGIN);
    return reading;
}

// Calculate resistance from ADC reading using voltage divider formula
// R_sensor = reading * R_bias / (ADC_MAX - reading)
float calculateResistance(int reading, float biasResistor) {
    if (reading >= ADC_MAX_VALUE) {
        return NAN;  // Avoid division by zero
    }
    return reading * biasResistor / (ADC_MAX_VALUE - reading);
}

// ===== THERMOCOUPLE READING =====

void readMAX6675(Input *ptr) {
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ptr->pin, LOW);
    delayMicroseconds(1);

    uint16_t value = SPI.transfer(0x00);
    value <<= 8;
    value |= SPI.transfer(0x00);

    digitalWrite(ptr->pin, HIGH);
    SPI.endTransaction();

    if (value & 0x4) {
        ptr->value = NAN;  // No thermocouple attached
    } else {
        value >>= 3;
        ptr->value = value * 0.25;  // Store in Celsius
    }
}

void readMAX31855(Input *ptr) {
    uint32_t d = 0;
    uint8_t buf[4];

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ptr->pin, LOW);
    delayMicroseconds(1);

    for (int i = 0; i < 4; i++) {
        buf[i] = SPI.transfer(0x00);
    }

    digitalWrite(ptr->pin, HIGH);
    SPI.endTransaction();

    // Combine bytes
    d = buf[0];
    d <<= 8;
    d |= buf[1];
    d <<= 8;
    d |= buf[2];
    d <<= 8;
    d |= buf[3];
    
    // Check fault bits
    if (d & 0x07) {
        ptr->value = NAN;
        return;
    }

    // Extract temperature (top 14 bits, bits 31-18)
    int16_t temp_raw;
    if (d & 0x80000000) {
        // Negative temperature
        temp_raw = 0xFFFFC000 | ((d >> 18) & 0x00003FFF);
    } else {
        // Positive temperature
        temp_raw = (d >> 18) & 0x00003FFF;
    }

    ptr->value = temp_raw * 0.25;  // Store in Celsius
}

// ===== GENERIC THERMISTOR - STEINHART-HART METHOD =====

void readThermistorSteinhart(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float R_bias, A, B, C;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        R_bias = ptr->customCalibration.steinhart.bias_resistor;
        A = ptr->customCalibration.steinhart.steinhart_a;
        B = ptr->customCalibration.steinhart.steinhart_b;
        C = ptr->customCalibration.steinhart.steinhart_c;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
        // Read from preset calibration (PROGMEM)
        const ThermistorSteinhartCalibration* cal = (const ThermistorSteinhartCalibration*)ptr->presetCalibration;
        R_bias = pgm_read_float(&cal->bias_resistor);
        A = pgm_read_float(&cal->steinhart_a);
        B = pgm_read_float(&cal->steinhart_b);
        C = pgm_read_float(&cal->steinhart_c);
    } else {
        // Use defaults
        R_bias = 10000.0;
        A = 1.129241e-3;
        B = 2.341077e-4;
        C = 8.775468e-8;
    }

    // Calculate thermistor resistance
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }
    
    // Steinhart-Hart equation: 1/T = A + B*ln(R) + C*(ln(R))^3
    float logR = log(R_thermistor);
    float logR3 = logR * logR * logR;
    float temp_kelvin = 1.0 / (A + (B * logR) + (C * logR3));
    
    ptr->value = temp_kelvin - 273.15;  // Store in Celsius
}

// ===== GENERIC THERMISTOR - BETA EQUATION METHOD =====

void readThermistorBeta(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float R_bias, beta, R0, T0_celsius;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_THERMISTOR_BETA) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        R_bias = ptr->customCalibration.beta.bias_resistor;
        beta = ptr->customCalibration.beta.beta;
        R0 = ptr->customCalibration.beta.r0;
        T0_celsius = ptr->customCalibration.beta.t0;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_THERMISTOR_BETA) {
        // Read from preset calibration (PROGMEM)
        const BetaCalibration* cal = (const BetaCalibration*)ptr->presetCalibration;
        R_bias = pgm_read_float(&cal->bias_resistor);
        beta = pgm_read_float(&cal->beta);
        R0 = pgm_read_float(&cal->r0);
        T0_celsius = pgm_read_float(&cal->t0);
    } else {
        // Use defaults (typical 10K NTC thermistor at 25°C, β=3950K)
        R_bias = 10000.0;
        beta = 3950.0;
        R0 = 10000.0;
        T0_celsius = 25.0;
    }

    // Calculate thermistor resistance
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Beta equation: T(K) = 1 / (1/T0(K) + (1/β) * ln(R/R0))
    // Convert T0 from Celsius to Kelvin
    float T0_kelvin = T0_celsius + 273.15;

    // Calculate temperature in Kelvin
    float logR_ratio = log(R_thermistor / R0);
    float temp_kelvin = 1.0 / ((1.0 / T0_kelvin) + (logR_ratio / beta));

    ptr->value = temp_kelvin - 273.15;  // Store in Celsius
}

// ===== GENERIC THERMISTOR - LOOKUP TABLE METHOD =====

void readThermistorLookup(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration from PROGMEM (REQUIRED for lookup method)
    if (ptr->calibrationType != CAL_THERMISTOR_LOOKUP || ptr->presetCalibration == nullptr) {
        ptr->value = NAN;  // Can't do lookup without table
        return;
    }

    const ThermistorLookupCalibration* cal = (const ThermistorLookupCalibration*)ptr->presetCalibration;

    // Read calibration values from PROGMEM
    float R_bias = pgm_read_float(&cal->bias_resistor);
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Read lookup table info from PROGMEM
    byte table_size = pgm_read_byte(&cal->table_size);
    const float* resistance_table = (const float*)pgm_read_ptr(&cal->resistance_table);
    const float* temperature_table = (const float*)pgm_read_ptr(&cal->temperature_table);

    ptr->value = interpolate(R_thermistor, table_size,
                            resistance_table, temperature_table);
}

// ===== LINEAR SENSOR - GENERIC METHOD =====
// Works for any linear sensor: temperature, pressure, voltage, etc.
// Units determined by measurementType field

void readLinearSensor(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float V_min, V_max, output_min, output_max;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_LINEAR) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        V_min = ptr->customCalibration.pressureLinear.voltage_min;
        V_max = ptr->customCalibration.pressureLinear.voltage_max;
        output_min = ptr->customCalibration.pressureLinear.output_min;
        output_max = ptr->customCalibration.pressureLinear.output_max;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_LINEAR) {
        // Read from preset calibration (PROGMEM)
        const LinearCalibration* cal = (const LinearCalibration*)ptr->presetCalibration;
        V_min = pgm_read_float(&cal->voltage_min);
        V_max = pgm_read_float(&cal->voltage_max);
        output_min = pgm_read_float(&cal->output_min);
        output_max = pgm_read_float(&cal->output_max);
    } else {
        // Default: 0.5V-4.5V → 0-5 bar (common automotive pressure sensor)
        V_min = 0.5;
        V_max = 4.5;
        output_min = 0.0;
        output_max = 5.0;
    }

    // Convert ADC reading to voltage
    float voltage = reading * (AREF_VOLTAGE / (float)ADC_MAX_VALUE);

    // Clamp voltage to valid range
    if (voltage < V_min) voltage = V_min;
    if (voltage > V_max) voltage = V_max;

    // Linear interpolation: Y = (V - V_min) / (V_max - V_min) * (Y_max - Y_min) + Y_min
    float outputValue = ((voltage - V_min) / (V_max - V_min)) * (output_max - output_min) + output_min;

    ptr->value = outputValue;  // Store in base units (°C for temp, bar for pressure, etc.)
}

// ===== GENERIC PRESSURE SENSOR - POLYNOMIAL METHOD =====

void readPressurePolynomial(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float bias_resistor, a, b, c;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        bias_resistor = ptr->customCalibration.pressurePolynomial.bias_resistor;
        a = ptr->customCalibration.pressurePolynomial.poly_a;
        b = ptr->customCalibration.pressurePolynomial.poly_b;
        c = ptr->customCalibration.pressurePolynomial.poly_c;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        // Read from preset calibration (PROGMEM)
        const PolynomialCalibration* cal = (const PolynomialCalibration*)ptr->presetCalibration;
        bias_resistor = pgm_read_float(&cal->bias_resistor);
        a = pgm_read_float(&cal->poly_a);
        b = pgm_read_float(&cal->poly_b);
        c = pgm_read_float(&cal->poly_c);
    } else {
        ptr->value = NAN;  // Can't calculate without coefficients
        return;
    }

    // VDO sensors use quadratic equation: V = A*P² + B*P + C
    // We need to solve for P: A*P² + B*P + (C - R) = 0
    // Using quadratic formula: P = (-B ± sqrt(B² - 4*A*(C-R))) / (2*A)
    float R_sensor = calculateResistance(reading, bias_resistor);

    if (isnan(R_sensor) || R_sensor <= 0) {
        ptr->value = NAN;
        return;
    }

    c = c - R_sensor;
    
    float discriminant = (b * b) - (4.0 * a * c);
    
    if (discriminant < 0) {
        ptr->value = NAN;  // No real solution
        return;
    }
    
    // Take the positive root (pressure is always positive)
    float pressure = (-b - sqrt(discriminant)) / (2.0 * a);
    
    ptr->value = pressure;  // Store in bar
}

// ===== VOLTAGE READING =====

void readVoltageDivider(Input *ptr) {
    int reading = analogRead(ptr->pin);

    if (reading < 10) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float r1, r2, correction, offset;
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        r1 = ptr->customCalibration.voltageDivider.r1;
        r2 = ptr->customCalibration.voltageDivider.r2;
        correction = ptr->customCalibration.voltageDivider.correction;
        offset = ptr->customCalibration.voltageDivider.offset;
    } else if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
        // Read from preset calibration (PROGMEM)
        const VoltageDividerCalibration* cal = (const VoltageDividerCalibration*)ptr->presetCalibration;
        r1 = pgm_read_float(&cal->r1);
        r2 = pgm_read_float(&cal->r2);
        correction = pgm_read_float(&cal->correction);
        offset = pgm_read_float(&cal->offset);
    } else {
        // Use defaults from platform.h
        // Calculate r1 and r2 from VOLTAGE_DIVIDER_RATIO
        // If VOLTAGE_DIVIDER_RATIO = (r1 + r2) / r2, we can use any r2 and calculate r1
        r2 = 1000.0;  // Arbitrary base value
        r1 = (VOLTAGE_DIVIDER_RATIO - 1.0) * r2;
        correction = 1.0;
        offset = 0.0;
    }

    // Calculate divider ratio from resistor values
    float divider_ratio = (r1 + r2) / r2;

    // Calculate voltage: V = ADC * (AREF / ADC_MAX) * divider_ratio * correction + offset
    float voltage = (reading * AREF_VOLTAGE / (float)ADC_MAX_VALUE) * divider_ratio * correction + offset;

    ptr->value = voltage;
}

// Read voltage directly (no divider)
void readVoltageDirect(Input *ptr) {
    int reading = analogRead(ptr->pin);

    if (reading < 10) {
        ptr->value = NAN;
        return;
    }

    // Direct voltage reading: V = ADC * (AREF / ADC_MAX)
    float voltage = (reading * AREF_VOLTAGE / (float)ADC_MAX_VALUE);

    ptr->value = voltage;
}

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

// ===== SENSOR INITIALIZATION FUNCTIONS =====
// These are called during input manager initialization

// Initialize thermocouple chip select pins
void initThermocoupleCS(Input* ptr) {
    pinMode(ptr->pin, OUTPUT);
    digitalWrite(ptr->pin, HIGH);  // CS idle state is HIGH
    Serial.print(F("✓ Thermocouple CS pin "));
    Serial.print(ptr->pin);
    Serial.print(F(" for "));
    Serial.println(ptr->abbrName);
}

// Initialize W-Phase RPM sensing (interrupt-based)
void initWPhaseRPM(Input* ptr) {
    pinMode(ptr->pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(ptr->pin), rpmPulseISR, RISING);
    Serial.print(F("✓ RPM sensing on pin "));
    Serial.print(ptr->pin);
    Serial.print(F(" for "));
    Serial.println(ptr->abbrName);
}

// Initialize digital float switch
void initFloatSwitch(Input* ptr) {
    pinMode(ptr->pin, INPUT_PULLUP);  // Most float switches need pullup
    Serial.print(F("✓ Digital input on pin "));
    Serial.print(ptr->pin);
    Serial.print(F(" for "));
    Serial.println(ptr->abbrName);
}

// Read W-Phase RPM
void readWPhaseRPM(Input *ptr) {
    // Get calibration parameters
    byte poles;
    float pulley_ratio;
    float calibration_mult;
    uint16_t timeout_ms;
    uint16_t min_rpm;
    uint16_t max_rpm;

    if (ptr->flags.useCustomCalibration) {
        // Use custom calibration from Input union
        poles = ptr->customCalibration.rpm.poles;
        pulley_ratio = ptr->customCalibration.rpm.pulley_ratio;
        calibration_mult = ptr->customCalibration.rpm.calibration_mult;
        timeout_ms = ptr->customCalibration.rpm.timeout_ms;
        min_rpm = ptr->customCalibration.rpm.min_rpm;
        max_rpm = ptr->customCalibration.rpm.max_rpm;
    } else {
        // Use preset calibration from sensor library
        const SensorInfo* sensorInfo = getSensorByIndex(ptr->sensorIndex);
        if (sensorInfo && sensorInfo->defaultCalibration) {
            const RPMCalibration* cal = (const RPMCalibration*)sensorInfo->defaultCalibration;
            poles = cal->poles;
            pulley_ratio = cal->pulley_ratio;
            calibration_mult = cal->calibration_mult;
            timeout_ms = cal->timeout_ms;
            min_rpm = cal->min_rpm;
            max_rpm = cal->max_rpm;
        } else {
            // Fallback defaults (12-pole, 3:1 ratio)
            poles = 12;             // Most common automotive alternator
            pulley_ratio = 3.0;     // Typical automotive ratio
            calibration_mult = 1.0; // No adjustment
            timeout_ms = 2000;
            min_rpm = 100;
            max_rpm = 10000;
        }
    }

    // Calculate pulses per alternator revolution from poles
    // A 12-pole alternator produces 6 pulses per revolution (poles/2)
    float pulses_per_rev = (float)poles / 2.0;

    // Calculate base calibration factor (pulses per engine revolution)
    // Accounts for both alternator pulses and pulley ratio
    float calibration_factor = pulses_per_rev * pulley_ratio;

    // Calculate time since last pulse
    unsigned long now = millis();
    unsigned long time_since_pulse = now - (rpm_last_time / 1000);

    // Check for timeout (engine stopped)
    if (time_since_pulse > timeout_ms) {
        ptr->value = 0;
        return;
    }

    // Calculate ENGINE RPM from pulse interval
    // Formula: Engine_RPM = (60,000,000 / (interval × pulses_per_rev × pulley_ratio)) × calibration_mult
    if (rpm_pulse_interval > 0) {
        float engine_rpm = (60000000.0 / (rpm_pulse_interval * calibration_factor)) * calibration_mult;

        // Validate range
        if (engine_rpm >= min_rpm && engine_rpm <= max_rpm) {
            // Apply simple smoothing filter
            if (!isnan(ptr->value) && ptr->value > 0) {
                ptr->value = (ptr->value * 0.8) + (engine_rpm * 0.2);
            } else {
                ptr->value = engine_rpm;
            }
        } else {
            ptr->value = NAN;  // Out of range
        }
    }
}

// ===== BME280 INITIALIZATION =====

void initBME280(Input* ptr) {
    // If already initialized, nothing to do
    if (bme280_initialized) {
        return;
    }

    // Create BME280 object on first use
    if (!bme280_ptr) {
        bme280_ptr = new Adafruit_BME280();
    }

    // Auto-detect I2C address (try 0x76 first, then 0x77)
    if (bme280_ptr->begin(0x76, &Wire)) {
        bme280_initialized = true;
        bme280_i2c_address = 0x76;
    } else if (bme280_ptr->begin(0x77, &Wire)) {
        bme280_initialized = true;
        bme280_i2c_address = 0x77;
    }

    if (bme280_initialized) {
        Serial.print(F("✓ BME280 (0x"));
        Serial.print(bme280_i2c_address, HEX);
        Serial.print(F(") "));
        // Show virtual pin number (I2C:0, I2C:1, etc.)
        if (ptr->pin >= 0xF0) {
            Serial.print(F("I2C:"));
            Serial.print(ptr->pin - 0xF0);
            Serial.print(F(" for "));
        }
        Serial.println(ptr->abbrName);
    } else {
        Serial.println(F("⚠ BME280 not found at 0x76 or 0x77"));
        Serial.println(F("⚠ BME280 sensors will read NAN"));
        delete bme280_ptr;
        bme280_ptr = nullptr;
    }
}

// ===== BME280 READING =====

void readBME280Temp(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readTemperature();  // Store in Celsius
    } else {
        ptr->value = NAN;
    }
}

void readBME280Pressure(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readPressure() / 100000.0;  // Store in bar
    } else {
        ptr->value = NAN;
    }
}

void readBME280Humidity(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readHumidity();  // Store as percentage (0-100)
    } else {
        ptr->value = NAN;
    }
}

void readBME280Elevation(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readAltitude(SEA_LEVEL_PRESSURE_HPA);  // Store in meters
    } else {
        ptr->value = NAN;
    }
}

// ===== DIGITAL FLOAT SWITCH =====

void readDigitalFloatSwitch(Input *ptr) {
    // Read digital state from the pin
    float rawValue = (float)digitalRead(ptr->pin);

    // Support both normally closed (NC) and normally open (NO) switches
    #ifdef COOLANT_LEVEL_INVERTED
    // Normally open: Float UP (ok) = OPEN = LOW, Float DOWN (low) = CLOSED = HIGH
    ptr->value = 1.0 - rawValue;  // Invert the reading
    #else
    // Normally closed (default): Float UP (ok) = CLOSED = HIGH, Float DOWN (low) = OPEN = LOW
    ptr->value = rawValue;
    #endif
}

// ===== CONVERSION FUNCTIONS =====
// Registry-based conversion - uses data from UNITS_REGISTRY

/**
 * Convert from base units to display units using registry conversion factors
 *
 * Base units by measurement type:
 * - Temperature: Celsius
 * - Pressure: Bar
 * - Voltage: Volts
 * - RPM: RPM
 * - Humidity: Percent
 * - Elevation: Meters
 *
 * @param baseValue  Value in base units
 * @param unitsIndex Index into UNITS_REGISTRY (0-10)
 * @return           Value in display units
 */
float convertFromBaseUnits(float baseValue, uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return baseValue;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    float factor = pgm_read_float(&info->conversionFactor);
    float offset = pgm_read_float(&info->conversionOffset);

    return baseValue * factor + offset;
}

/**
 * Convert from display units to base units (inverse of convertFromBaseUnits)
 *
 * @param displayValue Value in display units
 * @param unitsIndex   Index into UNITS_REGISTRY (0-10)
 * @return             Value in base units
 */
float convertToBaseUnits(float displayValue, uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return displayValue;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    float factor = pgm_read_float(&info->conversionFactor);
    float offset = pgm_read_float(&info->conversionOffset);

    return (displayValue - offset) / factor;
}


// ===== UNIT STRING CONVERSION =====

/**
 * Get unit symbol string from registry (registry-based)
 *
 * Returns pointer to symbol string in PROGMEM. Caller should use
 * in Serial.print(F()) or similar PROGMEM-aware context.
 *
 * @param unitsIndex Index into UNITS_REGISTRY (0-10)
 * @return           Pointer to symbol string in PROGMEM
 */
const char* getUnitStringByIndex(uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return PSTR("");

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    return (const char*)pgm_read_ptr(&info->symbol);
}

// ===== OBDII CONVERSION FUNCTIONS =====

float obdConvertTemperature(float celsius) {
    return celsius + 40.0;  // OBDII format: A-40
}

float obdConvertPressure(float bar) {
    return bar * 10.0;  // OBDII format: A/10
}

float obdConvertVoltage(float volts) {
    return volts * 10.0;  // OBDII format: A/10
}

float obdConvertDirect(float value) {
    return value;
}

float obdConvertRPM(float rpm) {
    return rpm / 4.0;  // OBDII format: RPM = ((A×256)+B)/4
}

float obdConvertHumidity(float humidity) {
    return humidity * 2.55;  // Convert 0-100% to 0-255
}

float obdConvertElevation(float meters) {
    return meters;
}

float obdConvertFloatSwitch(float value) {
    return value * 255.0;  // OBDII format: 0 or 255
}

// ===== MEASUREMENT TYPE CONVERSION HELPERS =====

ObdConvertFunc getObdConvertFunc(MeasurementType type) {
    switch (type) {
        case MEASURE_TEMPERATURE: return obdConvertTemperature;
        case MEASURE_PRESSURE: return obdConvertPressure;
        case MEASURE_VOLTAGE: return obdConvertVoltage;
        case MEASURE_RPM: return obdConvertRPM;
        case MEASURE_HUMIDITY: return obdConvertHumidity;
        case MEASURE_ELEVATION: return obdConvertElevation;
        case MEASURE_DIGITAL: return obdConvertFloatSwitch;
        default: return obdConvertVoltage;
    }
}