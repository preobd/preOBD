/*
 * can_discovery.h - OBD-II supported-PID discovery (SCAN PIDS, #511)
 *
 * Pure, header-only, like can_poll.h: the supported-PID bitmap walk. No
 * Arduino/HAL deps, so the native test build exercises it directly
 * (test/test_can_discovery). The transmit lives in input_can.cpp.
 *
 * Mode 01 PID 0x00 answers with a 4-byte bitmap A B C D of PIDs 0x01-0x20.
 * Bits count MSB-first across the four bytes: PID = base + 1 + i, so bit 7
 * of A is base+0x01 and bit 0 of D is base+0x20. That last PID is itself the
 * next bitmap, so its bit says "ask PID base+0x20 for more"; the walk
 * continues up to base 0xE0.
 *
 * Each block is requested functionally (0x7DF), so every ECU (0x7E8-0x7EF)
 * answers the blocks it supports. An ECU without a block stays silent or
 * sends a negative response, which the frame path drops. Because several
 * ECUs answer, a block's window runs its full length rather than closing on
 * the first answer.
 *
 * Only the bitmaps are collected: what the car supports. The values follow
 * once a PID is imported and the poller requests it.
 */

#ifndef CAN_DISCOVERY_H
#define CAN_DISCOVERY_H

#include <stdint.h>
#include <string.h>

#define CAN_DISC_WINDOW_MS   100   // Collect answers to one bitmap block for this long
#define CAN_DISC_TRIES       3     // Requests for PID 0x00 before giving up
#define CAN_DISC_MAX_ECUS    8     // 0x7E8-0x7EF
#define CAN_DISC_LAST_BASE   0xE0  // Highest bitmap PID (lists 0xE1-0xFF)

enum CanDiscPhase : uint8_t {
    CAN_DISC_IDLE,
    CAN_DISC_QUERYING,
    CAN_DISC_DONE
};

struct CanDiscState {
    uint8_t  supported[CAN_DISC_MAX_ECUS][32];  // bit (0x80 >> (pid & 7)) of byte pid >> 3
    uint32_t sentMs;        // When the current block's request went out
    uint8_t  phase;         // CanDiscPhase
    uint8_t  base;          // Bitmap block being requested: 0x00, 0x20, ... 0xE0
    uint8_t  tries;         // Requests sent for this block
    uint8_t  ecuMask;       // ECUs that answered any block (bit n = 0x7E8 + n)
    bool     sent;          // Current block's request is out, window open
    bool     more;          // An ECU set the continuation bit for this block
};

/** True for the bitmap PIDs 0x00, 0x20, ... 0xE0: never a sensor value. */
static inline bool canDiscIsBitmapPid(uint8_t pid) {
    return (pid & 0x1F) == 0;
}

static inline void canDiscReset(CanDiscState& s) {
    memset(&s, 0, sizeof(s));
}

static inline void canDiscStart(CanDiscState& s) {
    canDiscReset(s);
    s.phase = CAN_DISC_QUERYING;
}

static inline bool canDiscActive(const CanDiscState& s) {
    return s.phase == CAN_DISC_QUERYING;
}

/** Did ECU `responseId` list `pid` as supported? Bitmap PIDs never count. */
static inline bool canDiscSupports(const CanDiscState& s, uint16_t responseId, uint8_t pid) {
    if (responseId < 0x7E8 || responseId > 0x7EF) return false;
    if (canDiscIsBitmapPid(pid)) return false;
    return (s.supported[responseId - 0x7E8][pid >> 3] & (0x80 >> (pid & 7))) != 0;
}

/** Supported sensor PIDs listed by ECU `ecu` (0 = 0x7E8). */
static inline uint8_t canDiscCountEcu(const CanDiscState& s, uint8_t ecu) {
    uint8_t n = 0;
    for (uint16_t p = 1; p <= 0xFF; p++) {
        if (canDiscSupports(s, (uint16_t)(0x7E8 + ecu), (uint8_t)p)) n++;
    }
    return n;
}

/** Number of ECUs that answered. */
static inline uint8_t canDiscEcuCount(const CanDiscState& s) {
    uint8_t n = 0;
    for (uint8_t m = s.ecuMask; m; m >>= 1) n += m & 1;
    return n;
}

/**
 * A Mode 01 answer arrived: if it is the bitmap for the block in flight,
 * record it and return true (the caller keeps it out of the frame cache and
 * the scan results). `data` is the payload after the PID byte.
 */
static inline bool canDiscOnAnswer(CanDiscState& s, uint16_t responseId, uint8_t pid,
                                   const uint8_t* data, uint8_t len) {
    if (s.phase != CAN_DISC_QUERYING || !s.sent) return false;
    if (responseId < 0x7E8 || responseId > 0x7EF) return false;
    if (pid != s.base || len < 4) return false;

    uint8_t ecu = (uint8_t)(responseId - 0x7E8);
    s.ecuMask |= (uint8_t)(1 << ecu);
    for (uint8_t i = 0; i < 32; i++) {
        if (data[i >> 3] & (0x80 >> (i & 7))) {
            uint16_t p = (uint16_t)s.base + 1 + i;
            if (p <= 0xFF) s.supported[ecu][p >> 3] |= (uint8_t)(0x80 >> (p & 7));
        }
    }
    if ((data[3] & 0x01) && s.base < CAN_DISC_LAST_BASE) s.more = true;
    return true;
}

/**
 * Should the request for block `base` go out now? Closes an expired window
 * and moves on: the next block, a retry of PID 0x00, or done (no more
 * blocks, or nothing answered PID 0x00 CAN_DISC_TRIES times).
 */
static inline bool canDiscDue(CanDiscState& s, uint32_t now, uint8_t& base) {
    if (s.phase != CAN_DISC_QUERYING) return false;
    if (s.sent) {
        if (now - s.sentMs < CAN_DISC_WINDOW_MS) return false;
        s.sent = false;
        if (s.ecuMask == 0) {
            // Nothing answered PID 0x00 (ignition off, no ECU): retry, then stop
            if (s.tries >= CAN_DISC_TRIES) { s.phase = CAN_DISC_DONE; return false; }
        } else if (s.more) {
            s.base = (uint8_t)(s.base + 0x20);
            s.more = false;
            s.tries = 0;
        } else {
            s.phase = CAN_DISC_DONE;
            return false;
        }
    }
    base = s.base;
    return true;
}

/**
 * The block's request was handed to the controller. A refused write (TX
 * full, bus-off) still waits out the window, so a dead bus spends the same
 * CAN_DISC_TRIES windows on PID 0x00 and then reports no ECU.
 */
static inline void canDiscOnSent(CanDiscState& s, uint32_t now) {
    s.sentMs = now;
    s.sent = true;
    s.tries++;
}

#endif // CAN_DISCOVERY_H
