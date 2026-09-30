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
    SCAN_QUERYING,      // SCAN PIDS: asking the ECUs for their supported PIDs (#511)
    SCAN_DONE           // Window closed; results kept until the next scan / CANCEL
};

// ============================================================================
// SCAN RESULT STRUCTURE
// ============================================================================

// MAX_SCAN_RESULTS set per board in boards/<board>/board.h.

struct CANScanResult {
    uint16_t can_id;        // CAN identifier
    uint8_t pid;            // PID or identifier byte
    uint8_t data_length;    // Payload bytes (OBD: from the ISO-TP length byte)
    uint32_t sample_count;  // Frames received for this (can_id, pid)
    uint8_t last_data[8];   // Most recent data payload
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
    uint32_t last_seen_ms;  // millis() of the most recent frame (SCAN RESULTS JSON)
#endif
};

// ============================================================================
// EXPORTED FUNCTIONS
// ============================================================================

/**
 * Start CAN scan for specified duration. `quiet` suppresses the text table
 * printed when the window closes (SCAN CAN ... JSON: a UI reads the results
 * with SCAN RESULTS JSON instead, and the unsolicited table would land in
 * front of its next reply).
 */
void startCANScan(uint16_t duration_ms, bool quiet = false);

/**
 * Record one received frame (#510). Called from CAN input's frame handler for
 * every frame it parses, so counts are true frame counts and no ID is missed
 * when the frame cache is smaller than MAX_SCAN_RESULTS. No-op unless a scan
 * is listening.
 */
void canScanOnFrame(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len,
                    uint32_t now);

/**
 * Update CAN scan state machine: closes the window when the duration elapses.
 * Called from main loop.
 */
void updateCANScan();

CANScanState getCANScanState();

/**
 * Scan results array; *count receives the number of entries.
 */
const CANScanResult* getCANScanResults(uint8_t* count);

/** Increments per startCANScan() (0 = never scanned). */
uint8_t getCANScanId();

/** Window length and ms left in it (0 unless listening). */
uint16_t getCANScanDuration();
uint32_t getCANScanRemainingMs(uint32_t now);

/**
 * Cancel/clear scan results
 */
void cancelCANScan();

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
#include "can_discovery.h"

/**
 * SCAN PIDS (#511): ask the ECUs which Mode 01 PIDs they support. Shares the
 * scan slot with SCAN CAN (clears its results, bumps the scan ID) but
 * collects supported-PID bitmaps instead of frames. CAN input does the
 * transmitting: it asks canScanDiscoveryDue() each loop and reports answers
 * through canScanOnBitmap(). The caller checks the input is running NORMAL.
 */
void startCANPidScan(bool quiet);

/** True while SCAN PIDS is querying: CAN input pauses its own polling. */
bool canScanQuerying();

/** Should the bitmap request for block `base` go out now? (see can_discovery.h) */
bool canScanDiscoveryDue(uint32_t now, uint8_t& base);
void canScanDiscoverySent(uint32_t now);

/** A Mode 01 answer; true if it was a bitmap the discovery consumed. */
bool canScanOnBitmap(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len);

/** End a discovery early (CAN input left NORMAL mode). */
void canScanDiscoveryStop();

/** The last SCAN PIDS result, or nullptr if the last scan was SCAN CAN. */
const CanDiscState* getCANScanDiscovery();
#endif

#endif // CAN_SCAN_H
