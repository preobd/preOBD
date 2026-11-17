/*
 * output_realdash.cpp - RealDash binary CAN frames on a stream (#612: the
 * output's own transport, which it has to itself: no text may land in it)
 */

#include "../table/output_base.h"
#include "../../config.h"

#if INCLUDE_REALDASH

// One input as a RealDash "CAN over serial" frame: 44 33 22 11, the frame ID
// (0x0C80, little-endian), then the input's Mode 01 answer as 8 bytes
// (buildOBD2Frame). The baseline these bytes must match is in #612.
void sendRealdashFrame(Input* ptr, Print& out) {
    if (isnan(ptr->value)) return;
    static const uint8_t preamble[4] = {0x44, 0x33, 0x22, 0x11};
    static const uint8_t frameId[4]  = {0x80, 0x0C, 0x00, 0x00};   // 0x0C80, LE
    uint8_t frameData[8];
    if (!buildOBD2Frame(frameData, ptr)) return;   // no answerable PID
    out.write(preamble, 4);
    out.write(frameId, 4);
    out.write(frameData, 8);
}

#else

void sendRealdashFrame(Input*, Print&) {}

#endif
