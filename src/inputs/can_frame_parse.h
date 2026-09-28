/*
 * can_frame_parse.h - Identify a received CAN input frame
 *
 * Pure, header-only: decides which cache key (identifier) a frame is stored
 * under and where its payload starts. Kept free of Arduino/HAL deps so the
 * native test build can exercise it directly (test/test_can_frame_parse).
 */

#ifndef CAN_FRAME_PARSE_H
#define CAN_FRAME_PARSE_H

#include <stdint.h>

/**
 * Parse a CAN input frame into (identifier, data offset).
 *
 * Recognised shapes:
 * - ISO-TP single-frame OBD-II Mode 01 response:
 *     [0] = 2 + number of PID data bytes (0x03..0x07 for 1..5 bytes)
 *     [1] = 0x41, [2] = PID, [3+] = data
 *   e.g. 03 41 05 7B (coolant), 04 41 0C 1A F8 (RPM), 06 41 00 BE 3F A8 13
 * - Mode 01 response without the length byte (some ECUs):
 *     [0] = 0x41, [1] = PID, [2+] = data
 * - Anything else (custom protocols): identifier = [0], data from [0].
 *
 * @return false if the frame carries no payload after parsing
 */
static inline bool parseCANInputFrame(const uint8_t* data, uint8_t len,
                                      uint8_t& identifier, uint8_t& dataOffset) {
    if (len == 0) return false;

    if (len >= 3 && data[0] >= 0x03 && data[0] <= 0x07 && data[1] == 0x41) {
        identifier = data[2];
        dataOffset = 3;
    } else if (len >= 3 && data[0] == 0x41) {
        identifier = data[1];
        dataOffset = 2;
    } else {
        identifier = data[0];
        dataOffset = 0;
    }

    return len > dataOffset;
}

#endif // CAN_FRAME_PARSE_H
