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
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../storage/system_config.h"
#include "obd_query.h"

#if INCLUDE_CAN

#include "../hal/hal_can.h"
#include "../inputs/input_can.h"

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

// ===== MULTI-FRAME (ISO 15765-2) ANSWERS =====
//
// J1979 PIDs whose answer is longer than a single frame (0x70 boost: 12
// bytes, 0x78 EGT: 11, counting mode + PID) go out as a First Frame, then,
// once the tester's Flow Control allows it, Consecutive Frames (#516).
// One answer in flight; a new request replaces it. Paced from updateCAN().

#define ISOTP_FC_TIMEOUT_MS 1000   // N_Bs: give up if no Flow Control arrives

static struct {
    uint8_t  buf[OBD_ANSWER_MAX];
    uint8_t  len;         // answer bytes (mode + PID + payload)
    uint8_t  pos;         // next byte to send
    uint8_t  seq;         // next Consecutive Frame sequence number
    uint8_t  blockSize;   // CFs allowed before the next Flow Control (0 = all)
    uint8_t  sentInBlock;
    uint8_t  stMinMs;
    uint32_t lastMs;
    enum : uint8_t { IDLE, WAIT_FC, SENDING } state;
} isotp = {};

static void isotpSendConsecutive() {
    byte f[8] = {0};
    f[0] = 0x20 | (isotp.seq & 0x0F);
    for (uint8_t i = 0; i < 7 && isotp.pos < isotp.len; i++) f[1 + i] = isotp.buf[isotp.pos++];
    sendCANFrame(0x7E8, f, 8);
    isotp.seq = (isotp.seq + 1) & 0x0F;
    isotp.lastMs = millis();
    isotp.sentInBlock++;
    if (isotp.pos >= isotp.len) isotp.state = isotp.IDLE;
    else if (isotp.blockSize && isotp.sentInBlock >= isotp.blockSize) {
        isotp.state = isotp.WAIT_FC;
        isotp.sentInBlock = 0;
    }
}

// A Flow Control frame from the tester (30 = continue, 31 = wait, 32 = abort)
static void isotpOnFlowControl(const byte* data, uint8_t len) {
    if (isotp.state != isotp.WAIT_FC || len < 3) return;
    switch (data[0] & 0x0F) {
        case 0:   // ContinueToSend
            isotp.blockSize = data[1];
            // STmin: 0x00-0x7F ms; 0xF1-0xF9 are 100-900 us, under one tick;
            // reserved values count as the longest, 127 ms (ISO 15765-2)
            isotp.stMinMs = data[2] <= 0x7F ? data[2]
                          : (data[2] >= 0xF1 && data[2] <= 0xF9) ? 0 : 0x7F;
            isotp.sentInBlock = 0;
            isotp.state = isotp.SENDING;
            isotp.lastMs = millis() - isotp.stMinMs;   // first CF right away
            break;
        case 1:   // Wait: the tester restarts the timeout
            isotp.lastMs = millis();
            break;
        default:  // Overflow / abort
            isotp.state = isotp.IDLE;
            break;
    }
}

static void isotpTick() {
    if (isotp.state == isotp.IDLE) return;
    uint32_t now = millis();
    if (isotp.state == isotp.WAIT_FC) {
        if (now - isotp.lastMs > ISOTP_FC_TIMEOUT_MS) isotp.state = isotp.IDLE;
        return;
    }
    while (isotp.state == isotp.SENDING && (uint32_t)(now - isotp.lastMs) >= isotp.stMinMs) {
        isotpSendConsecutive();
        if (isotp.stMinMs) break;   // one per STmin
    }
}

/**
 * Send an input's Mode 01 answer on 0x7E8: one frame when it fits,
 * otherwise a First Frame and the rest on the tester's Flow Control.
 */
static void sendOBD2Response(Input* input) {
    uint8_t answer[OBD_ANSWER_MAX];
    uint8_t n = obdBuildAnswer(*input, answer);
    if (n == 0) {
        // No answer (e.g. the input's type changed since the PID table was
        // built): say so, rather than let the tester time out
        byte nrc[8] = {0x03, 0x7F, 0x01, 0x31, 0, 0, 0, 0};
        sendCANFrame(0x7E8, nrc, 8);
        return;
    }
    byte f[8] = {0};
    if (n <= 7) {
        f[0] = n;
        for (uint8_t i = 0; i < n; i++) f[1 + i] = answer[i];
        sendCANFrame(0x7E8, f, 8);
        return;
    }
    memcpy(isotp.buf, answer, n);
    isotp.len = n;
    f[0] = 0x10;              // First Frame, length high nibble 0
    f[1] = n;
    for (uint8_t i = 0; i < 6; i++) f[2 + i] = answer[i];
    isotp.pos = 6;
    isotp.seq = 1;
    isotp.sentInBlock = 0;
    isotp.state = isotp.WAIT_FC;
    isotp.lastMs = millis();
    sendCANFrame(0x7E8, f, 8);
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

    // Flow Control for a multi-frame answer in flight (physical address only)
    if ((data[0] & 0xF0) == 0x30) {
        if (canId == 0x7E0) isotpOnFlowControl(data, len);
        return;
    }

    // A new request replaces a multi-frame answer still in flight
    isotp.state = isotp.IDLE;

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

/**
 * OBD-II request drained by CAN input on a shared bus (#495). Output's own
 * read loop is skipped there, so this is the only way requests reach the
 * responder. The module can be disabled at runtime without a re-init.
 */
static void onSharedBusRequest(uint32_t id, const uint8_t* data, uint8_t len) {
    if (!systemConfig.outputEnabled[OUTPUT_CAN] || !systemConfig.buses.can_output_enabled) return;
    processOBD2Request(id, data, len);
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

    // Shared with CAN input: input drains the bus and hands requests over
    setCANRequestHandler(canInputOwnsBus(canOutputBus) ? onSharedBusRequest : nullptr);

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

    // A single frame on 0x7E8 in the middle of a multi-frame answer makes the
    // tester drop the answer (ISO 15765-2): hold broadcasts until it is done
    if (isotp.state != isotp.IDLE) return;

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

    isotpTick();   // pace a multi-frame answer's Consecutive Frames

    // Shared with CAN input: input is the only reader (see onSharedBusRequest)
    if (canInputOwnsBus(canOutputBus)) return;

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
