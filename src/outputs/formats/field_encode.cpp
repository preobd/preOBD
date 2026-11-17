/*
 * field_encode.cpp - see field_encode.h. Pure: no globals.
 */

#include <math.h>
#include "field_encode.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static void packField(uint32_t r, uint8_t length, uint8_t flags, uint8_t* out) {
    for (uint8_t i = 0; i < length; i++) {
        uint8_t shift = (flags & FIELD_LITTLE_ENDIAN) ? i : (uint8_t)(length - 1 - i);
        out[i] = (uint8_t)(r >> (8 * shift));
    }
}

FLASHMEM uint8_t encodeScaledField(float value, float scale, float offset, uint8_t length,
                                   uint8_t flags, bool reserveAllOnes, uint8_t* out) {
    if (!isfinite(value)) return 0;
    if (length != 1 && length != 2 && length != 4) return 0;

    // raw = (value - offset) / scale, rounded and clamped in float before any
    // integer cast (casting an out-of-range float is undefined)
    float raw = (value - offset) / scale;
    uint32_t r;

    if (flags & FIELD_SIGNED) {
        const int32_t maxV = (int32_t)((1UL << (8 * length - 1)) - 1);   // 127, 32767, 2^31-1
        const int32_t minV = -maxV - 1;
        const int32_t limit = reserveAllOnes ? maxV - 1 : maxV;
        const float hiF = (float)maxV + 1.0f;      // 2^(bits-1), exact in float
        const float loF = (float)minV;             // -2^(bits-1), exact
        int32_t s;
        if (raw != raw) s = 0;                      // NaN (scale 0, value == offset)
        else if (raw + 0.5f >= hiF) s = limit;
        else if (raw + 0.5f <= loF) s = minV;
        else {
            s = (int32_t)floorf(raw + 0.5f);
            if (s > limit) s = limit;
            if (s < minV) s = minV;
        }
        r = (uint32_t)s;
    } else {
        // 4294967295.0f rounds to 2^32 in float; that is the right bound
        const float maxF = length >= 4 ? 4294967295.0f
                                       : (float)((1UL << (8 * length)) - 1);
        const uint32_t maxV = length >= 4 ? 0xFFFFFFFFUL : (uint32_t)maxF;
        const uint32_t limit = reserveAllOnes ? maxV - 1 : maxV;
        const float limitF = length >= 4 ? 4294967295.0f : (float)limit;
        if (!(raw > 0.0f)) r = 0;                   // also NaN
        else if (raw + 0.5f >= limitF) r = limit;
        else {
            r = (uint32_t)(raw + 0.5f);
            if (r > limit) r = limit;
        }
    }
    packField(r, length, flags, out);
    return length;
}

FLASHMEM void fieldAllOnes(uint8_t length, uint8_t* out, uint8_t flags) {
    if (length != 1 && length != 2 && length != 4) return;
    uint32_t r = (flags & FIELD_SIGNED) ? (uint32_t)((1UL << (8 * length - 1)) - 1)
                                        : (length >= 4 ? 0xFFFFFFFFUL : (1UL << (8 * length)) - 1);
    packField(r, length, flags, out);
}
