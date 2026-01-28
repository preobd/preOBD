/*
 * thermocouple_common.cpp - Shared Thermocouple Initialization
 *
 * Provides common initialization function for SPI-based thermocouples.
 * Used by both MAX6675 and MAX31855 sensors.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"
#include <SPI.h>

/**
 * Initialize thermocouple chip select pin
 *
 * Sets up the SPI chip select (CS) pin for thermocouple sensors.
 * The CS pin is set HIGH (idle state) to deselect the sensor.
 *
 * @param ptr  Pointer to Input structure containing pin configuration
 *
 * @note This function is shared by both MAX6675 and MAX31855 sensors.
 */
void initThermocoupleCS(Input* ptr) {
    pinMode(ptr->pin, OUTPUT);
    digitalWrite(ptr->pin, HIGH);  // CS idle state is HIGH
    msg.debug.info(TAG_SENSOR, "Thermocouple CS pin %d for %s", ptr->pin, ptr->abbrName);
}
