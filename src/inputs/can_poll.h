/*
 * can_poll.h - OBD-II Mode 01 request pacing for imported CAN inputs (#476)
 *
 * Pure, header-only: the request frame, the ECU addressing rule and the
 * pacing state machine. Kept free of Arduino/HAL deps so the native test
 * build can exercise it directly (test/test_can_poll). Which inputs get
 * polled, and the actual transmit, live in input_can.cpp.
 *
 * Pacing is response-driven: one request is outstanding at a time, and the
 * next goes out CAN_POLL_MIN_GAP_MS after its answer arrives, or
 * CAN_POLL_RESPONSE_TIMEOUT_MS after it was sent if no answer came. ECUs
 * normally answer within 5-15 ms, so a round of N PIDs takes roughly
 * N * 25 ms — well inside the 2 s default stale timeout. After
 * CAN_POLL_BACKOFF_AFTER consecutive unanswered or unsent requests (ignition
 * off, no ECU, bus down) the rate drops to one request per
 * CAN_POLL_BACKOFF_MS until an answer arrives.
 */

#ifndef CAN_POLL_H
#define CAN_POLL_H

#include <stdint.h>

#define CAN_POLL_MIN_GAP_MS          10    // Gap after an answer before the next request
#define CAN_POLL_RESPONSE_TIMEOUT_MS 50    // Give up on an answer after this long (ISO 15765-4 P2)
#define CAN_POLL_BACKOFF_AFTER       10    // Consecutive misses before slowing down
#define CAN_POLL_BACKOFF_MS          1000  // Request interval while backed off

/** True for the 11-bit OBD-II ECU response IDs 0x7E8-0x7EF. */
static inline bool isObdResponseId(uint16_t id) {
    return id >= 0x7E8 && id <= 0x7EF;
}

/** Physical request ID for an ECU response ID: 0x7E8 -> 0x7E0 (ISO 15765-4). */
static inline uint16_t obdRequestIdFor(uint16_t responseId) {
    return responseId - 8;
}

/** Build an ISO-TP single-frame Mode 01 request: 02 01 <pid> 00 00 00 00 00. */
static inline void buildMode01Request(uint8_t pid, uint8_t out[8]) {
    out[0] = 0x02;
    out[1] = 0x01;
    out[2] = pid;
    for (uint8_t i = 3; i < 8; i++) out[i] = 0x00;
}

struct CanPollState {
    uint32_t lastTxMs;
    uint16_t pendingId;     // Response ID we are waiting on
    uint8_t  pendingPid;
    uint8_t  missStreak;    // Consecutive unanswered / unsent requests
    bool     pending;
};

static inline void canPollReset(CanPollState& s, uint32_t now) {
    s.lastTxMs = now;
    s.pendingId = 0;
    s.pendingPid = 0;
    s.missStreak = 0;
    s.pending = false;
}

/**
 * A Mode 01 response (id, pid) was received at `now`. Clears the outstanding
 * request if it matches, restarting the gap from the answer, and any answer
 * ends a backoff.
 */
static inline void canPollOnResponse(CanPollState& s, uint16_t id, uint8_t pid,
                                     uint32_t now) {
    if (!isObdResponseId(id)) return;
    s.missStreak = 0;
    if (s.pending && id == s.pendingId && pid == s.pendingPid) {
        s.pending = false;
        s.lastTxMs = now;   // CAN_POLL_MIN_GAP_MS counts from the answer
    }
}

/**
 * May the next request go out now? lastTxMs is the last request, or the
 * answer that released it. A request that has waited past its timeout is
 * counted as a miss here.
 */
static inline bool canPollReady(CanPollState& s, uint32_t now) {
    uint32_t elapsed = now - s.lastTxMs;
    uint32_t wait;
    if (s.missStreak >= CAN_POLL_BACKOFF_AFTER) {
        wait = CAN_POLL_BACKOFF_MS;
    } else if (s.pending) {
        wait = CAN_POLL_RESPONSE_TIMEOUT_MS;
    } else {
        wait = CAN_POLL_MIN_GAP_MS;
    }
    if (elapsed < wait) return false;

    if (s.pending) {
        s.pending = false;
        if (s.missStreak < 0xFF) s.missStreak++;
    }
    return true;
}

/**
 * A request for (responseId, pid) was handed to the controller. A refused
 * write (TX full, bus-off) counts as a miss so a dead bus backs off.
 */
static inline void canPollOnSent(CanPollState& s, uint16_t responseId, uint8_t pid,
                                 bool ok, uint32_t now) {
    s.lastTxMs = now;
    if (ok) {
        s.pending = true;
        s.pendingId = responseId;
        s.pendingPid = pid;
    } else {
        s.pending = false;
        if (s.missStreak < 0xFF) s.missStreak++;
    }
}

#endif // CAN_POLL_H
