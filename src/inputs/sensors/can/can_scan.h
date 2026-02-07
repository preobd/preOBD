/*
 * can_scan.h - CAN Bus Scanning State Machine
 *
 * Interactive CAN bus scanning to detect available PIDs.
 * Collects frames during scan period and displays results.
 */

#ifndef CAN_SCAN_H
#define CAN_SCAN_H

#include <Arduino.h>

// ============================================================================
// SCAN STATE MACHINE
// ============================================================================

enum CANScanState {
    SCAN_IDLE,          // Not scanning
    SCAN_LISTENING,     // Actively listening for frames
    SCAN_DISPLAYING     // Displaying results, awaiting user input
};

// ============================================================================
// SCAN RESULT STRUCTURE
// ============================================================================

#define MAX_SCAN_RESULTS 32

struct CANScanResult {
    uint16_t can_id;        // CAN identifier
    uint8_t pid;            // PID or identifier byte
    uint8_t data_length;    // Number of data bytes
    uint32_t sample_count;  // Number of times this PID was seen
    uint8_t last_data[8];   // Most recent data payload
};

// ============================================================================
// EXPORTED FUNCTIONS
// ============================================================================

/**
 * Start CAN scan for specified duration
 * @param duration_ms   How long to listen (milliseconds)
 */
void startCANScan(uint16_t duration_ms);

/**
 * Update CAN scan state machine
 * Called from main loop during CONFIG mode
 */
void updateCANScan();

/**
 * Get current scan state
 * @return Current state (IDLE, LISTENING, DISPLAYING)
 */
CANScanState getCANScanState();

/**
 * Get scan results array
 * @param count   Output: number of results
 * @return Pointer to results array
 */
const CANScanResult* getCANScanResults(uint8_t* count);

/**
 * Cancel/reset scan
 */
void cancelCANScan();

#endif // CAN_SCAN_H
