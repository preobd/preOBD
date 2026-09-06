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
 *
 * The masks are OR-ed and handed to acquireRequiredBuses() in ONE pass before
 * any client initializes, so pin reservations are in place before the record
 * loop validates a single stored pin and before initLCD() runs.
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

#endif // BUS_NEEDS_H
