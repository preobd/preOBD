/*
 * can_scan.cpp - CAN Bus Scanning State Machine
 *
 * Scans the CAN bus for active PIDs and displays results.
 * Integrates with CAN frame cache to collect traffic.
 */

#include "can_scan.h"
#include "can_frame_cache.h"
#include "../../../lib/can_sensor_library/standard_pids.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static CANScanState scanState = SCAN_IDLE;
static CANScanResult scanResults[MAX_SCAN_RESULTS];
static uint8_t scanResultCount = 0;
static uint32_t scanStartTime = 0;
static uint16_t scanDuration = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Display scan results to user
 */
static void displayScanResults() {
    msg.control.println(F("\n=== CAN Scan Complete ==="));

    if (scanResultCount == 0) {
        msg.control.println(F("No PIDs detected during scan period."));
        msg.control.println(F("Ensure CAN input is enabled and bus is active."));
        return;
    }

    msg.control.print(F("Found "));
    msg.control.print(scanResultCount);
    msg.control.println(F(" PIDs:\n"));
    msg.control.println(F("  PID   Name                    CAN ID  Len  Samples"));
    msg.control.println(F("  ----- ----------------------- ------- ---- --------"));

    for (uint8_t i = 0; i < scanResultCount; i++) {
        // Lookup standard name
        const StandardPIDInfo* info = lookupStandardPID(scanResults[i].pid);

        char name[32];
        if (info) {
            // Copy name from PROGMEM
            strncpy_P(name, info->name, 31);
            name[31] = '\0';
        } else {
            sprintf(name, "Unknown PID");
        }

        // Format output manually (no printf)
        msg.control.print(F("  0x"));
        if (scanResults[i].pid < 0x10) msg.control.print('0');
        msg.control.print(scanResults[i].pid, HEX);
        msg.control.print(F("  "));
        msg.control.print(name);

        // Pad to align columns (23 chars for name)
        uint8_t nameLen = strlen(name);
        for (uint8_t j = nameLen; j < 23; j++) {
            msg.control.print(' ');
        }

        msg.control.print(F(" 0x"));
        if (scanResults[i].can_id < 0x100) msg.control.print('0');
        if (scanResults[i].can_id < 0x10) msg.control.print('0');
        msg.control.print(scanResults[i].can_id, HEX);
        msg.control.print(F("   "));
        msg.control.print(scanResults[i].data_length);
        msg.control.print(F("    "));
        msg.control.println(scanResults[i].sample_count);
    }

    msg.control.println(F("\nTo import a PID: SET CAN <pid_hex>"));
    msg.control.println(F("Example: SET CAN 0x0C (imports Engine RPM)"));
    msg.control.println(F("\nType 'SCAN CANCEL' to clear results."));
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void startCANScan(uint16_t duration_ms) {
    // Reset state
    scanState = SCAN_LISTENING;
    scanResultCount = 0;
    scanStartTime = millis();
    scanDuration = duration_ms;
    memset(scanResults, 0, sizeof(scanResults));

    msg.control.print(F("Scanning CAN bus for "));
    msg.control.print(duration_ms);
    msg.control.println(F(" ms..."));
    msg.control.println(F("Listening for all CAN frames..."));
}

void updateCANScan() {
    if (scanState != SCAN_LISTENING) {
        return;
    }

    // Check if scan period has elapsed
    if (millis() - scanStartTime > scanDuration) {
        scanState = SCAN_DISPLAYING;
        displayScanResults();
        return;
    }

    // Collect PIDs from cache
    // Iterate through cache and add new PIDs to results
    for (uint8_t i = 0; i < CAN_CACHE_SIZE; i++) {
        // Access cache entry
        CANFrameEntry entry;

        // Get pointer to cache entry (external linkage)
        extern CANFrameEntry canFrameCache[CAN_CACHE_SIZE];
        memcpy(&entry, &canFrameCache[i], sizeof(CANFrameEntry));

        if (!entry.valid) continue;

        // Check if PID already in results
        bool found = false;
        for (uint8_t j = 0; j < scanResultCount; j++) {
            if (scanResults[j].can_id == entry.can_id &&
                scanResults[j].pid == entry.pid) {
                // Update sample count and last data
                scanResults[j].sample_count++;
                memcpy(scanResults[j].last_data, entry.data, 8);
                found = true;
                break;
            }
        }

        // Add new PID if space available
        if (!found && scanResultCount < MAX_SCAN_RESULTS) {
            scanResults[scanResultCount].can_id = entry.can_id;
            scanResults[scanResultCount].pid = entry.pid;
            // NOTE: Cache doesn't store data_length, so we default to 8
            // Standard PIDs define actual data_length in standard_pids.h
            scanResults[scanResultCount].data_length = 8;
            scanResults[scanResultCount].sample_count = 1;
            memcpy(scanResults[scanResultCount].last_data, entry.data, 8);
            scanResultCount++;
        }
    }
}

CANScanState getCANScanState() {
    return scanState;
}

const CANScanResult* getCANScanResults(uint8_t* count) {
    if (count) {
        *count = scanResultCount;
    }
    return scanResults;
}

void cancelCANScan() {
    scanState = SCAN_IDLE;
    scanResultCount = 0;
    msg.control.println(F("CAN scan cancelled."));
}
