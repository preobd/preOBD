/*
 * can_scan.cpp - CAN Bus Scanning State Machine
 *
 * Scans the CAN bus for active PIDs and displays results. CAN input's frame
 * handler reports every parsed frame through canScanOnFrame() (#510); the
 * scan used to sample the frame cache each loop instead, which counted loop
 * passes rather than frames, lost the payload length (always 8), and missed
 * IDs once more were active than the cache holds.
 */

#include "can_scan.h"
#include "../../can_sensor_library/standard_pids.h"
#include "../../../core/message_api.h"
#include "../../../core/log_tags.h"
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static CANScanState scanState = SCAN_IDLE;
static CANScanResult scanResults[MAX_SCAN_RESULTS];
static uint8_t scanResultCount = 0;
static uint32_t scanStartTime = 0;
static uint16_t scanDuration = 0;
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
static uint8_t scanId = 0;        // SCAN STATUS JSON: tells a restarted scan apart
static bool scanQuiet = false;    // SCAN CAN ... JSON: no text table at the end
static CanDiscState disc;         // SCAN PIDS (#511)
static bool scanIsPids = false;   // The current/last scan was SCAN PIDS
#endif

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

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
    msg.control.println(F("  PID   Name                    CAN ID  Len  Frames"));
    msg.control.println(F("  ----- ----------------------- ------- ---- --------"));

    for (uint8_t i = 0; i < scanResultCount; i++) {
        // Lookup standard name
        const StandardPIDInfo* info = lookupStandardPID(scanResults[i].pid);

        char name[32];
        if (info) {
            // Copy name from PROGMEM: the name pointer itself lives in the
            // PROGMEM row, so read it with pgm_read_ptr (#496 — reading
            // info->name directly gave a garbage name on AVR)
            strncpy_P(name, (const char*)pgm_read_ptr(&info->name), 31);
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

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
/** Copy a standard PID's name from PROGMEM, or "" when it is not in the table. */
static FLASHMEM void standardPidName(uint8_t pid, char* name, size_t size) {
    const StandardPIDInfo* info = lookupStandardPID(pid);
    name[0] = '\0';
    if (!info) return;
    strncpy_P(name, (const char*)pgm_read_ptr(&info->name), size - 1);
    name[size - 1] = '\0';
}

/** SCAN PIDS text result: each answering ECU and the PIDs it supports. */
static FLASHMEM void displayDiscoveryResults() {
    msg.control.println(F("\n=== Supported PIDs ==="));
    if (disc.ecuMask == 0) {
        msg.control.println(F("No ECU answered. Is the ignition on?"));
        return;
    }
    for (uint8_t e = 0; e < CAN_DISC_MAX_ECUS; e++) {
        if (!(disc.ecuMask & (1 << e))) continue;
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%03X", 0x7E8 + e);   // HEX prints lowercase on Teensy
        msg.control.print(F("\nECU "));
        msg.control.print(hex);
        msg.control.print(F(": "));
        msg.control.print(canDiscCountEcu(disc, e));
        msg.control.println(F(" PIDs"));
        for (uint16_t p = 1; p <= 0xFF; p++) {
            if (!canDiscSupports(disc, (uint16_t)(0x7E8 + e), (uint8_t)p)) continue;
            char name[32];
            standardPidName((uint8_t)p, name, sizeof(name));
            snprintf(hex, sizeof(hex), "0x%02X", (unsigned)p);
            msg.control.print(F("  "));
            msg.control.print(hex);
            msg.control.print(F("  "));
            msg.control.println(name[0] ? name : "(not in preOBD's PID table)");
        }
    }
    msg.control.println(F("\nTo import a PID: SET CAN <pid_hex> [ecu]"));
    msg.control.println(F("  (ecu only for a PID another ECU lists, e.g. SET CAN 0x05 0x7E9)"));
}
#endif

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
FLASHMEM void startCANPidScan(bool quiet) {
    scanState = SCAN_QUERYING;
    scanResultCount = 0;
    scanStartTime = millis();
    scanDuration = 0;
    memset(scanResults, 0, sizeof(scanResults));
    scanId++;
    if (scanId == 0) scanId = 1;
    scanQuiet = quiet;
    scanIsPids = true;
    canDiscStart(disc);
    msg.control.println(F("Asking the ECUs for their supported PIDs..."));
}

bool canScanQuerying() {
    return scanState == SCAN_QUERYING;
}

bool canScanDiscoveryDue(uint32_t now, uint8_t& base) {
    if (scanState != SCAN_QUERYING) return false;
    return canDiscDue(disc, now, base);
}

void canScanDiscoverySent(uint32_t now) {
    canDiscOnSent(disc, now);
}

bool canScanOnBitmap(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len) {
    if (scanState != SCAN_QUERYING) return false;
    if (canDiscOnAnswer(disc, can_id, pid, data, len)) return true;
    // A bitmap that missed its window is still no sensor value: keep it out
    // of the frame cache too
    return can_id >= 0x7E8 && can_id <= 0x7EF && canDiscIsBitmapPid(pid);
}

void canScanDiscoveryStop() {
    if (canDiscActive(disc)) disc.phase = CAN_DISC_DONE;   // updateCANScan finishes it
}

const CanDiscState* getCANScanDiscovery() {
    return scanIsPids ? &disc : nullptr;
}
#endif

void startCANScan(uint16_t duration_ms, bool quiet) {
    // Reset state
    scanState = SCAN_LISTENING;
    scanResultCount = 0;
    scanStartTime = millis();
    scanDuration = duration_ms;
    memset(scanResults, 0, sizeof(scanResults));
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
    scanId++;
    if (scanId == 0) scanId = 1;   // 0 means "never scanned"
    scanQuiet = quiet;
    scanIsPids = false;
    canDiscReset(disc);
#else
    (void)quiet;
#endif

    msg.control.print(F("Scanning CAN bus for "));
    msg.control.print(duration_ms);
    msg.control.println(F(" ms..."));
    msg.control.println(F("Listening for all CAN frames..."));
}

void updateCANScan() {
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
    if (scanState == SCAN_QUERYING && !canDiscActive(disc)) {
        scanState = SCAN_DONE;
        if (!scanQuiet) displayDiscoveryResults();
        return;
    }
#endif
    if (scanState != SCAN_LISTENING) {
        return;
    }
    if (millis() - scanStartTime > scanDuration) {
        scanState = SCAN_DONE;
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
        if (scanQuiet) return;
#endif
        displayScanResults();
    }
}

void canScanOnFrame(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len,
                    uint32_t now) {
    if (scanState != SCAN_LISTENING) return;
    if (len > 8) len = 8;

    for (uint8_t j = 0; j < scanResultCount; j++) {
        if (scanResults[j].can_id == can_id && scanResults[j].pid == pid) {
            scanResults[j].sample_count++;
            scanResults[j].data_length = len;
            memcpy(scanResults[j].last_data, data, len);
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
            scanResults[j].last_seen_ms = now;
#endif
            return;
        }
    }
    if (scanResultCount < MAX_SCAN_RESULTS) {
        CANScanResult& r = scanResults[scanResultCount++];
        r.can_id = can_id;
        r.pid = pid;
        r.data_length = len;
        r.sample_count = 1;
        memset(r.last_data, 0, sizeof(r.last_data));
        memcpy(r.last_data, data, len);
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
        r.last_seen_ms = now;
#else
        (void)now;
#endif
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

uint8_t getCANScanId() {
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
    return scanId;
#else
    return 0;
#endif
}

uint16_t getCANScanDuration() {
    return scanDuration;
}

uint32_t getCANScanRemainingMs(uint32_t now) {
    if (scanState != SCAN_LISTENING) return 0;
    uint32_t elapsed = now - scanStartTime;
    return elapsed >= scanDuration ? 0 : scanDuration - elapsed;
}

void cancelCANScan() {
    scanState = SCAN_IDLE;
    scanResultCount = 0;
#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
    scanIsPids = false;
    canDiscReset(disc);
#endif
    msg.control.println(F("CAN scan cancelled."));
}
