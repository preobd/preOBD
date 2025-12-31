/*
 * output_realdash.cpp - RealDash binary CAN output for data plane
 */

#include "output_base.h"
#include "../config.h"
#include "../lib/message_api.h"

#ifdef ENABLE_REALDASH

void initRealdash() {
    // Serial initialization happens in main.cpp
    msg.data.println("✓ RealDash output initialized");
}

void sendRealdash(Input *ptr) {
    if (isnan(ptr->value)) {
        return;
    }

    // RealDash requires specific framing
    byte preamble[4] = {0x44, 0x33, 0x22, 0x11};
    unsigned long canFrameId = 0x0C80;  // Base frame ID

    byte frameData[8];

    // Build OBDII frame using shared helper (fixes length byte and endianness)
    if (!buildOBD2Frame(frameData, ptr)) {
        return;  // Invalid data size
    }

    // Send RealDash frame to data plane
    msg.data.write(preamble, 4);
    msg.data.write((const byte*)&canFrameId, 4);
    msg.data.write(frameData, 8);
}

void updateRealdash() {
    // Handle any incoming RealDash commands if needed
}

#else

void initRealdash() {}
void sendRealdash(Input *ptr) {}
void updateRealdash() {}

#endif
