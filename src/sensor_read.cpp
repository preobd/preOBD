/*
 * sensor_read.cpp - All sensor reading implementations
 */

#include "sensor_types.h"
#include "config.h"
#include "platform.h"
#include <SPI.h>

#ifdef ENABLE_AMBIENT_TEMP
#include <Adafruit_BME280.h>
extern Adafruit_BME280 bme;
#endif

// ===== UTILITY FUNCTIONS =====

float interpolate(float X, byte size, const float* x, const float* y) {
    // Handle edge cases
    if (X >= x[0]) return y[0];
    if (X <= x[size-1]) return y[size-1];

    // Find the right segment
    for (int i = size-1; i >= 0; i--) {
        if (X >= x[i] && X <= x[i-1]) {
            // Linear interpolation: y = y1 + ((x – x1) / (x2 – x1)) * (y2 – y1)
            return y[i] + ((X - x[i]) / (x[i+1] - x[i])) * (y[i+1] - y[i]);
        }
    }
    return NAN;
}

// ===== THERMOCOUPLE READING =====

void readMAX6675(Sensor *ptr) {
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ptr->input, LOW);
    delayMicroseconds(1);
    
    uint16_t value = SPI.transfer(0x00);
    value <<= 8;
    value |= SPI.transfer(0x00);
    
    digitalWrite(ptr->input, HIGH);
    SPI.endTransaction();

    if (value & 0x4) {
        ptr->value = NAN;  // No thermocouple attached
    } else {
        value >>= 3;
        ptr->value = value * 0.25;  // Store in Celsius
    }
}

void readMAX31855(Sensor *ptr) {
    uint32_t d = 0;
    uint8_t buf[4];
    
    digitalWrite(ptr->input, LOW);
    delayMicroseconds(1);
    
    for (int i = 0; i < 4; i++) {
        buf[i] = SPI.transfer(0x00);
    }
    
    digitalWrite(ptr->input, HIGH);
    
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
    
    // Extract temperature (top 14 bits)
    if (d & 0x80000000) {
        d = 0xFFFFC000 | ((d >> 18) & 0x00003FFF);
    } else {
        d >>= 18;
    }
    
    float temp = d;
    ptr->value = temp * 0.25;  // Store in Celsius
}

// ===== GENERIC THERMISTOR - STEINHART-HART METHOD =====

void readThermistorSteinhart(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    delay(10);
    reading = analogRead(ptr->input);  // Discard first reading
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // Get calibration (with defaults if not provided)
    ThermistorSteinhartCalibration* cal = getThermistorSteinhartCal(ptr);
    
    float R_bias = (cal != nullptr) ? cal->bias_resistor : 10000.0;
    float A = (cal != nullptr) ? cal->steinhart_a : 1.129241e-3;
    float B = (cal != nullptr) ? cal->steinhart_b : 2.341077e-4;
    float C = (cal != nullptr) ? cal->steinhart_c : 8.775468e-8;
    
    // Calculate thermistor resistance
    float R_thermistor = reading * R_bias / (ADC_MAX_VALUE - reading);
    
    if (R_thermistor <= 0) {
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

void readThermistorLookup(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    delay(10);
    reading = analogRead(ptr->input);  // Discard first reading
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // Get calibration (REQUIRED for lookup method)
    ThermistorLookupCalibration* cal = getThermistorLookupCal(ptr);
    
    if (cal == nullptr) {
        ptr->value = NAN;  // Can't do lookup without table
        return;
    }
    
    // Calculate thermistor resistance
    float R_thermistor = reading * cal->bias_resistor / (ADC_MAX_VALUE - reading);
    
    if (R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }
    
    // Interpolate temperature from lookup table
    ptr->value = interpolate(R_thermistor, cal->table_size,
                            cal->resistance_table, cal->temperature_table);
}

// ===== GENERIC PRESSURE SENSOR - LINEAR METHOD =====

void readPressureLinear(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // Get calibration (with defaults if not provided)
    PressureLinearCalibration* cal = getPressureLinearCal(ptr);
    
    // Default: 0.5V-4.5V → 0-5 bar (common automotive sensor)
    float V_min = (cal != nullptr) ? cal->voltage_min : 0.5;
    float V_max = (cal != nullptr) ? cal->voltage_max : 4.5;
    float P_min = (cal != nullptr) ? cal->pressure_min : 0.0;
    float P_max = (cal != nullptr) ? cal->pressure_max : 5.0;
    
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

void readPressurePolynomial(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // Get calibration (REQUIRED for polynomial method)
    PressurePolynomialCalibration* cal = getPressurePolynomialCal(ptr);
    
    if (cal == nullptr) {
        ptr->value = NAN;  // Can't calculate without coefficients
        return;
    }
    
    // VDO sensors use quadratic equation: V = A*P² + B*P + C
    // We need to solve for P: A*P² + B*P + (C - V) = 0
    // Using quadratic formula: P = (-B ± sqrt(B² - 4*A*(C-V))) / (2*A)
    
    float voltage = reading * (AREF_VOLTAGE / (float)ADC_MAX_VALUE);
    
    float a = cal->poly_a;
    float b = cal->poly_b;
    float c = cal->poly_c - voltage;
    
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

void readVoltageDivider(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading < 10) {
        ptr->value = NAN;
        return;
    }
    
    ptr->value = reading * VOLTAGE_DIVIDER_RATIO * AREF_VOLTAGE / (float)ADC_MAX_VALUE;
}

// ===== BME280 READING =====

void readBME280Temp(Sensor *ptr) {
    #ifdef ENABLE_AMBIENT_TEMP
    ptr->value = bme.readTemperature();  // Store in Celsius
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Pressure(Sensor *ptr) {
    #ifdef ENABLE_BAROMETRIC_PRESSURE
    ptr->value = bme.readPressure() / 100000.0;  // Store in bar
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Humidity(Sensor *ptr) {
    #if defined(ENABLE_HUMIDITY)
    ptr->value = bme.readHumidity();  // Store as percentage (0-100)
    #else
    ptr->value = NAN;
    #endif
}

void readBME280Altitude(Sensor *ptr) {
    #if defined(ENABLE_ALTITUDE)
    ptr->value = bme.readAltitude(SEA_LEVEL_PRESSURE_HPA);  // Store in meters
    #else
    ptr->value = NAN;
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

float convertHumidity(float humidity, DisplayUnits units) {
    return humidity;
}

float convertAltitude(float meters, DisplayUnits units) {
    if (units == FEET) {
        return meters * 3.28084;
    }
    return meters;
}

// ===== OBDII CONVERSION FUNCTIONS =====

float obdConvertTemp(float celsius) {
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

float obdConvertHumidity(float humidity) {
    return humidity * 2.55;  // Convert 0-100% to 0-255
}

float obdConvertAltitude(float meters) {
    return meters;
}