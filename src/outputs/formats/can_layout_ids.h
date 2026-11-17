/*
 * can_layout_ids.h - The CAN IDs each layout format sends (#612 phases 3a, 4)
 *
 * One source for the IDs: the layouts send on them (layout_*.cpp) and
 * admission refuses two enabled outputs on one bus that share one
 * (output_admission.cpp). Pure: no globals beyond the PROGMEM ID table.
 */

#ifndef CAN_LAYOUT_IDS_H
#define CAN_LAYOUT_IDS_H

#include <stdint.h>

// PREOBD owns CAN IDs [PREOBD_BASE_ID, PREOBD_BASE_ID + PREOBD_FRAMES): fixed, 11-bit
#define PREOBD_BASE_ID 0x500
#define PREOBD_FRAMES  9

// HALTECH (Haltech ECU Broadcast CAN Protocol v2.0): the frames preOBD can
// fill, in the order of HALTECH_FRAME_IDS (layout_haltech.cpp's frame index)
#define HALTECH_FRAMES 11
extern const uint16_t HALTECH_FRAME_IDS[HALTECH_FRAMES];   // PROGMEM

// A layout format's frame count (0 for a format that isn't a layout) and frame f's ID
uint8_t  layoutFormatFrames(uint8_t format);
uint16_t layoutFormatFrameId(uint8_t format, uint8_t f);
// Does layout format `format` send on CAN ID `id`?
bool     layoutFormatOwnsCanId(uint8_t format, uint16_t id);

#endif // CAN_LAYOUT_IDS_H
