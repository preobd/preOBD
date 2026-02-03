/*
 * can_frame_cache.cpp - CAN Frame Cache Implementation
 *
 * IMPORTANT: All cache operations assume single-threaded access from main loop.
 * Do NOT call updateCANCache() from interrupt handlers without adding
 * interrupt guards (noInterrupts()/interrupts()).
 */

#include "can_frame_cache.h"
#include <string.h>

// ===== GLOBAL CACHE =====
CANFrameEntry canFrameCache[CAN_CACHE_SIZE];

// ===== HELPER FUNCTIONS =====

/**
 * Hash function for (CAN_ID, PID) pair
 * Simple XOR hash with modulo for cache indexing
 *
 * @param can_id    CAN identifier
 * @param pid       PID byte
 * @return          Cache index (0 to CAN_CACHE_SIZE-1)
 */
static inline uint8_t hashCANFrame(uint16_t can_id, uint8_t pid) {
    return (can_id ^ pid) & (CAN_CACHE_SIZE - 1);  // Efficient modulo for power-of-2
}

/**
 * Find least recently used entry for replacement
 * Used when hash collision occurs
 *
 * @return  Index of oldest entry
 */
static uint8_t findLRUEntry() {
    uint32_t oldest_time = 0xFFFFFFFF;
    uint8_t oldest_index = 0;

    for (uint8_t i = 0; i < CAN_CACHE_SIZE; i++) {
        if (!canFrameCache[i].valid) {
            return i;  // Empty slot found
        }
        if (canFrameCache[i].timestamp_ms < oldest_time) {
            oldest_time = canFrameCache[i].timestamp_ms;
            oldest_index = i;
        }
    }

    return oldest_index;
}

// ===== API IMPLEMENTATION =====

void initCANFrameCache() {
    memset(canFrameCache, 0, sizeof(canFrameCache));
    for (uint8_t i = 0; i < CAN_CACHE_SIZE; i++) {
        canFrameCache[i].valid = false;
    }
}

void updateCANCache(uint16_t can_id, uint8_t pid, const uint8_t* data, uint8_t len) {
    // Validate input parameters
    // NOTE: Caller MUST ensure 'data' buffer has at least 'len' bytes available
    // Current callers (updateCANInput, CAN scan) always provide 8-byte buffers
    if (!data || len == 0 || len > 8) return;

    uint8_t index = hashCANFrame(can_id, pid);
    uint8_t copy_len = min(len, (uint8_t)8);

    // Check if entry already exists at hash index
    if (canFrameCache[index].valid &&
        canFrameCache[index].can_id == can_id &&
        canFrameCache[index].pid == pid) {
        // Update existing entry
        memcpy(canFrameCache[index].data, data, copy_len);
        canFrameCache[index].timestamp_ms = millis();
        return;
    }

    // Hash collision - check if this (can_id, pid) exists elsewhere
    for (uint8_t i = 0; i < CAN_CACHE_SIZE; i++) {
        if (canFrameCache[i].valid &&
            canFrameCache[i].can_id == can_id &&
            canFrameCache[i].pid == pid) {
            // Found existing entry, update it
            memcpy(canFrameCache[i].data, data, copy_len);
            canFrameCache[i].timestamp_ms = millis();
            return;
        }
    }

    // New entry - use LRU replacement if hash slot occupied
    if (canFrameCache[index].valid) {
        index = findLRUEntry();
    }

    // Store new entry
    canFrameCache[index].can_id = can_id;
    canFrameCache[index].pid = pid;
    memcpy(canFrameCache[index].data, data, copy_len);
    canFrameCache[index].timestamp_ms = millis();
    canFrameCache[index].valid = true;
}

CANFrameEntry* getCANCacheEntry(uint16_t can_id, uint8_t pid) {
    // Fast path: check hash index first
    uint8_t index = hashCANFrame(can_id, pid);
    if (canFrameCache[index].valid &&
        canFrameCache[index].can_id == can_id &&
        canFrameCache[index].pid == pid) {
        return &canFrameCache[index];
    }

    // Slow path: linear search for hash collision resolution
    for (uint8_t i = 0; i < CAN_CACHE_SIZE; i++) {
        if (canFrameCache[i].valid &&
            canFrameCache[i].can_id == can_id &&
            canFrameCache[i].pid == pid) {
            return &canFrameCache[i];
        }
    }

    return nullptr;  // Not found
}

bool isCANDataStale(CANFrameEntry* entry, uint32_t timeout_ms) {
    if (!entry || !entry->valid) return true;

    uint32_t elapsed = millis() - entry->timestamp_ms;

    // Handle millis() rollover (occurs every ~49 days)
    if (elapsed > timeout_ms && elapsed < 0x80000000UL) {
        return true;  // Stale
    }

    return false;  // Fresh
}

void clearCANCacheEntry(uint16_t can_id, uint8_t pid) {
    CANFrameEntry* entry = getCANCacheEntry(can_id, pid);
    if (entry) {
        entry->valid = false;
    }
}

void clearCANCache() {
    initCANFrameCache();
}
