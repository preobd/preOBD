/*
 * isotp_rx.h - Reassemble an ISO-TP multi-frame OBD-II Mode 01 reply (#527)
 *
 * Pure, header-only: the receive state machine and the Flow Control frame.
 * Kept free of Arduino/HAL deps so the native test build can exercise it
 * directly (test/test_isotp_rx). When to send Flow Control, and what the
 * reassembled reply feeds, live in input_can.cpp.
 *
 * A Mode 01 reply longer than 7 bytes (0x70 boost, 0x78/0x79 EGT, 0x7F run
 * time) arrives as a First Frame (1L LL 41 <pid> + 4 bytes), then, after the
 * requester's Flow Control, Consecutive Frames (2n + 7 bytes, n = 1, 2, ...
 * mod 16). One reply is reassembled at a time: a new First Frame replaces
 * one in progress. Only Mode 01 positive replies short enough for the cache
 * are taken; anything else (a VIN for another tester, Mode 22) is ignored.
 */

#ifndef ISOTP_RX_H
#define ISOTP_RX_H

#include <stdint.h>
#include <string.h>

// 0x41 + PID + the longest table payload (0x7F run time: support byte +
// 3 x 4 bytes). input_can.cpp static_asserts it against the frame cache.
#define ISOTP_RX_MAX        15
// N_Cr: longest wait for the next Consecutive Frame (ISO 15765-4)
#define ISOTP_RX_TIMEOUT_MS 150

enum IsoTpRxResult : uint8_t {
    ISOTP_RX_IGNORED,    // Not part of a reply we are reassembling
    ISOTP_RX_STARTED,    // First Frame accepted: the requester sends Flow Control
    ISOTP_RX_PROGRESS,   // Consecutive Frame accepted, more to come
    ISOTP_RX_COMPLETE,   // buf[0..total) holds 41 <pid> <data...>
    ISOTP_RX_ABORTED     // Wrong sequence, short frame or timeout: reply dropped
};

struct IsoTpRx {
    uint32_t lastMs;     // Last accepted frame
    uint16_t id;         // Response ID being reassembled
    uint8_t  buf[ISOTP_RX_MAX];
    uint8_t  total;      // Message length from the First Frame
    uint8_t  pos;        // Bytes received so far
    uint8_t  nextSeq;    // Expected Consecutive Frame sequence number
    bool     active;
};

static inline void isoTpRxReset(IsoTpRx& s) {
    s.active = false;
    s.total = 0;
    s.pos = 0;
}

/** Flow Control "clear to send": 30 <block size 0 = all> <STmin 0>, padded. */
static inline void buildIsoTpFlowControl(uint8_t out[8]) {
    out[0] = 0x30;
    for (uint8_t i = 1; i < 8; i++) out[i] = 0x00;
}

/**
 * Feed one frame received on an ECU response ID. Frames that are not a
 * First or Consecutive Frame return ISOTP_RX_IGNORED without touching the
 * state.
 */
static inline IsoTpRxResult isoTpRxFrame(IsoTpRx& s, uint16_t id, const uint8_t* data,
                                         uint8_t len, uint32_t now) {
    if (len == 0) return ISOTP_RX_IGNORED;
    uint8_t pci = data[0] & 0xF0;

    if (pci == 0x10) {
        // A First Frame fills the CAN frame and carries more than a single
        // frame could (>= 8 bytes)
        uint16_t total = ((uint16_t)(data[0] & 0x0F) << 8) | data[1];
        if (s.active && id == s.id) isoTpRxReset(s);   // that ECU started over
        if (len < 8 || total < 8 || total > ISOTP_RX_MAX || data[2] != 0x41) {
            return ISOTP_RX_IGNORED;
        }
        s.id = id;
        s.total = (uint8_t)total;
        memcpy(s.buf, &data[2], 6);
        s.pos = 6;
        s.nextSeq = 1;
        s.lastMs = now;
        s.active = true;
        return ISOTP_RX_STARTED;
    }

    if (pci != 0x20 || !s.active || id != s.id) return ISOTP_RX_IGNORED;

    if (now - s.lastMs > ISOTP_RX_TIMEOUT_MS || (data[0] & 0x0F) != s.nextSeq) {
        isoTpRxReset(s);
        return ISOTP_RX_ABORTED;
    }
    uint8_t need = s.total - s.pos;
    if (need > 7) need = 7;
    if ((uint8_t)(len - 1) < need) {   // a frame too short to carry its share
        isoTpRxReset(s);
        return ISOTP_RX_ABORTED;
    }
    memcpy(&s.buf[s.pos], &data[1], need);
    s.pos += need;
    s.nextSeq = (s.nextSeq + 1) & 0x0F;
    s.lastMs = now;
    if (s.pos < s.total) return ISOTP_RX_PROGRESS;
    s.active = false;
    return ISOTP_RX_COMPLETE;
}

#endif // ISOTP_RX_H
