/*
 * output_can.cpp - CAN bus output module
 * Supports both native FlexCAN (Teensy 3.x/4.x) and MCP2515 (all boards)
 *
 * Features:
 * - Broadcast mode: Periodic transmission of all sensor PIDs (for RealDash)
 * - Request/Response mode: OBD-II Mode 01 queries (for ELM327/Torque)
 * - Hybrid mode: Both modes work simultaneously
 */

#include "../config.h"
#include "output_base.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"

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

// ===== OBD-II REQUEST/RESPONSE SUPPORT =====

// PID Lookup Table - Maps PIDs to Input pointers for fast lookup
#define MAX_PID_ENTRIES 64

struct PIDMapping {
    uint8_t pid;
    Input* inputPtr;
};

static PIDMapping pidLookupTable[MAX_PID_ENTRIES];
static uint8_t pidLookupCount = 0;

// ===== PLATFORM ABSTRACTION =====

/**
 * Platform-agnostic CAN frame sender
 * @param canId CAN identifier (0x7E8 for ECU responses)
 * @param data Frame data (8 bytes)
 * @param len Data length (usually 8)
 */
static void sendCANFrame(uint32_t canId, const byte* data, uint8_t len) {
    #ifdef USING_FLEXCAN
        CAN_message_t msg;
        msg.id = canId;
        msg.len = len;
        msg.flags.extended = 0;
        msg.flags.remote = 0;
        memcpy(msg.buf, data, len);
        Can0.write(msg);
    #elif defined(USING_ESP32_TWAI)
        CanFrame frame;
        frame.identifier = canId;
        frame.extd = 0;
        frame.data_length_code = len;
        memcpy(frame.data, data, len);
        ESP32Can.writeFrame(frame);
    #else
        CAN.beginPacket(canId, len);
        CAN.write(data, len);
        CAN.endPacket();
    #endif
}

// ===== PID LOOKUP TABLE =====

/**
 * Build PID lookup table from active inputs
 * Called during initCAN() after inputs are configured
 */
static void buildPIDLookupTable() {
    pidLookupCount = 0;

    for (uint8_t i = 0; i < MAX_INPUTS && pidLookupCount < MAX_PID_ENTRIES; i++) {
        if (inputs[i].flags.isEnabled &&
            inputs[i].obd2pid != 0x00) {  // Skip invalid PIDs

            // Check for duplicate PIDs
            bool isDuplicate = false;
            for (uint8_t j = 0; j < pidLookupCount; j++) {
                if (pidLookupTable[j].pid == inputs[i].obd2pid) {
                    isDuplicate = true;
                    msg.debug.warn(TAG_CAN, "Duplicate PID 0x%02X - using first occurrence (%s)",
                                  inputs[i].obd2pid, pidLookupTable[j].inputPtr->abbrName);
                    break;
                }
            }

            if (!isDuplicate) {
                pidLookupTable[pidLookupCount].pid = inputs[i].obd2pid;
                pidLookupTable[pidLookupCount].inputPtr = &inputs[i];
                pidLookupCount++;
            }
        }
    }

    msg.debug.info(TAG_CAN, "Built OBD-II PID lookup table: %d PIDs available", pidLookupCount);
}

/**
 * Find Input by PID
 * @param pid OBD-II PID to lookup
 * @return Pointer to Input, or nullptr if not found
 */
static Input* findInputByPID(uint8_t pid) {
    for (uint8_t i = 0; i < pidLookupCount; i++) {
        if (pidLookupTable[i].pid == pid) {
            return pidLookupTable[i].inputPtr;
        }
    }
    return nullptr;
}

// ===== PID 00 (SUPPORTED PIDS BITMAP) =====

/**
 * Generate PID 00 bitmap (Supported PIDs 0x01-0x20)
 * Sets bit for each PID present in pidLookupTable
 *
 * Bitmap encoding (ISO 15765-4):
 *   Byte A, Bit 7 = PID 0x01 supported
 *   Byte A, Bit 6 = PID 0x02 supported
 *   ...
 *   Byte D, Bit 0 = PID 0x20 supported
 *
 * @param bitmap 4-byte buffer to fill
 */
static void generatePID00Bitmap(uint8_t* bitmap) {
    memset(bitmap, 0, 4);

    for (uint8_t i = 0; i < pidLookupCount; i++) {
        uint8_t pid = pidLookupTable[i].pid;

        // Only PIDs 0x01-0x20 go in PID 00 bitmap
        if (pid >= 0x01 && pid <= 0x20) {
            uint8_t byteIndex = (pid - 1) / 8;     // Which byte (0-3)
            uint8_t bitIndex = 7 - ((pid - 1) % 8); // Which bit (7-0, MSB first)
            bitmap[byteIndex] |= (1 << bitIndex);
        }
    }
}

/**
 * Send Mode 01 PID 00 response (Supported PIDs)
 * Single frame format: [06 41 00 XX XX XX XX 00]
 * Length=6: mode (1) + PID (1) + bitmap (4)
 */
static void sendPID00Response() {
    uint8_t bitmap[4];
    generatePID00Bitmap(bitmap);

    // Single frame response (fits in 8 bytes)
    byte frameData[8] = {
        0x06,         // Length: 6 bytes (mode + PID + 4 bitmap bytes)
        0x41,         // Mode 01 response
        0x00,         // PID 00
        bitmap[0],    // PIDs 0x01-0x08
        bitmap[1],    // PIDs 0x09-0x10
        bitmap[2],    // PIDs 0x11-0x18
        bitmap[3],    // PIDs 0x19-0x20
        0x00          // Padding
    };

    sendCANFrame(0x7E8, frameData, 8);

    #ifdef DEBUG
    msg.debug.debug(TAG_CAN, "PID 00 bitmap: %02X %02X %02X %02X",
                   bitmap[0], bitmap[1], bitmap[2], bitmap[3]);
    #endif
}

// ===== REQUEST PROCESSING =====

/**
 * Send OBD-II negative response (ISO 14229-1)
 * @param requestId CAN ID of request (for logging)
 * @param mode Service ID that failed
 * @param nrc Negative Response Code
 */
static void sendNegativeResponse(uint32_t requestId, uint8_t mode, uint8_t nrc) {
    byte frameData[8] = {0x03, 0x7F, mode, nrc, 0x00, 0x00, 0x00, 0x00};
    //                    Len  Neg  Mode NRC

    sendCANFrame(0x7E8, frameData, 8);

    #ifdef DEBUG
    msg.debug.debug(TAG_CAN, "Sent negative response: NRC 0x%02X", nrc);
    #endif
}

/**
 * Send OBD-II Mode 01 response on CAN ID 0x7E8
 * @param input Input to respond with
 */
static void sendOBD2Response(Input* input) {
    byte frameData[8];

    if (!buildOBD2Frame(frameData, input)) {
        msg.debug.warn(TAG_CAN, "Failed to build OBD2 response");
        return;
    }

    sendCANFrame(0x7E8, frameData, 8);
}

/**
 * Parse and process OBD-II Mode 01 request
 * Handles both functional (0x7DF) and physical (0x7E0) addressing
 *
 * Frame format (ISO 15765-4):
 *   [0] = Length (2 for Mode 01)
 *   [1] = Mode (0x01 = Show current data)
 *   [2] = PID
 *   [3-7] = Unused (padding)
 *
 * @param msg Received CAN message
 */
static void processOBD2Request(uint32_t canId, const byte* data, uint8_t len) {
    // Validate minimum frame structure
    if (len < 3) return;

    // data[0] is frame length, not validated per OBD-II spec
    uint8_t mode = data[1];
    uint8_t pid = data[2];

    #ifdef DEBUG
    msg.debug.debug(TAG_CAN, "OBD-II Request: Mode=0x%02X PID=0x%02X", mode, pid);
    #endif

    // Only handle Mode 01 (Show current data)
    if (mode != 0x01) {
        sendNegativeResponse(canId, mode, 0x12);  // Sub-function not supported
        return;
    }

    // Special case: PID 00 (Supported PIDs 0x01-0x20)
    if (pid == 0x00) {
        sendPID00Response();
        return;
    }

    // Lookup PID in active inputs
    Input* input = findInputByPID(pid);
    if (input == nullptr || isnan(input->value)) {
        // PID not supported or no valid data
        sendNegativeResponse(canId, mode, 0x31);  // Request out of range
        return;
    }

    // Build and send response
    sendOBD2Response(input);
}

void initCAN() {
    #ifdef USING_FLEXCAN
        // Initialize native FlexCAN
        Can0.begin();
        Can0.setBaudRate(500000);  // 500 kbps
        Can0.setMaxMB(16);

        // Configure RX filters for OBD-II requests
        Can0.setMBFilter(MB0, 0x7DF);  // Functional addressing (broadcast)
        Can0.setMBFilter(MB1, 0x7E0);  // Physical addressing (ECU 0)
        Can0.enableMBInterrupt(MB0);
        Can0.enableMBInterrupt(MB1);

        msg.debug.info(TAG_CAN, "Native FlexCAN initialized (500kbps)");
        msg.debug.info(TAG_CAN, "OBD-II RX filters configured (0x7DF, 0x7E0)");
    #elif defined(USING_ESP32_TWAI)
        // Initialize ESP32 TWAI (CAN)
        // Note: External CAN transceiver required (MCP2551, TJA1050, SN65HVD230, etc.)
        #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
            // ESP32-S3/C3 pins: GPIO20 (TX), GPIO21 (RX) - GPIO22 doesn't exist on S3
            ESP32Can.setPins(GPIO_NUM_20, GPIO_NUM_21);  // TX, RX
        #else
            // Original ESP32 pins: GPIO21 (TX), GPIO22 (RX)
            ESP32Can.setPins(GPIO_NUM_21, GPIO_NUM_22);  // TX, RX
        #endif
        ESP32Can.setSpeed(ESP32Can.convertSpeed(500));  // 500 kbps
        if (ESP32Can.begin()) {
            msg.debug.info(TAG_CAN, "ESP32 TWAI (CAN) initialized (500kbps)");
            msg.debug.info(TAG_CAN, "OBD-II request/response enabled");
        } else {
            msg.debug.error(TAG_CAN, "ESP32 TWAI init failed!");
            return;
        }
    #else
        // Initialize MCP2515 via SPI
        CAN.setPins(CAN_CS, CAN_INT);
        if (!CAN.begin(500E3)) {
            msg.debug.error(TAG_CAN, "MCP2515 CAN init failed!");
            return;
        }
        msg.debug.info(TAG_CAN, "MCP2515 CAN initialized (500kbps)");
        msg.debug.info(TAG_CAN, "OBD-II request/response enabled");

        // Uncomment for testing
        // CAN.loopback();
    #endif

    // Build PID lookup table for request/response
    buildPIDLookupTable();
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
    // Process incoming OBD-II requests (request/response mode)
    #ifdef USING_FLEXCAN
        CAN_message_t msg;
        while (Can0.read(msg)) {
            // Check if this is an OBD-II request
            if (msg.id == 0x7DF || msg.id == 0x7E0) {
                processOBD2Request(msg.id, msg.buf, msg.len);
            }
        }
    #elif defined(USING_ESP32_TWAI)
        CanFrame frame;
        while (ESP32Can.readFrame(frame, 0)) {  // Non-blocking read
            // Check if this is an OBD-II request
            if (frame.identifier == 0x7DF || frame.identifier == 0x7E0) {
                processOBD2Request(frame.identifier, frame.data, frame.data_length_code);
            }
        }
    #else
        // MCP2515: Check for incoming packets
        int packetSize = CAN.parsePacket();
        if (packetSize > 0) {
            uint32_t id = CAN.packetId();
            if (id == 0x7DF || id == 0x7E0) {
                byte data[8];
                uint8_t len = 0;
                while (CAN.available() && len < 8) {
                    data[len++] = CAN.read();
                }
                processOBD2Request(id, data, len);
            }
        }
    #endif
}

#else

// Dummy functions if CAN is disabled
void initCAN() {}
void sendCAN(Input *ptr) {}
void updateCAN() {}

#endif
