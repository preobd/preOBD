/*
 * output_can.cpp - CAN bus output module
 * Supports both native FlexCAN (Teensy 3.x/4.x) and MCP2515 (all boards)
 */

#include "../config.h"
#include "output_base.h"
#include "../inputs/input.h"

#ifdef ENABLE_CAN

// Select CAN library based on platform and configuration
#if defined(USE_FLEXCAN_NATIVE) && (defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__) || defined(__IMXRT1062__))
    // Use native FlexCAN on Teensy 3.x/4.x
    #include <FlexCAN_T4.h>
    FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
    #define USING_FLEXCAN
#elif defined(ESP32)
    // Use native TWAI (CAN) on ESP32
    #include <ESP32-TWAI-CAN.hpp>
    #define USING_ESP32_TWAI
#else
    // Use MCP2515 via SPI (all other boards or when FlexCAN not selected)
    #include <CAN.h>
    #define USING_MCP2515
#endif

void initCAN() {
    #ifdef USING_FLEXCAN
        // Initialize native FlexCAN
        Can0.begin();
        Can0.setBaudRate(500000);  // 500 kbps
        Can0.setMaxMB(16);
        Serial.println("Native FlexCAN initialized (500kbps)");
    #elif defined(USING_ESP32_TWAI)
        // Initialize ESP32 TWAI (CAN)
        // Default pins: GPIO21 (TX), GPIO22 (RX)
        // Note: External CAN transceiver required (MCP2551, TJA1050, SN65HVD230, etc.)
        ESP32Can.setPins(GPIO_NUM_21, GPIO_NUM_22);  // TX, RX
        ESP32Can.setSpeed(ESP32Can.convertSpeed(500));  // 500 kbps
        if (ESP32Can.begin()) {
            Serial.println("ESP32 TWAI (CAN) initialized (500kbps)");
        } else {
            Serial.println("ESP32 TWAI init failed!");
        }
    #else
        // Initialize MCP2515 via SPI
        CAN.setPins(CAN_CS, CAN_INT);
        if (!CAN.begin(500E3)) {
            Serial.println("MCP2515 CAN init failed!");
            return;
        }
        Serial.println("MCP2515 CAN initialized (500kbps)");

        // Uncomment for testing
        // CAN.loopback();
    #endif
}

void sendCAN(Input *ptr) {
    if (isnan(ptr->value)) {
        return;  // Don't send invalid data
    }

    byte frameData[8];

    // Build OBDII frame using shared helper (fixes length byte and endianness)
    if (!buildOBD2Frame(frameData, ptr)) {
        return;  // Invalid data size
    }

    // Send on standard OBDII ECU response ID
    #ifdef USING_FLEXCAN
        // Send using FlexCAN
        CAN_message_t msg;
        msg.id = 0x7E8;
        msg.len = 8;
        msg.flags.extended = 0;  // Standard 11-bit ID
        msg.flags.remote = 0;
        for (int i = 0; i < 8; i++) {
            msg.buf[i] = frameData[i];
        }
        Can0.write(msg);
    #elif defined(USING_ESP32_TWAI)
        // Send using ESP32 TWAI
        CanFrame frame;
        frame.identifier = 0x7E8;
        frame.extd = 0;  // Standard 11-bit ID
        frame.data_length_code = 8;
        for (int i = 0; i < 8; i++) {
            frame.data[i] = frameData[i];
        }
        ESP32Can.writeFrame(frame);
    #else
        // Send using MCP2515
        CAN.beginPacket(0x7E8, 8);
        CAN.write(frameData, 8);
        CAN.endPacket();
    #endif
}

void updateCAN() {
    // Can be used for receiving CAN messages if needed
    #ifdef USING_FLEXCAN
        CAN_message_t msg;
        while (Can0.read(msg)) {
            // Process received CAN messages here if needed in future
        }
    #elif defined(USING_ESP32_TWAI)
        CanFrame frame;
        if (ESP32Can.readFrame(frame, 0)) {  // Non-blocking read
            // Process received CAN messages here if needed in future
        }
    #endif
}

#else

// Dummy functions if CAN is disabled
void initCAN() {}
void sendCAN(Input *ptr) {}
void updateCAN() {}

#endif
