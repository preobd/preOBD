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
#include "../../../core/platform.h"
#include "../../../drivers/bus_manager.h"
#include "../../../inputs/input.h"
#include "../../../core/message_api.h"
#include "../../../core/log_tags.h"
#include "../../../storage/system_config.h"   // seaLevelPressureHpa() (SYSTEM SEA_LEVEL)
#include <Wire.h>

#if INCLUDE_BME280
#include <Adafruit_BME280.h>

// Shared BME280 object and state (lazy initialization)
static Adafruit_BME280* bme280_ptr = nullptr;
static bool bme280_initialized = false;
static uint8_t bme280_i2c_address = 0x00;  // 0 = not yet detected

// Recovery after the chip stops answering or loses power (#589). A NAN read
// from a chip that still answers usually means it was power-cycled into
// sleep mode; its settings are re-applied then, at most once per
// BME280_SETUP_MS so the measurement they start has time to finish.
static const uint32_t BME280_SETUP_MS = 1000;
static bool bme280_lost = false;          // stopped answering
static bool bme280_needs_setup = false;   // re-apply settings at the next read
static uint32_t bme280_last_setup = 0;

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
    if (!i2c) {
        msg.debug.error(TAG_SENSOR, "I2C not configured — run 'BUS I2C 0' then SAVE and reboot");
        delete bme280_ptr;
        bme280_ptr = nullptr;
        return;
    }
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

// Whether a read can trust the chip right now. The Adafruit library never
// checks its I2C transfers: a missing chip's reads compute from leftover
// buffer bytes into plausible wrong values (RH 100 %, baro -0.2 bar, #589).
// So probe the address first (zero bytes: fast, and harmless when NACKed).
//
// Recovery re-applies the settings with setSampling() (the defaults begin()
// uses: four register writes) rather than calling begin() again. begin()
// would re-run Wire.begin() (resetting the shared bus clock), block ~120 ms,
// and can spin forever in its calibration-wait loop if the chip drops
// mid-init. The calibration it reads lives in the chip's own NVM, so it
// still holds for the same chip.
static bool bme280Ready() {
    if (!bme280_ptr || !bme280_initialized) return false;
    TwoWire* i2c = getActiveI2C();
    if (!i2c) return false;
    i2c->beginTransmission(bme280_i2c_address);
    if (i2c->endTransmission() != 0) {
        if (!bme280_lost) {
            bme280_lost = true;
#if !defined(__AVR__)   // msg.debug format strings sit in RAM on AVR
            msg.debug.warn(TAG_SENSOR, "BME280 not answering, reading NAN");
#endif
        }
        bme280_needs_setup = true;
        return false;
    }
    if (bme280_needs_setup && (bme280_lost || (uint32_t)(millis() - bme280_last_setup) >= BME280_SETUP_MS)) {
        bme280_ptr->setSampling();
        bme280_last_setup = millis();
        bme280_needs_setup = false;
        if (bme280_lost) {
            bme280_lost = false;
#if !defined(__AVR__)
            msg.debug.info(TAG_SENSOR, "BME280 back");
#endif
        }
    }
    return !bme280_lost;
}

// A NAN from a chip that answered: it may have been power-cycled between two
// reads (sleep mode, registers at their reset values). Set it up again.
static float bme280Checked(float v) {
    if (isnan(v)) bme280_needs_setup = true;
    return v;
}

// ===== READING FUNCTIONS =====

/**
 * Read BME280 temperature
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 * @return     Temperature in Celsius, or NAN if sensor not initialized
 */
void readBME280Temp(Input *ptr) {
    if (bme280Ready()) {
        ptr->value = bme280Checked(bme280_ptr->readTemperature());  // Store in Celsius
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
    if (bme280Ready()) {
        ptr->value = bme280Checked(bme280_ptr->readPressure()) / 100000.0;  // Store in bar
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
    if (bme280Ready()) {
        ptr->value = bme280Checked(bme280_ptr->readHumidity());  // Store as percentage (0-100)
    } else {
        ptr->value = NAN;
    }
}

/**
 * Read BME280 calculated elevation
 *
 * Calculates elevation based on atmospheric pressure, against the
 * SYSTEM SEA_LEVEL reference.
 *
 * @param ptr  Pointer to Input structure to store elevation reading
 * @return     Elevation in meters, or NAN if sensor not initialized
 */
void readBME280Elevation(Input *ptr) {
    if (bme280Ready()) {
        ptr->value = bme280Checked(bme280_ptr->readAltitude(seaLevelPressureHpa()));  // Store in meters
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

#endif // INCLUDE_BME280
