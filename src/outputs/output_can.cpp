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
#include "../lib/obd_query.h"

#if ENABLE_CAN

#include "../hal/hal_can.h"

// Which bus we're outputting on (set during init)
static uint8_t canOutputBus = 0;

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

/**
 * Send a supported-PIDs discovery response for the given base PID.
 * Handles the full chain: 0x00, 0x20, 0x40, 0x60, …
 * Single frame format: [06 41 <pid> XX XX XX XX 00]
 * Length=6: mode (1) + PID (1) + bitmap (4)
 */
static void sendSupportedPIDsResponse(uint8_t basePID) {
    uint8_t bitmap[4];
    obdQuery_getSupportedPIDBitmap(bitmap, basePID);

    byte frameData[8] = {
        0x06,         // Length: 6 bytes (mode + PID + 4 bitmap bytes)
        0x41,         // Mode 01 response
        basePID,
        bitmap[0],
        bitmap[1],
        bitmap[2],
        bitmap[3],
        0x00          // Padding
    };

    sendCANFrame(0x7E8, frameData, 8);

    #ifdef DEBUG
    msg.debug.debug(TAG_CAN, "PID 0x%02X bitmap: %02X %02X %02X %02X",
                   basePID, bitmap[0], bitmap[1], bitmap[2], bitmap[3]);
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

    // Discovery PIDs: 0x00, 0x20, 0x40 … 0xE0 (supported-PIDs chain)
    if ((pid & 0x1F) == 0x00) {
        sendSupportedPIDsResponse(pid);
        return;
    }

    // Lookup PID in active inputs
    Input* input = obdQuery_findByPID(pid);
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
    // PID lookup table built in main.cpp after initOutputModules()
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
