/*
 * sensor_read.cpp - All sensor reading implementations
 * Works with both EEPROM config and compile-time config
 */

#include "../config.h"
#include "../lib/platform.h"
#include "input.h"
#include "../lib/sensor_types.h"
#include "../lib/sensor_library.h"
#include <SPI.h>

#ifdef USE_BME280
#include <Adafruit_BME280.h>
extern Adafruit_BME280 bme;
#endif

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
#ifdef USE_INPUT_BASED_ARCHITECTURE
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        R_bias = ptr->customCalibration.steinhart.bias_resistor;
        A = ptr->customCalibration.steinhart.steinhart_a;
        B = ptr->customCalibration.steinhart.steinhart_b;
        C = ptr->customCalibration.steinhart.steinhart_c;
    } else
#endif
    if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_THERMISTOR_STEINHART) {
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

// ===== GENERIC PRESSURE SENSOR - LINEAR METHOD =====

void readPressureLinear(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration values (from custom RAM or PROGMEM preset)
    float V_min, V_max, P_min, P_max;
#ifdef USE_INPUT_BASED_ARCHITECTURE
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_PRESSURE_LINEAR) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        V_min = ptr->customCalibration.pressureLinear.voltage_min;
        V_max = ptr->customCalibration.pressureLinear.voltage_max;
        P_min = ptr->customCalibration.pressureLinear.pressure_min;
        P_max = ptr->customCalibration.pressureLinear.pressure_max;
    } else
#endif
    if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_PRESSURE_LINEAR) {
        // Read from preset calibration (PROGMEM)
        const LinearCalibration* cal = (const LinearCalibration*)ptr->presetCalibration;
        V_min = pgm_read_float(&cal->voltage_min);
        V_max = pgm_read_float(&cal->voltage_max);
        P_min = pgm_read_float(&cal->pressure_min);
        P_max = pgm_read_float(&cal->pressure_max);
    } else {
        // Default: 0.5V-4.5V → 0-5 bar (common automotive sensor)
        V_min = 0.5;
        V_max = 4.5;
        P_min = 0.0;
        P_max = 5.0;
    }
    
    // Convert ADC reading to voltage
    float voltage = reading * (AREF_VOLTAGE / (float)ADC_MAX_VALUE);
    
    // Clamp voltage to valid range
    if (voltage < V_min) voltage = V_min;
    if (voltage > V_max) voltage = V_max;
    
    // Linear interpolation: P = (V - V_min) / (V_max - V_min) * (P_max - P_min) + P_min
    float pressure = ((voltage - V_min) / (V_max - V_min)) * (P_max - P_min) + P_min;
    
    ptr->value = pressure;  // Store in bar
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
#ifdef USE_INPUT_BASED_ARCHITECTURE
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        bias_resistor = ptr->customCalibration.pressurePolynomial.bias_resistor;
        a = ptr->customCalibration.pressurePolynomial.poly_a;
        b = ptr->customCalibration.pressurePolynomial.poly_b;
        c = ptr->customCalibration.pressurePolynomial.poly_c;
    } else
#endif
    if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
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
#ifdef USE_INPUT_BASED_ARCHITECTURE
    if (ptr->flags.useCustomCalibration && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
        // Read from custom calibration (RAM) - only available in EEPROM/serial config mode
        r1 = ptr->customCalibration.voltageDivider.r1;
        r2 = ptr->customCalibration.voltageDivider.r2;
        correction = ptr->customCalibration.voltageDivider.correction;
        offset = ptr->customCalibration.voltageDivider.offset;
    } else
#endif
    if (ptr->presetCalibration != nullptr && ptr->calibrationType == CAL_VOLTAGE_DIVIDER) {
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
    // Note: RPM calibration not yet in union, using defaults
    // TODO: Add RPM calibration to Input union
    float pulses_per_rev = 3.0;  // Default for 12-pole alternator
    uint16_t timeout_ms = 2000;  // 2 second timeout
    uint16_t min_rpm = 100;
    uint16_t max_rpm = 10000;

    // Calculate time since last pulse
    unsigned long now = millis();
    unsigned long time_since_pulse = now - (rpm_last_time / 1000);

    // Check for timeout (engine stopped)
    if (time_since_pulse > timeout_ms) {
        ptr->value = 0;
        return;
    }

    // Calculate RPM from pulse interval
    // Formula: RPM = (60,000,000 µs/min) / (interval_µs × pulses_per_rev)
    if (rpm_pulse_interval > 0) {
        float rpm = 60000000.0 / (rpm_pulse_interval * pulses_per_rev);

        // Validate range
        if (rpm >= min_rpm && rpm <= max_rpm) {
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

// ===== BME280 READING =====

void readBME280Temp(Input *ptr) {
    #ifdef USE_BME280
    ptr->value = bme.readTemperature();  // Store in Celsius
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Pressure(Input *ptr) {
    #ifdef USE_BME280
    ptr->value = bme.readPressure() / 100000.0;  // Store in bar
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Humidity(Input *ptr) {
    #ifdef USE_BME280
    ptr->value = bme.readHumidity();  // Store as percentage (0-100)
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Elevation(Input *ptr) {
    #ifdef USE_BME280
    ptr->value = bme.readAltitude(SEA_LEVEL_PRESSURE_HPA);  // Store in meters
    #else
    ptr->value = NAN;
    #endif
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

float convertTemperature(float celsius, DisplayUnits units) {
    if (units == FAHRENHEIT) {
        return celsius * 9.0/5.0 + 32.0;
    }
    return celsius;
}

float convertPressure(float bar, DisplayUnits units) {
    if (units == PSI) {
        return bar * 14.5038;
    } else if (units == KPA) {
        return bar * 100.0;
    } else if (units == INHG) {
        return bar * 29.53;
    }
    return bar;
}

float convertVoltage(float volts, DisplayUnits units) {
    return volts;
}

float convertRPM(float rpm, DisplayUnits units) {
    return rpm;  // RPM is always displayed as RPM
}

float convertHumidity(float humidity, DisplayUnits units) {
    return humidity;
}

float convertElevation(float meters, DisplayUnits units) {
    if (units == FEET) {
        return meters * 3.28084;
    }
    return meters;
}

float convertFloatSwitch(float value, DisplayUnits units) {
    if (units == PERCENT) {
        return value * 100.0;  // 0% (low) or 100% (ok)
    }
    return value;  // 0 or 1
}

// ===== UNIT STRING CONVERSION =====

const char* getUnitString(DisplayUnits units) {
    switch (units) {
        case CELSIUS: return "C";
        case FAHRENHEIT: return "F";
        case BAR: return "bar";
        case PSI: return "psi";
        case KPA: return "kPa";
        case INHG: return "inHg";
        case VOLTS: return "V";
        case PERCENT: return "%";
        case METERS: return "m";
        case FEET: return "ft";
        default: return "";
    }
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

DisplayConvertFunc getDisplayConvertFunc(MeasurementType type) {
    switch (type) {
        case MEASURE_TEMPERATURE: return convertTemperature;
        case MEASURE_PRESSURE: return convertPressure;
        case MEASURE_VOLTAGE: return convertVoltage;
        case MEASURE_RPM: return convertRPM;
        case MEASURE_HUMIDITY: return convertHumidity;
        case MEASURE_ELEVATION: return convertElevation;
        case MEASURE_DIGITAL: return convertFloatSwitch;
        default: return convertVoltage;
    }
}

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