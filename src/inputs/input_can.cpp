/*
 * input_can.cpp - CAN Input Subsystem
 *
 * Separate CAN input subsystem - independent from CAN output.
 * Receives frames from configured input CAN bus and populates frame cache.
 * Supports OBD-II, J1939, and custom CAN protocols.
 */

#include <Arduino.h>
#include "input_can.h"
#include "../lib/system_config.h"
#include "../lib/bus_config.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "sensors/can/can_frame_cache.h"

// Platform-specific CAN library includes
#if defined(TEENSY_40) || defined(TEENSY_41) || defined(TEENSY_36)
    #define USING_FLEXCAN
    #include <FlexCAN_T4.h>

    // FlexCAN_T4 instances (defined in output_can.cpp)
    extern FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
    #if defined(CAN2)
        extern FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> Can1;
    #endif
    #if defined(CAN3)
        extern FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> Can2;
    #endif

#elif defined(ESP32)
    #define USING_ESP32_TWAI
    #include <ESP32-TWAI-CAN.hpp>

    // ESP32-TWAI instance (defined in output_can.cpp)
    extern ESP32Can Can0;

#else
    // MCP2515 via SPI (Arduino Mega, Uno, etc.)
    #define USING_MCP2515
    #include <CAN.h>
    // CAN library uses global CAN object
#endif

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool canInputInitialized = false;

// ============================================================================
// BUS ABSTRACTION HELPER
// ============================================================================

// No helper needed - use switch statements to avoid base class pointer issues

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize CAN input subsystem
 * Sets up input CAN bus based on systemConfig.buses.input_can_bus
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

    #ifdef USING_FLEXCAN
        // Initialize specific bus (template instances don't share base class methods)
        switch (bus) {
            case 0:
                Can0.begin();
                Can0.setBaudRate(baudrate);
                Can0.setMaxMB(16);
                for (int i = 0; i < 8; i++) {
                    Can0.setMB((FLEXCAN_MAILBOX)i, RX, STD);
                }
                break;
            #if defined(CAN2)
            case 1:
                Can1.begin();
                Can1.setBaudRate(baudrate);
                Can1.setMaxMB(16);
                for (int i = 0; i < 8; i++) {
                    Can1.setMB((FLEXCAN_MAILBOX)i, RX, STD);
                }
                break;
            #endif
            #if defined(CAN3)
            case 2:
                Can2.begin();
                Can2.setBaudRate(baudrate);
                Can2.setMaxMB(16);
                for (int i = 0; i < 8; i++) {
                    Can2.setMB((FLEXCAN_MAILBOX)i, RX, STD);
                }
                break;
            #endif
            default:
                canInputInitialized = false;
                return false;
        }

        msg.debug.info(TAG_CAN, "CAN input initialized on CAN%d (%lu bps)", bus + 1, baudrate);

    #elif defined(USING_ESP32_TWAI)
        // ESP32 uses single CAN bus (Can0)
        if (bus != 0) {
            canInputInitialized = false;
            return false;
        }

        // Initialize if not already done
        if (!Can0.begin(baudrate, GPIO_NUM_21, GPIO_NUM_22)) {
            canInputInitialized = false;
            return false;
        }

    #else  // MCP2515
        // MCP2515 uses single CAN bus via SPI
        if (bus != 0) {
            canInputInitialized = false;
            return false;
        }

        // Initialize if not already done
        if (!CAN.begin(baudrate)) {
            canInputInitialized = false;
            return false;
        }
    #endif

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
    } else if (len >= 2 && data[1] == 0x41) {
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

    uint8_t bus = systemConfig.buses.input_can_bus;
    if (bus == 0xFF) return;

    #ifdef USING_FLEXCAN
        CAN_message_t msg;

        // Poll specific bus for frames
        switch (bus) {
            case 0:
                while (Can0.read(msg)) {
                    processCANFrame(msg.id, msg.buf, msg.len);
                }
                break;
            #if defined(CAN2)
            case 1:
                while (Can1.read(msg)) {
                    processCANFrame(msg.id, msg.buf, msg.len);
                }
                break;
            #endif
            #if defined(CAN3)
            case 2:
                while (Can2.read(msg)) {
                    processCANFrame(msg.id, msg.buf, msg.len);
                }
                break;
            #endif
        }

    #elif defined(USING_ESP32_TWAI)
        if (bus != 0) return;  // ESP32 only has one CAN bus

        CanFrame frame;
        while (Can0.readFrame(frame, 0)) {  // Non-blocking read
            processCANFrame(frame.identifier, frame.data, frame.data_length_code);
        }

    #else  // MCP2515
        if (bus != 0) return;  // MCP2515 only has one CAN bus

        int packetSize = CAN.parsePacket();
        if (packetSize > 0) {
            uint32_t can_id = CAN.packetId();
            uint8_t buf[8];
            uint8_t len = 0;

            while (CAN.available() && len < 8) {
                buf[len++] = CAN.read();
            }

            processCANFrame(can_id, buf, len);
        }
    #endif
}
