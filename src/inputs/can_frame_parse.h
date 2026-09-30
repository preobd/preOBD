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

/**
 * On an OBD-II ECU response ID (0x7E8-0x7EF), is this frame something other
 * than a value? A negative response (03 7F 01 31: "PID not supported") and
 * the ISO-TP multi-frame PCIs (1x first frame, 2x consecutive, 3x flow
 * control) fall through parseCANInputFrame to the custom path, keyed by
 * their first byte: an NRC was cached as PID 0x03 and a first frame as PID
 * 0x10 (MAF), and a scan listed both. Supported-PID discovery (#511) makes
 * both routine: a functional request for a block an ECU lacks draws an NRC.
 * The caller drops such frames; it applies this only to ECU response IDs,
 * so a custom device whose first byte is 0x10-0x3F is unaffected.
 */
static inline bool isObdNonValueFrame(const uint8_t* data, uint8_t len) {
    if (len == 0) return false;
    uint8_t pci = data[0] & 0xF0;
    if (pci == 0x10 || pci == 0x20 || pci == 0x30) return true;
    return len >= 2 && data[0] >= 0x01 && data[0] <= 0x07 && data[1] == 0x7F;
}

#endif // CAN_FRAME_PARSE_H
