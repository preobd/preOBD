// uuid_util.h -- ASCII UUID string to 16-byte little-endian array.

#ifndef UUID_UTIL_H
#define UUID_UTIL_H

#include <stdint.h>

// Parse a canonical 36-char UUID ("8-4-4-4-12" with '-' separators) into a
// 16-byte array in the little-endian byte order Bluefruit expects. Returns
// false on malformed input.
bool parseUuid128(const char* s, uint8_t out[16]);

#endif // UUID_UTIL_H
