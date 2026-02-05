/*
 * output_can.cpp - CAN bus output module
 * Supports FlexCAN (Teensy), TWAI (ESP32), and MCP2515 (AVR) via HAL
 *
 * Features:
 * - Broadcast mode: Periodic transmission of all sensor PIDs (for RealDash)
 * - Request/Response mode: OBD-II Mode 01 queries (for ELM327/Torque)
 * - Hybrid mode: Both modes work simultaneously
 * - Configurable output bus (supports dual-bus on Teensy)
 */

#include "../config.h"
#include "output_base.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "../lib/system_config.h"

#ifdef ENABLE_CAN

#include "../hal/hal_can.h"

// Which bus we're outputting on (set during init)
static uint8_t canOutputBus = 0;

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
    hal::can::write(canId, data, len, false, canOutputBus);  // Standard 11-bit ID
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
    (void)requestId;  // Used only for debug logging
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
 * @param canId CAN ID of received request
 * @param data Received frame data
 * @param len Frame data length
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
    // Check if output is enabled
    if (!systemConfig.buses.can_output_enabled) {
        return;
    }

    canOutputBus = systemConfig.buses.output_can_bus;
    if (canOutputBus == 0xFF) {
        return;  // No output bus configured
    }

    uint32_t baudrate = systemConfig.buses.can_output_baudrate;

    // Initialize CAN bus via HAL
    if (!hal::can::begin(baudrate, canOutputBus)) {
        msg.debug.error(TAG_CAN, "CAN output init failed on bus %d!", canOutputBus);
        return;
    }

    // Configure RX filters for OBD-II requests
    hal::can::setFilters(0x7DF, 0x7E0, canOutputBus);  // Functional and physical addressing

    msg.debug.info(TAG_CAN, "CAN output initialized on bus %d (%lu bps)", canOutputBus, baudrate);
    msg.debug.info(TAG_CAN, "OBD-II request/response enabled");

    // Build PID lookup table for request/response
    buildPIDLookupTable();
}

void sendCAN(Input *ptr) {
    if (!systemConfig.buses.can_output_enabled || canOutputBus == 0xFF) {
        return;  // Output not configured
    }

    if (isnan(ptr->value)) {
        return;  // Don't send invalid data
    }

    byte frameData[8];

    // Build OBDII frame using shared helper (fixes length byte and endianness)
    if (!buildOBD2Frame(frameData, ptr)) {
        return;  // Invalid data size
    }

    // Send on standard OBDII ECU response ID
    hal::can::write(0x7E8, frameData, 8, false, canOutputBus);
}

void updateCAN() {
    if (!systemConfig.buses.can_output_enabled || canOutputBus == 0xFF) {
        return;  // Output not configured
    }

    // Process incoming OBD-II requests (request/response mode)
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
    bool extended;

    while (hal::can::read(id, data, len, extended, canOutputBus)) {
        // Check if this is an OBD-II request
        if (id == 0x7DF || id == 0x7E0) {
            processOBD2Request(id, data, len);
        }
    }
}

#else

// Dummy functions if CAN is disabled
void initCAN() {}
void sendCAN(Input *ptr) { (void)ptr; }
void updateCAN() {}

#endif
