/*
 * hash.h - DJB2 Hash Function for String Lookups
 *
 * Implements 16-bit case-insensitive DJB2 hash for efficient registry lookups.
 * This is a simple, well-tested hash function suitable for embedded systems.
 *
 * DJB2 Algorithm:
 *   hash = 5381
 *   for each character: hash = ((hash << 5) + hash) + c  // hash * 33 + c
 *
 * Modifications:
 *   - 16-bit output (uint16_t) to save memory
 *   - Case-insensitive (converts to uppercase during hashing)
 *   - Optimized for AVR with minimal RAM usage
 */

#ifndef HASH_H
#define HASH_H

#include <Arduino.h>
#include <ctype.h>

/**
 * DJB2 hash function - 16-bit, case-insensitive
 *
 * Computes a 16-bit hash of a null-terminated string.
 * Converts all characters to uppercase before hashing for case-insensitive lookups.
 *
 * @param str  Null-terminated string to hash (must not be null)
 * @return     16-bit hash value (0-65535)
 *
 * Usage:
 *   uint16_t hash = djb2_hash("celsius");  // Case-insensitive
 *   uint16_t hash2 = djb2_hash("CELSIUS"); // Same hash value
 */
inline uint16_t djb2_hash(const char* str) {
    if (!str) return 0;

    uint32_t hash = 5381;  // Use 32-bit for intermediate calculations
    char c;

    while ((c = *str++)) {
        // Convert to uppercase for case-insensitive hashing
        c = toupper(c);
        // DJB2: hash = hash * 33 + c
        hash = ((hash << 5) + hash) + c;
    }

    // Return lower 16 bits for compact storage
    return (uint16_t)(hash & 0xFFFF);
}

/**
 * DJB2 hash function for PROGMEM strings
 *
 * Same as djb2_hash() but reads from flash memory (PROGMEM).
 * Used to hash strings stored in flash memory.
 *
 * @param str  Pointer to null-terminated string in PROGMEM
 * @return     16-bit hash value (0-65535)
 */
inline uint16_t djb2_hash_P(const char* str) {
    if (!str) return 0;

    uint32_t hash = 5381;
    char c;

    while ((c = pgm_read_byte(str++))) {
        c = toupper(c);
        hash = ((hash << 5) + hash) + c;
    }

    return (uint16_t)(hash & 0xFFFF);
}

#endif // HASH_H
