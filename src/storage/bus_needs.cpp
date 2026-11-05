/*
 * bus_needs.cpp - Which buses does the stored configuration need?
 *
 * Only the display's vote lives here; the per-sensor mapping is inline in
 * sensor_library/sensor_helpers.h (see bus_needs.h for why).
 */

#include "bus_needs.h"
#include "system_config.h"

uint8_t busNeedsForDisplayType(uint8_t displayType) {
    // Both panels are I2C: the PCF8574-backpack LCD and the SH1107 OLED.
    // Whether this image can drive the type is the caller's question (main.cpp
    // only counts the vote when the driver is built in).
    return (displayType == DISPLAY_LCD || displayType == DISPLAY_OLED) ? BUS_NEED_I2C : BUS_NEED_NONE;
}

uint8_t busNeedsForSdPin(uint8_t sdPin) {
    return sdPin == 254 ? BUS_NEED_NONE : BUS_NEED_SPI;   // 254 = BUILTIN_SDCARD (SDIO)
}
