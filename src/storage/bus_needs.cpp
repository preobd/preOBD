/*
 * bus_needs.cpp - Which buses does the stored configuration need?
 *
 * Only the display's vote lives here; the per-sensor mapping is inline in
 * sensor_library/sensor_helpers.h (see bus_needs.h for why).
 */

#include "bus_needs.h"
#include "system_config.h"

uint8_t busNeedsForDisplayType(uint8_t displayType) {
    // Only the PCF8574-backpack LCD is I2C today. DISPLAY_OLED has no driver in
    // the image yet; it gets its vote when it does.
    return (displayType == DISPLAY_LCD) ? BUS_NEED_I2C : BUS_NEED_NONE;
}
