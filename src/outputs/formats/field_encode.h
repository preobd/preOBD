/*
 * field_encode.h - Pure scaled-integer field encoder (#612)
 *
 * The round / clamp / pack core shared by the OBD-II answer (obd_encode.cpp)
 * and the CAN layouts. No globals, no Arduino, host-testable.
 *
 * "N/A" (no data) pattern, fieldAllOnes():
 *   unsigned  all bits set (0xFF, 0xFFFF, 0xFFFFFFFF)
 *   signed    the MAXIMUM POSITIVE value (0x7F, 0x7FFF, 0x7FFFFFFF), because
 *             all-ones is -1 in two's complement, a perfectly real reading.
 * With reserveAllOnes a finite value never encodes to that pattern: it
 * clamps one step lower (max-1, unsigned and signed alike).
 */

#ifndef FIELD_ENCODE_H
#define FIELD_ENCODE_H

#include <stdint.h>

#define FIELD_SIGNED         0x01   // two's complement
#define FIELD_LITTLE_ENDIAN  0x02   // least significant byte first (default: big-endian)

/**
 * out[0..length) = round((value - offset) / scale), clamped to the field.
 * length is 1, 2 or 4 (anything else returns 0). Rounds half up.
 * Unsigned clamps to [0, max]; signed to [min, max]; reserveAllOnes lowers
 * max by one so the N/A pattern is never produced for a real value.
 * Returns length, or 0 (out untouched) for a non-finite value or bad length;
 * the caller decides whether that means "write N/A".
 */
uint8_t encodeScaledField(float value, float scale, float offset, uint8_t length,
                          uint8_t flags, bool reserveAllOnes, uint8_t* out);

/** Write the N/A pattern (see above) for a field of `length` (1, 2, 4) bytes. */
void fieldAllOnes(uint8_t length, uint8_t* out, uint8_t flags);

#endif // FIELD_ENCODE_H
