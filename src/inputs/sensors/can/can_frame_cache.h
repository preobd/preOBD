/*
 * can_frame_cache.h - CAN Frame Cache for Imported Sensors
 *
 * Circular buffer to cache incoming CAN frames by (CAN_ID, PID) pair.
 * Used by CAN-imported sensors to retrieve cached data without blocking.
 *
 * Architecture:
 * - Fixed-size cache (16 entries) with hash-based indexing
 * - LRU (Least Recently Used) replacement on collision
 * - Timeout detection for stale data (2000ms default)
 * - Thread-safe for single-threaded Arduino environment
 */

#ifndef CAN_FRAME_CACHE_H
#define CAN_FRAME_CACHE_H

#include <Arduino.h>

// ===== CONFIGURATION =====
#define CAN_CACHE_SIZE 16           // Must be power of 2 for efficient modulo
#define CAN_DEFAULT_TIMEOUT_MS 2000 // Default stale timeout

// ===== DATA STRUCTURES =====

/**
 * CAN Frame Cache Entry
 * Stores a single cached CAN frame indexed by (CAN_ID, PID)
 * Size: ~13 bytes per entry
 */
struct CANFrameEntry {
    uint16_t can_id;        // CAN identifier (0x7E8 for OBD-II, 0x400+ for J1939, etc.)
    uint8_t pid;            // PID or identifier byte (OBD-II PID or custom protocol ID)
    uint8_t data[8];        // Full 8-byte CAN frame data payload
    uint32_t timestamp_ms;  // millis() when frame was last updated
    bool valid;             // Entry is populated and valid
};

// ===== GLOBAL CACHE =====
extern CANFrameEntry canFrameCache[CAN_CACHE_SIZE];

// ===== API FUNCTIONS =====

/**
 * Initialize CAN frame cache
 * Clears all entries and marks them as invalid
 * Call during system startup
 */
void initCANFrameCache();

/**
 * Update cache with incoming CAN frame
 * Uses hash-based indexing with LRU replacement on collision
 *
 * @param can_id    CAN identifier
 * @param pid       PID or identifier byte
 * @param data      Pointer to data payload (up to 8 bytes)
 * @param len       Data length (1-8 bytes)
 */
void updateCANCache(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len);

/**
 * Get cached CAN frame entry
 * Returns pointer to cache entry or nullptr if not found
 *
 * @param can_id    CAN identifier to lookup
 * @param pid       PID to lookup
 * @return          Pointer to CANFrameEntry or nullptr if not found/invalid
 */
CANFrameEntry* getCANCacheEntry(uint16_t can_id, uint8_t pid);

/**
 * Check if cached data is stale (timed out)
 *
 * @param entry         Pointer to cache entry
 * @param timeout_ms    Timeout in milliseconds (default: CAN_DEFAULT_TIMEOUT_MS)
 * @return              true if stale (millis() - timestamp > timeout)
 */
bool isCANDataStale(CANFrameEntry* entry, uint32_t timeout_ms = CAN_DEFAULT_TIMEOUT_MS);

/**
 * Clear specific cache entry
 *
 * @param can_id    CAN identifier
 * @param pid       PID
 */
void clearCANCacheEntry(uint16_t can_id, uint8_t pid);

/**
 * Clear all cache entries
 * Equivalent to initCANFrameCache()
 */
void clearCANCache();

#endif // CAN_FRAME_CACHE_H
