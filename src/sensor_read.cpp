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

// ===== LEGACY THERMISTOR FUNCTIONS (Backward Compatibility) =====
// These now just call the generic functions with hardcoded calibrations

void readVDO120(Sensor *ptr) {
    // This is now just a wrapper - deprecated but kept for compatibility
    // Users should use readThermistorLookup with VDO_120C_LOOKUP instead
    readThermistorLookup(ptr);
}

void readVDO150(Sensor *ptr) {
    // This is now just a wrapper - deprecated but kept for compatibility
    // Users should use readThermistorLookup with VDO_150C_LOOKUP instead
    readThermistorLookup(ptr);
}

void readSteinhart(Sensor *ptr) {
    // Deprecated - use readThermistorSteinhart instead
    readThermistorSteinhart(ptr);
}

// ===== PRESSURE SENSOR READING =====

void readVDO5BAR(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // VDO 5 bar sensor polynomial: y = -0.3682x² + 36.465x + 10.648
    float pressure = (-36.465 - sqrt(-1.4728 * reading + 1345.37859)) / 0.7364;
    
    ptr->value = pressure;  // Store in BAR
}

void readVDO2BAR(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    // VDO 2 bar sensor polynomial: y = -3.1515x² + 93.686x + 9.6307
    float pressure = (-93.686 - sqrt(-12.606 * reading + 8898.47120)) / 6.303;
    
    ptr->value = pressure;  // Store in BAR
}

void readGenericBoost(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    float voltage = reading * (SYSTEM_VOLTAGE / (float)ADC_MAX_VALUE);
    
    if (voltage < 0.5) {
        ptr->value = 0;
    } else {
        ptr->value = (voltage - 0.5) * (2.0 / (SYSTEM_VOLTAGE - 0.5));
    }
}

void readMPX4250AP(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3) || reading <= 3) {
        ptr->value = NAN;
        return;
    }
    
    float voltage = reading * (SYSTEM_VOLTAGE / (float)ADC_MAX_VALUE);
    float kPa = ((voltage - 0.2) / (SYSTEM_VOLTAGE - 0.2)) * 230.0 + 20.0;
    ptr->value = kPa / 100.0;  // Convert to bar
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