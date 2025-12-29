/*
 * max6675.cpp - MAX6675 K-Type Thermocouple Reader
 *
 * Implements SPI communication with MAX6675 thermocouple-to-digital converter.
 * Supports K-type thermocouples with 0-1024°C range and 0.25°C resolution.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include <SPI.h>

/**
 * Read MAX6675 thermocouple sensor
 *
 * Reads temperature from MAX6675 via SPI and stores result in Celsius.
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 *
 * Protocol:
 * - 16-bit data transfer (MSB first)
 * - Bit 2 indicates thermocouple connection status (1 = disconnected)
 * - Temperature data in bits 14-3 (12 bits, 0.25°C resolution)
 *
 * @note Minimum 220ms between readings for temperature conversion
 * @note Returns NAN if thermocouple is disconnected
 */
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
