/*
 * can_layout_ids.cpp - see can_layout_ids.h. Pure.
 */

#include "../../config.h"

#if INCLUDE_CAN_LAYOUTS

#include <Arduino.h>
#include "can_layout_ids.h"
#include "../table/output_table.h"   // FMT_PREOBD, FMT_HALTECH

// Haltech's IDs for the channels preOBD has an application for. The rates are
// Haltech's (50/20/10/5 Hz); preOBD sends them all every interval (owner,
// #612 phase 4 decision 1).
const uint16_t HALTECH_FRAME_IDS[HALTECH_FRAMES] PROGMEM = {
    0x360,   // 0  RPM, MAP, TPS                       50 Hz
    0x361,   // 1  fuel pressure, oil pressure         50 Hz
    0x36B,   // 2  brake pressure front                20 Hz
    0x370,   // 3  vehicle speed                       20 Hz
    0x372,   // 4  battery voltage, barometric         10 Hz
    0x373,   // 5  EGT 1-4                             10 Hz
    0x374,   // 6  EGT 5-8                             10 Hz
    0x376,   // 7  ambient air temperature, humidity   10 Hz
    0x3E0,   // 8  coolant, air, oil temperature        5 Hz
    0x3E1,   // 9  gearbox oil, diff oil temperature    5 Hz
    0x6F8,   // 10 fuel level percentage                5 Hz
};

uint8_t layoutFormatFrames(uint8_t format) {
    if (format == FMT_PREOBD) return PREOBD_FRAMES;
    if (format == FMT_HALTECH) return HALTECH_FRAMES;
    return 0;
}

uint16_t layoutFormatFrameId(uint8_t format, uint8_t f) {
    if (format == FMT_HALTECH) return f < HALTECH_FRAMES ? (uint16_t)pgm_read_word(&HALTECH_FRAME_IDS[f]) : 0xFFFF;
    return (uint16_t)(PREOBD_BASE_ID + f);
}

bool layoutFormatOwnsCanId(uint8_t format, uint16_t id) {
    const uint8_t n = layoutFormatFrames(format);
    for (uint8_t f = 0; f < n; f++) {
        if (layoutFormatFrameId(format, f) == id) return true;
    }
    return false;
}

#endif  // INCLUDE_CAN_LAYOUTS
