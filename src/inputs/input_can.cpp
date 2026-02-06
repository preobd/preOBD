/*
 * input_can.cpp - CAN Input Subsystem
 *
 * Separate CAN input subsystem - independent from CAN output.
 * Receives frames from configured input CAN bus and populates frame cache.
 * Supports OBD-II, J1939, and custom CAN protocols.
 *
 * Uses HAL for platform abstraction (FlexCAN, TWAI, MCP2515).
 * Supports dual-bus on Teensy (input on different bus than output).
 */

#include <Arduino.h>
#include "input_can.h"
#include "../lib/system_config.h"
#include "../lib/bus_config.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "sensors/can/can_frame_cache.h"
#include "../hal/hal_can.h"

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool canInputInitialized = false;
static uint8_t canInputBus = 0;  // Which bus we're reading from

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize CAN input subsystem
 * Sets up input CAN bus based on systemConfig.buses.input_can_bus
 * Uses HAL for platform abstraction.
 *
 * @return true if initialized successfully, false otherwise
 */
bool initCANInput() {
    // Check if input is enabled (NORMAL or LISTEN mode)
    uint8_t mode = systemConfig.buses.can_input_mode;
    if (mode == CAN_INPUT_OFF) {
        canInputInitialized = false;
        return false;
    }

    uint8_t bus = systemConfig.buses.input_can_bus;
    if (bus == 0xFF) {
        canInputInitialized = false;
        return false;  // No input bus configured
    }

    uint32_t baudrate = systemConfig.buses.can_input_baudrate;
    bool listenOnly = (mode == CAN_INPUT_LISTEN);

    // Check if sharing bus with output
    bool sharedBus = (bus == systemConfig.buses.output_can_bus &&
                     systemConfig.buses.can_output_enabled &&
                     bus != 0xFF);

    if (sharedBus) {
        // Output subsystem will initialize the shared bus
        // Just verify baudrates match
        if (baudrate != systemConfig.buses.can_output_baudrate) {
            msg.debug.warn(TAG_CAN, "Shared bus: using output baudrate %lu",
                          systemConfig.buses.can_output_baudrate);
        }
        canInputBus = bus;
        msg.debug.info(TAG_CAN, "CAN input using shared bus %d (initialized by output)", bus);
    } else {
        // Independent bus - initialize via HAL
        if (!hal::can::begin(baudrate, bus, listenOnly)) {
            msg.debug.error(TAG_CAN, "CAN input init failed on bus %d", bus);
            canInputInitialized = false;
            return false;
        }
        canInputBus = bus;
        const char* modeStr = listenOnly ? "listen-only" : "normal";
        msg.debug.info(TAG_CAN, "CAN input initialized on bus %d (%lu bps, %s)", bus, baudrate, modeStr);
    }

    // Initialize CAN frame cache
    initCANFrameCache();

    canInputInitialized = true;
    return true;
}

/**
 * Shutdown CAN input subsystem
 *
 * NOTE: This does NOT disable the CAN bus hardware peripheral for two reasons:
 * 1. Output subsystem may still be using the same physical bus
 * 2. HAL doesn't provide a bus-safe shutdown API (multiple subsystems per bus)
 *
 * This is intentional behavior - CAN hardware stays active but frame processing stops.
 * Power consumption: ~5-10mA (MCP2515) or ~2-3mA (FlexCAN standby mode).
 *
 * For true power-down, disable ENABLE_CAN at compile time or power-cycle the board.
 */
void shutdownCANInput() {
    canInputInitialized = false;
}

// ============================================================================
// FRAME RECEPTION
// ============================================================================

/**
 * Process a single CAN frame and extract data into cache
 * @param can_id    CAN identifier
 * @param data      Frame data buffer
 * @param len       Frame data length
 */
static void processCANFrame(uint32_t can_id, const uint8_t* data, uint8_t len) {
    // Validate frame has minimum data
    if (len == 0) {
        #ifdef DEBUG
        msg.debug.warn(TAG_CAN, "Empty CAN frame (ID 0x%03X)", can_id);
        #endif
        return;
    }

    // Detect protocol format and extract identifier
    uint8_t identifier;
    uint8_t data_offset;

    if (len >= 3 && data[0] == 0x04 && data[1] == 0x41) {
        // OBD-II Mode 01 response format:
        // [0] = 0x04 (number of data bytes)
        // [1] = 0x41 (Mode 01 response)
        // [2] = PID
        // [3+] = Data bytes
        identifier = data[2];  // PID
        data_offset = 3;
    } else if (len >= 3 && data[0] == 0x41) {
        // OBD-II without length prefix (some ECUs)
        // [0] = 0x41 (Mode 01 response)
        // [1] = PID
        // [2+] = Data bytes
        identifier = data[1];  // PID
        data_offset = 2;
    } else if (len >= 2 && data[0] == 0x41) {
        // Malformed OBD-II frame (too short for proper extraction)
        // Fall back to custom protocol handling
        #ifdef DEBUG
        msg.debug.warn(TAG_CAN, "Short OBD-II frame (ID 0x%03X, len=%d)", can_id, len);
        #endif
        identifier = data[0];
        data_offset = 0;
    } else {
        // Custom protocol or J1939 - use first byte as identifier
        identifier = data[0];
        data_offset = 0;
    }

    // Update cache with extracted data
    uint8_t data_length = (len > data_offset) ? (len - data_offset) : 0;
    if (data_length == 0) {
        #ifdef DEBUG
        msg.debug.warn(TAG_CAN, "No data after protocol parsing (ID 0x%03X)", can_id);
        #endif
        return;
    }

    updateCANCache(can_id, identifier, &data[data_offset], data_length);
}

/**
 * Update CAN input - poll for incoming frames and populate cache
 * Called from main loop - non-blocking
 * Uses HAL for platform abstraction.
 *
 * Supports:
 * - OBD-II responses (Mode 0x41) - extracts PID from byte[2]
 * - J1939 and custom protocols - uses byte[0] as identifier
 * - Any CAN ID (not limited to 0x7E8)
 */
void updateCANInput() {
    // Check if input is enabled and initialized
    if (!canInputInitialized || systemConfig.buses.can_input_mode == CAN_INPUT_OFF) {
        return;
    }

    // Poll for frames via HAL (handles all platforms)
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
    bool extended;

    while (hal::can::read(id, data, len, extended, canInputBus)) {
        processCANFrame(id, data, len);
    }
}
