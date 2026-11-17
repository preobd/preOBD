/*
 * bus_needs.h - Which buses does the stored CONFIGURATION need?
 *
 * Bus bring-up is driven by what needs a bus, declared by the configuration,
 * not discovered by whichever client happens to initialize first (#358). The
 * voters are:
 *
 *   - every stored input record, via its sensor's declared requirement
 *     (see busNeedsForSensorIndex);
 *   - the display type — one more voter with no special status. The LCD is an
 *     add-on peripheral; it must not be the thing that decides whether the
 *     board's I2C bus exists.
 *   - an SD card on an external chip-select (#381). Unlike the other two this
 *     is WIRING (SD_PIN, compile time), not stored configuration, but it votes
 *     the same way: the card shares MOSI/MISO/SCK with every other SPI device,
 *     so the bus manager has to own them before the SD library starts clocking.
 *
 * The masks are OR-ed and handed to acquireRequiredBuses() in ONE pass before
 * any client initializes, so pin reservations are in place before the record
 * loop validates a single stored pin and before initLCD() runs.
 *
 * A vote here is a REQUEST, not a compulsion (#377): acquireRequiredBuses()
 * brings a bus up only if the stored CONFIG names it or a build-time WIRING
 * declaration does (see bus_wiring.h). A need with neither behind it comes
 * back unmet — main.cpp's boot diagnosis reports it rather than the old
 * behavior of silently granting it from a DEFAULT_*_BUS guess.
 */

#ifndef BUS_NEEDS_H
#define BUS_NEEDS_H

#include <Arduino.h>

enum : uint8_t {
    BUS_NEED_NONE = 0,
    BUS_NEED_I2C  = 1 << 0,
    BUS_NEED_SPI  = 1 << 1,
};

// The per-sensor mapping — busNeedsForSensorIndex() / busNeedsForSensorHash() —
// lives in sensor_library/sensor_helpers.h, next to getSensorCategory() and the
// rest of the facts derived from a SensorInfo. It is deliberately NOT here: this
// header is included by main.cpp and bus_manager.cpp, and SENSOR_LIBRARY is a
// PROGMEM table defined in a header, so pulling it into one more translation
// unit costs another full copy of the sensor/application/units registries
// (~8.8 KB of flash, measured on mega2560).

/** The display's vote: an I2C-attached LCD needs I2C, everything else nothing. */
uint8_t busNeedsForDisplayType(uint8_t displayType);

/**
 * The SD card's vote, from the board's SD_PIN: a card on an external
 * chip-select is an SPI device; the built-in slot (BUILTIN_SDCARD, 254) is
 * SDIO on every board that has one and needs no bus.
 */
uint8_t busNeedsForSdPin(uint8_t sdPin);

#endif // BUS_NEEDS_H
