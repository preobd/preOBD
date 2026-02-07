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
 * - OBD-II Mode 01 responses (0x41) - extracts PID from byte[2]
 * - Custom protocols - uses byte[0] as identifier
 * - Any CAN ID (not limited to 0x7E8)
 */
void updateCANInput();

/**
 * Shutdown CAN input subsystem
 * Disables CAN input bus
 */
void shutdownCANInput();

#endif // INPUT_CAN_H
