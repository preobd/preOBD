/*
 * bme280.cpp - Bosch BME280 Environmental Sensor
 *
 * Implements I2C communication with BME280 combined humidity, pressure,
 * and temperature sensor. Provides temperature, pressure, humidity, and
 * calculated elevation readings.
 *
 * Note: This file includes conditional compilation guards to allow building
 * without BME280 library when not needed.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../../lib/bus_manager.h"
#include "../../input.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"
#include <Wire.h>

#ifdef ENABLE_BME280
#include <Adafruit_BME280.h>

// Shared BME280 object and state (lazy initialization)
static Adafruit_BME280* bme280_ptr = nullptr;
static bool bme280_initialized = false;
static uint8_t bme280_i2c_address = 0x00;  // 0 = not yet detected

// ===== INITIALIZATION =====

/**
 * Initialize BME280 sensor
 *
 * Performs lazy initialization of BME280 via I2C.
 * Auto-detects I2C address (tries 0x76 first, then 0x77).
 *
 * @param ptr  Pointer to Input structure (not used, but required for init signature)
 *
 * @note Only initializes once - subsequent calls return immediately
 * @note If initialization fails, all read functions will return NAN
 */
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
    // Use the active I2C bus from bus_manager
    TwoWire* i2c = getActiveI2C();
    if (bme280_ptr->begin(0x76, i2c)) {
        bme280_initialized = true;
        bme280_i2c_address = 0x76;
    } else if (bme280_ptr->begin(0x77, i2c)) {
        bme280_initialized = true;
        bme280_i2c_address = 0x77;
    }

    if (bme280_initialized) {
        // Show virtual pin number (I2C:0, I2C:1, etc.)
        if (ptr->pin >= 0xF0) {
            msg.debug.info(TAG_SENSOR, "BME280 (0x%02X) initialized on I2C:%d for %s",
                          bme280_i2c_address, ptr->pin - 0xF0, ptr->abbrName);
        } else {
            msg.debug.info(TAG_SENSOR, "BME280 (0x%02X) initialized for %s",
                          bme280_i2c_address, ptr->abbrName);
        }
    } else {
        msg.debug.warn(TAG_SENSOR, "BME280 not found at 0x76 or 0x77");
        msg.debug.warn(TAG_SENSOR, "BME280 sensors will read NAN");
        delete bme280_ptr;
        bme280_ptr = nullptr;
    }
}

// ===== READING FUNCTIONS =====

/**
 * Read BME280 temperature
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 * @return     Temperature in Celsius, or NAN if sensor not initialized
 */
void readBME280Temp(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readTemperature();  // Store in Celsius
    } else {
        ptr->value = NAN;
    }
}

/**
 * Read BME280 pressure
 *
 * @param ptr  Pointer to Input structure to store pressure reading
 * @return     Pressure in bar, or NAN if sensor not initialized
 */
void readBME280Pressure(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readPressure() / 100000.0;  // Store in bar
    } else {
        ptr->value = NAN;
    }
}

/**
 * Read BME280 humidity
 *
 * @param ptr  Pointer to Input structure to store humidity reading
 * @return     Relative humidity in percent (0-100), or NAN if sensor not initialized
 */
void readBME280Humidity(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readHumidity();  // Store as percentage (0-100)
    } else {
        ptr->value = NAN;
    }
}

/**
 * Read BME280 calculated elevation
 *
 * Calculates elevation based on atmospheric pressure.
 *
 * @param ptr  Pointer to Input structure to store elevation reading
 * @return     Elevation in meters, or NAN if sensor not initialized
 */
void readBME280Elevation(Input *ptr) {
    if (bme280_ptr && bme280_initialized) {
        ptr->value = bme280_ptr->readAltitude(SEA_LEVEL_PRESSURE_HPA);  // Store in meters
    } else {
        ptr->value = NAN;
    }
}

#else

// ===== STUB IMPLEMENTATIONS (BME280 DISABLED) =====

void initBME280(Input* ptr) {
    msg.debug.warn(TAG_SENSOR, "BME280 support not compiled in");
}

void readBME280Temp(Input *ptr) { ptr->value = NAN; }
void readBME280Pressure(Input *ptr) { ptr->value = NAN; }
void readBME280Humidity(Input *ptr) { ptr->value = NAN; }
void readBME280Elevation(Input *ptr) { ptr->value = NAN; }

#endif // ENABLE_BME280
