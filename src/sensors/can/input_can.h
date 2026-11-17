/*
 * input_can.h - CAN Input Subsystem
 *
 * Separate subsystem for receiving CAN frames and populating the frame cache.
 * Completely independent from CAN output (output_can.cpp).
 *
 * Architecture:
 * - Uses input_can_bus from systemConfig.buses
 * - Polls for incoming frames without blocking
 * - Updates canFrameCache for readCANSensor() to consume
 * - Supports any CAN ID (OBD-II, J1939, custom protocols)
 */

#ifndef INPUT_CAN_H
#define INPUT_CAN_H

#include <Arduino.h>

/**
 * Initialize CAN input subsystem
 *
 * Uses systemConfig.buses.input_can_bus to determine which CAN bus to initialize.
 * Only initializes if can_input_mode is NORMAL or LISTEN (not OFF).
 *
 * @return true if initialization successful or disabled, false on error
 */
bool initCANInput();

/**
 * Update CAN input - poll for incoming frames
 *
 * Called from main loop. Non-blocking - reads all available frames and returns.
 * Populates canFrameCache with received data for CAN sensors to consume.
 *
 * Supports:
 * - OBD-II Mode 01 responses (0x41), 1-5 data bytes - PID is the identifier
 * - Custom protocols - uses byte[0] as identifier
 * - Any CAN ID (not limited to 0x7E8)
 *
 * In NORMAL (ENABLE) mode it also requests each imported OBD-II PID
 * (source ID 0x7E8-0x7EF) round-robin with a Mode 01 request, so a lone
 * preOBD on an ECU bus gets data (#476). LISTEN never transmits.
 */
void updateCANInput();

/**
 * Shutdown CAN input subsystem
 * Disables CAN input bus
 */
void shutdownCANInput();

/**
 * Is CAN input reading `bus`? On a bus shared with CAN output, input's
 * updateCANInput() is the only reader: two drain loops on one controller
 * each discard the other's frames (#495). Output checks this and, instead
 * of reading, registers a handler for the OBD-II requests input drains.
 */
bool canInputOwnsBus(uint8_t bus);

/** Is CAN input initialized and not switched OFF at runtime? */
bool canInputIsRunning();

/**
 * OBD-II request frames (11-bit 0x7DF / 0x7E0) that input reads are passed
 * here instead of being cached. nullptr (the default) caches them as before.
 */
typedef void (*CANRequestHandler)(uint32_t id, const uint8_t* data, uint8_t len);
void setCANRequestHandler(CANRequestHandler handler);

#if SUPPORTS_JSON_EXPORT
struct Input;
struct CanBusSnapshot;

/**
 * May CAN input transmit requests now? NORMAL both as configured and as
 * brought up at boot: after BUS CAN INPUT ... ENABLE on a LISTEN boot the
 * controller stays listen-only until a reboot (SCAN PIDS, #511).
 */
bool canInputRunsNormal();

/** Would the poller request this input's PID (NORMAL mode, OBD source, #476)? */
bool canInputIsPolled(const Input& in);

/**
 * Fill BUS CAN JSON's live input fields (#510): running/shared, frame and
 * poll counters, backoff, and rebootPending. The caller fills the configured
 * bus/mode/baud (s.inBus/inMode/inBaudrate) from systemConfig FIRST —
 * rebootPending compares them with what was brought up at boot.
 */
void getCANInputStatus(CanBusSnapshot& s, uint32_t now);
#endif

#endif // INPUT_CAN_H
