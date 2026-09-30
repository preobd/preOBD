/*
 * elm327_format.h - ELM327-style text of a Mode 01 answer (#516)
 *
 * Pure (no serial, no globals) so the native tests pin the exact text.
 */

#ifndef ELM327_FORMAT_H
#define ELM327_FORMAT_H

#include <stdint.h>
#include <stddef.h>

/**
 * `bytes` = 0x41, PID, payload. Up to 7 bytes: one line, as an ELM327
 * prints a single frame (headers: "7E8 <len> ..."). Longer (0x70 boost,
 * 0x78 EGT): the ISO-TP frames, as an ELM327 prints a multi-frame reply.
 * Ends with "\r\r>".
 */
void elmFormatAnswer(const uint8_t* bytes, uint8_t len, bool headersOn, bool spacesOn,
                     char* out, size_t outSize);

#endif // ELM327_FORMAT_H
