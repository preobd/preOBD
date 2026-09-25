#include "status_compose.h"

#include <string.h>

void statusComposeInit(StatusComposer& c) {
    memset(&c, 0, sizeof(c));
}

void statusComposeOnExt(StatusComposer& c, const uint8_t* payload, uint16_t len) {
    if (len < kStatusComposeExtMinSize || len > kStatusComposeExtMaxSize) {
        c.extLen = 0;
        return;
    }
    memcpy(c.ext, payload, len);
    c.extLen = len;
}

uint16_t statusComposeOnStatus(StatusComposer& c, const uint8_t* payload,
                               uint16_t len, uint8_t* out) {
    const uint16_t held = c.extLen;
    c.extLen = 0;
    if (len != kStatusComposeBaseSize) return 0;
    memcpy(out, payload, kStatusComposeBaseSize);
    memcpy(out + kStatusComposeBaseSize, c.ext, held);
    return (uint16_t)(kStatusComposeBaseSize + held);
}
