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
    // Check if input is enabled
    if (!systemConfig.buses.can_input_enabled) {
        canInputInitialized = false;
        return false;
    }

    uint8_t bus = systemConfig.buses.input_can_bus;
    if (bus == 0xFF) {
        canInputInitialized = false;
        return false;  // No input bus configured
    }

    uint32_t baudrate = systemConfig.buses.can_baudrate;

    // Initialize via HAL (handles all platforms)
    if (!hal::can::begin(baudrate, bus)) {
        msg.debug.error(TAG_CAN, "CAN input init failed on bus %d", bus);
        canInputInitialized = false;
        return false;
    }

    canInputBus = bus;
    msg.debug.info(TAG_CAN, "CAN input initialized on bus %d (%lu bps)", bus, baudrate);

    // Initialize CAN frame cache
    initCANFrameCache();

    canInputInitialized = true;
    return true;
}

/**
 * Shutdown CAN input subsystem
 * Does NOT disable the CAN bus hardware (output may still be using it)
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
    } else {
        // Custom protocol or J1939 - use first byte as identifier
        identifier = data[0];
        data_offset = 0;
    }

    // Update cache with extracted data
    uint8_t data_length = (len > data_offset) ? (len - data_offset) : 0;
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
    if (!canInputInitialized || !systemConfig.buses.can_input_enabled) {
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
