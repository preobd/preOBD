/*
 * output_realdash.cpp - RealDash CAN output module
 */

#include "output_base.h"
#include "../config.h"

#ifdef ENABLE_REALDASH

void initRealdash() {
    Serial.begin(115200);
    Serial.println("RealDash output initialized");
}

void sendRealdash(Sensor *ptr) {
    if (isnan(ptr->value)) {
        return;
    }
    
    // RealDash requires specific framing
    byte preamble[4] = {0x44, 0x33, 0x22, 0x11};
    unsigned long canFrameId = 0x0C80;  // Base frame ID
    
    byte frameData[8] = {0};
    frameData[0] = ptr->obd2length;
    frameData[1] = 0x41;
    frameData[2] = ptr->obd2pid;
    
    float obdValue = ptr->obdConvert(ptr->value);
    uint16_t value = (uint16_t)obdValue;
    
    frameData[3] = value & 0xFF;
    frameData[4] = (value >> 8) & 0xFF;
    
    // Send RealDash frame
    Serial.write(preamble, 4);
    Serial.write((const byte*)&canFrameId, 4);
    Serial.write(frameData, 8);
}

void updateRealdash() {
    // Handle any incoming RealDash commands if needed
}

#else

void initRealdash() {}
void sendRealdash(Sensor *ptr) {}
void updateRealdash() {}

#endif
