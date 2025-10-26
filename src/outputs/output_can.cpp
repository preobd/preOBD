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
    
    byte frameData[8] = {0};
    
    // Build OBDII frame
    frameData[0] = ptr->obd2length;  // Number of additional bytes
    frameData[1] = 0x41;             // Mode 01: Show current data
    frameData[2] = ptr->obd2pid;     // PID
    
    // Convert value to OBDII format using sensor's conversion function
    float obdValue = ptr->obdConvert(ptr->value);
    uint16_t value = (uint16_t)obdValue;
    
    // Split into bytes (little endian for compatibility)
    frameData[3] = value & 0xFF;         // LSB
    frameData[4] = (value >> 8) & 0xFF;  // MSB
    
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
