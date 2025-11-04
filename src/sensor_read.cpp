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

float interpolate(float X, byte size, float* x, float* y) {
    // Handle edge cases
    if (X <= x[0]) return y[0];
    if (X >= x[size-1]) return y[size-1];
    
    // Find the right segment
    for (int i = 0; i < size-1; i++) {
        if (X >= x[i] && X <= x[i+1]) {
            // Linear interpolation: y = y1 + (x-x1)/(x2-x1) * (y2-y1)
            return y[i] + ((X - x[i]) / (x[i+1] - x[i])) * (y[i+1] - y[i]);
        }
    }
    return NAN;
}

// ===== THERMOCOUPLE READING =====

void readMAX6675(Sensor *ptr) {
    digitalWrite(ptr->input, LOW);
    delay(1); // Allow chip select to settle
    
    uint16_t value = SPI.transfer(0x00);
    value <<= 8;
    value |= SPI.transfer(0x00);
    
    digitalWrite(ptr->input, HIGH);
    
    if (value & 0x4) {
        // No thermocouple attached
        ptr->value = NAN;
    } else {
        value >>= 3;
        ptr->value = value * 0.25;  // Store in Celsius
    }
}

void readMAX31855(Sensor *ptr) {
    uint32_t d = 0;
    uint8_t buf[4];
    
    digitalWrite(ptr->input, LOW);
    delay(1);
    
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
        // Negative value
        d = 0xFFFFC000 | ((d >> 18) & 0x00003FFF);
    } else {
        d >>= 18;
    }
    
    float temp = d;
    ptr->value = temp * 0.25;  // Store in Celsius
}

// ===== THERMISTOR READING =====

void readVDO120(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    delay(10);
    reading = analogRead(ptr->input);  // Discard first reading
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
        ptr->value = NAN;
        return;
    }
    
    // Calculate thermistor resistance
    float R2 = 2200.0;  // Bias resistor
    float R1 = R2 * (1023.0 / reading - 1.0);
    
    // VDO120 lookup table (resistance in Ω, temperature in °C)
    const byte size = 39;
    float ohms[] = {17162.35,12439.5,9134.53,6764.48,5087.6,3833.89,2929.9,2249.44,
                    1743.15,1364.07,1075.63,850.09,676.95,543.54,439.29,356.64,291.46,
                    239.56,197.29,161.46,134.03,113.96,97.05,82.36,70.12,59.73,51.21,
                    44.32,38.47,33.4,29.12,25.53,22.44,19.75,17.44,15.46,13.75,12.26,10.96};
    float temps[] = {-40,-35,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,35,40,45,50,55,
                     60,65,70,75,80,85,90,95,100,105,110,115,120,125,130,135,140,145,150};
    
    ptr->value = interpolate(R1, size, ohms, temps);  // Store in Celsius
}

void readVDO150(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    delay(10);
    reading = analogRead(ptr->input);  // Discard first reading
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
        ptr->value = NAN;
        return;
    }
    
    // Calculate thermistor resistance
    float R2 = 2200.0;
    float R1 = R2 * ((float)ADC_MAX_VALUE / reading - 1.0);
    
    // VDO150 lookup table (resistance in Ω, temperature in °C)
    const byte size = 45;
    float ohms[] = {36563.56,26284.63,19149.20,14127.68,10540.68,7721.35,5720.88,4284.03,
                    3240.18,2473.60,1905.87,1486.65,1168.64,926.71,739.98,594.90,481.53,
                    392.57,322.17,266.19,221.17,184.72,155.29,131.38,112.08,96.40,82.96,
                    71.44,61.92,54.01,47.24,41.42,36.51,32.38,28.81,25.70,23.0,20.66,18.59,
                    16.74,15.11,13.66,12.38,11.25,10.24};
    float temps[] = {-40,-35,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,35,40,45,50,55,60,
                     65,70,75,80,85,90,95,100,105,110,115,120,125,130,135,140,145,150,155,
                     160,165,170,175,180};
    
    ptr->value = interpolate(R1, size, ohms, temps);  // Store in Celsius
}

void STEINHART(Sensor *ptr) {
    /* Use full Steinhart-Hart model.  B model wasn't as accurate.
        0/45/150	25/45/150
    A	1.597491234	1.590025176
    B	2.63014794	2.65980073
    C	-1.184237497	-1.665059362
    float steinhart;                              	//steinhart equation to estimate temperature value at any resistance from curve of thermistor sensor
    steinhart = log(resistance);                  	//lnR
    steinhart = pow(steinhart,3);                 	//(lnR)^3
    steinhart *= steinconstC;                     	//C*((lnR)^3)
    steinhart += (steinconstB*(log(resistance))); 	//B*(lnR) + C*((lnR)^3)
    steinhart += steinconstA;                     	//Complete equation, 1/T=A+BlnR+C(lnR)^3
    steinhart = 1.0/steinhart;                    	//Inverse to isolate for T
    steinhart -= 273.15;                          	//Conversion from kelvin to celcius
    */
}

// ===== PRESSURE SENSOR READING =====

void readVDO5BAR(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
        ptr->value = NAN;
        return;
    }
    
    // VDO 5 bar sensor polynomial: y = -0.3682x² + 36.465x + 10.648
    float pressure = (-36.465 - sqrt(-1.4728 * reading + 1345.37859)) / 0.7364;
    
    ptr->value = pressure;  // Store in BAR
}

void readVDO2BAR(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
        ptr->value = NAN;
        return;
    }
    
    // VDO 2 bar sensor polynomial: y = -3.1515x² + 93.686x + 9.6307
    float pressure = (-93.686 - sqrt(-12.606 * reading + 8898.47120)) / 6.303;
    
    ptr->value = pressure;  // Store in BAR
}

void readGenericBoost(Sensor *ptr) {
    int reading = analogRead(ptr->input);
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
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
    
    if (reading >= (ADC_MAX_VALUE - 3)) {
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
    ptr->value = bme.readPressure() / 1000.0;  // Store in kPa
    #else
    ptr->value = NAN;
    #endif
}

// ===== CONVERSION FUNCTIONS =====

// Display conversion - converts from storage units to display units
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
    }
    return bar;
}

float convertVoltage(float volts, DisplayUnits units) {
    return volts;
}

// OBDII conversion functions
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
    return value;  // Direct value (like MAX6675/MAX31855)
}
