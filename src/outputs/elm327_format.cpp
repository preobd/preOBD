/*
 * elm327_format.cpp - see elm327_format.h
 */

#include <Arduino.h>
#include "elm327_format.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM void elmFormatAnswer(const uint8_t* bytes, uint8_t len, bool headersOn, bool spacesOn,
                     char* out, size_t outSize)
{
    size_t pos = 0;

    auto appendHex = [&](uint8_t b) {
        static const char hex[] = "0123456789ABCDEF";
        if (pos + 2 < outSize) {
            out[pos++] = hex[b >> 4];
            out[pos++] = hex[b & 0x0F];
        }
    };
    auto appendChar = [&](char c) {
        if (pos + 1 < outSize) out[pos++] = c;
    };
    auto appendStr = [&](const char* s) {
        while (*s && pos + 1 < outSize) out[pos++] = *s++;
    };
    // Reference: a genuine STN1155 (OBDLink LX, v4.0.0) reading preOBD over
    // CAN, captured 2026-10-02 (#528 follow-up). Apps are written against
    // real adapters, so the emulator copies them byte for byte rather than
    // trusting every client to normalise whitespace:
    //   - with spaces on, every byte is followed by a space, so each line
    //     (the "00C " byte count too) ends in one before its \r. With spaces
    //     off there is nothing to trail.
    //   - with headers on, the whole 8-byte CAN frame is shown, padding
    //     included ("7E8 06 41 00 80 00 00 01 00 "), not just the length
    //     byte's worth. Headers off shows only the answer.
    auto endLine = [&]() {
        if (spacesOn) appendChar(' ');
        appendChar('\r');
    };

    // Longer than one CAN frame (0x70 boost, 0x78 EGT; #516): print it the way
    // an ELM327 prints an ISO-TP reply.
    //   headers on:  the raw frames, "7E8 10 0C 41 70 .." / "7E8 21 .."
    //   headers off: the byte count "00C", then "0: " + the First Frame's 6
    //                data bytes and "1: " .. + 7 per Consecutive Frame
    // Frames are padded with 00 to 8 bytes, as sent on the bus.
    if (len > 7) {
        if (!headersOn) {
            appendChar('0');            // "00C": the byte count in 3 hex digits
            appendHex(len);
            endLine();
        }
        uint8_t i = 0;
        for (uint8_t frame = 0; i < len; frame++) {
            uint8_t take = frame == 0 ? 6 : 7;
            if (headersOn) {
                appendStr(spacesOn ? "7E8 " : "7E8");
                if (frame == 0) {
                    appendHex(0x10);
                    if (spacesOn) appendChar(' ');
                    appendHex(len);
                } else {
                    appendHex(0x20 | (frame & 0x0F));
                }
            } else {
                appendChar("0123456789ABCDEF"[frame & 0x0F]);
                appendChar(':');
            }
            for (uint8_t k = 0; k < take; k++) {
                if (spacesOn) appendChar(' ');
                appendHex(i < len ? bytes[i] : 0x00);
                i++;
            }
            endLine();
        }
        appendStr("\r>");
        out[pos] = '\0';
        return;
    }

    if (headersOn) {
        // Fixed ECU response header 7E8, followed by length byte
        if (spacesOn) {
            appendStr("7E8 ");
            appendHex(len);
            appendChar(' ');
        } else {
            appendStr("7E8");
            appendHex(len);
        }
    }

    // With headers, the frame's 7 data bytes, zero-padded as sent on the bus
    const uint8_t shown = headersOn ? 7 : len;
    for (uint8_t i = 0; i < shown; i++) {
        if (spacesOn && i > 0) appendChar(' ');
        appendHex(i < len ? bytes[i] : 0x00);
    }

    endLine();
    appendStr("\r>");
    out[pos] = '\0';
}

