#include "uuid_util.h"

#include <ctype.h>
#include <stdlib.h>

bool parseUuid128(const char* s, uint8_t out[16]) {
    size_t bi = 0;
    for (size_t i = 0; s[i] != '\0' && bi < 16; ) {
        if (s[i] == '-') { ++i; continue; }
        if (!isxdigit(s[i]) || !isxdigit(s[i + 1])) return false;
        char buf[3] = { s[i], s[i + 1], 0 };
        out[bi++] = static_cast<uint8_t>(strtoul(buf, nullptr, 16));
        i += 2;
    }
    if (bi != 16) return false;
    // Bluefruit expects little-endian byte order in memory.
    for (size_t i = 0; i < 8; ++i) {
        uint8_t t = out[i];
        out[i] = out[15 - i];
        out[15 - i] = t;
    }
    return true;
}
