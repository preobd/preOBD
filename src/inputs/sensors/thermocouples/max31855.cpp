/*
 * max31855.cpp - MAX31855 K-Type Thermocouple Reader
 *
 * Implements SPI communication with MAX31855 thermocouple-to-digital converter.
 * Supports K-type thermocouples with -200 to 1350°C range and 0.25°C resolution.
 * Improved over MAX6675 with extended range and internal cold junction compensation.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../../lib/bus_manager.h"
#include "../../input.h"
#include <SPI.h>

/**
 * Read MAX31855 thermocouple sensor
 *
 * Reads temperature from MAX31855 via SPI and stores result in Celsius.
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 *
 * Protocol:
 * - 32-bit data transfer (MSB first)
 * - Bits 2-0 indicate fault conditions
 * - Temperature data in bits 31-18 (14 bits, signed, 0.25°C resolution)
 *
 * @note Returns NAN if any fault is detected (thermocouple short/open, etc.)
 */
void readMAX31855(Input *ptr) {
    uint32_t d = 0;
    uint8_t buf[4];

    SPIClass* spi = getActiveSPI();
    spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ptr->pin, LOW);
    delayMicroseconds(1);

    for (int i = 0; i < 4; i++) {
        buf[i] = spi->transfer(0x00);
    }

    digitalWrite(ptr->pin, HIGH);
    spi->endTransaction();

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
