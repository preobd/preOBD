/*
 * output_can.cpp - CAN bus output module
 */

#include "output_base.h"
#include "../config.h"

#ifdef ENABLE_CAN

#include <CAN.h>

void initCAN() {
    CAN.setPins(CAN_CS, CAN_INT);
    if (!CAN.begin(500E3)) {
        Serial.println("CAN init failed!");
        return;
    }
    Serial.println("CAN initialized");
    
    // Uncomment for testing
    // CAN.loopback();
}

void sendCAN(Sensor *ptr) {
    if (isnan(ptr->value)) {
        return;  // Don't send invalid data
    }

    byte frameData[8];

    // Build OBDII frame using shared helper (fixes length byte and endianness)
    if (!buildOBD2Frame(frameData, ptr)) {
        return;  // Invalid data size
    }

    // Send on standard OBDII ECU response ID
    CAN.beginPacket(0x7E8, 8);
    CAN.write(frameData, 8);
    CAN.endPacket();
}

void updateCAN() {
    // Can be used for receiving CAN messages if needed
}

#else

// Dummy functions if CAN is disabled
void initCAN() {}
void sendCAN(Sensor *ptr) {}
void updateCAN() {}

#endif
