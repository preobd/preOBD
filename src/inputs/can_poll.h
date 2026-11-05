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
 * CAN_POLL_RESPONSE_TIMEOUT_MS after it was sent if no answer came. A
 * negative response (03 7F 01 xx, PID not supported) is an answer too: it
 * releases the request without waiting out the timeout (#507). ECUs
 * normally answer within 5-15 ms, so a round of N PIDs takes roughly
 * N * 25 ms — well inside the 2 s default stale timeout. A round never
 * starts sooner than CAN_POLL_MIN_PID_INTERVAL_MS after the previous one,
 * so with few PIDs each is requested at the sensor read rate rather than
 * every CAN_POLL_MIN_GAP_MS (#508). After
 * CAN_POLL_BACKOFF_AFTER consecutive unanswered or unsent requests (ignition
 * off, no ECU, bus down) the rate drops to one request per
 * CAN_POLL_BACKOFF_MS until an answer arrives. A multi-frame answer (#527)
 * restarts the timeout with each of its frames, so it is never cut off
 * part way.
 */

#ifndef CAN_POLL_H
#define CAN_POLL_H

#include <stdint.h>

#define CAN_POLL_MIN_GAP_MS          10    // Gap after an answer before the next request
#define CAN_POLL_RESPONSE_TIMEOUT_MS 50    // Give up on an answer after this long (ISO 15765-4 P2)
#define CAN_POLL_BACKOFF_AFTER       10    // Consecutive misses before slowing down
#define CAN_POLL_BACKOFF_MS          1000  // Request interval while backed off
// Floor on a round's length, so on each PID's request interval. Matched to
// SENSOR_READ_INTERVAL_MS (config.h): polling faster than inputs are read
// only loads the bus (#508).
#define CAN_POLL_MIN_PID_INTERVAL_MS 50

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
    uint32_t roundStartMs;  // First request of the current round
    uint16_t pendingId;     // Response ID we are waiting on
    uint8_t  pendingPid;
    uint8_t  missStreak;    // Consecutive unanswered / unsent requests
    bool     pending;
    bool     roundHeld;     // A new round is waiting for the round floor
};

static inline void canPollReset(CanPollState& s, uint32_t now) {
    s.lastTxMs = now;
    s.roundStartMs = now - CAN_POLL_MIN_PID_INTERVAL_MS;   // first round not held
    s.pendingId = 0;
    s.pendingPid = 0;
    s.missStreak = 0;
    s.pending = false;
    s.roundHeld = false;
}

/**
 * A Mode 01 response (id, pid) was received at `now`. Clears the outstanding
 * request if it matches, restarting the gap from the answer, and any answer
 * ends a backoff. Returns true when it answered the outstanding request.
 */
static inline bool canPollOnResponse(CanPollState& s, uint16_t id, uint8_t pid,
                                     uint32_t now) {
    if (!isObdResponseId(id)) return false;
    s.missStreak = 0;
    if (s.pending && id == s.pendingId && pid == s.pendingPid) {
        s.pending = false;
        s.lastTxMs = now;   // CAN_POLL_MIN_GAP_MS counts from the answer
        return true;
    }
    return false;
}

/**
 * A negative response to Mode 01 (03 7F 01 <nrc>) arrived from `id` at
 * `now`. It carries no PID, so it releases the outstanding request when it
 * comes from the ECU that request went to. Without a PID it can't be told
 * from an NRC to another tester on the bus (or a late one to our request
 * that already timed out); releasing early then only means the next request
 * goes out sooner, and the real answer, if any, is still cached. It is an
 * answer: the ECU is there, so it also ends a backoff and isn't counted as
 * a miss (#507).
 * Returns true when it released the outstanding request.
 */
static inline bool canPollOnNegative(CanPollState& s, uint16_t id, uint32_t now) {
    if (!isObdResponseId(id)) return false;
    s.missStreak = 0;
    if (s.pending && id == s.pendingId) {
        s.pending = false;
        s.lastTxMs = now;
        return true;
    }
    return false;
}

/**
 * A multi-frame reply to the outstanding request (same ECU, same PID) has
 * started or moved on (#527). Restarts the answer timeout from `now`, so a
 * reply still arriving is not counted as a miss and its frames aren't
 * aborted mid-exchange. Its last frame answers the request through
 * canPollOnResponse(). Returns true when it extended the outstanding request.
 */
static inline bool canPollOnProgress(CanPollState& s, uint16_t id, uint8_t pid,
                                     uint32_t now) {
    if (!s.pending || id != s.pendingId || pid != s.pendingPid) return false;
    s.lastTxMs = now;
    return true;
}

/**
 * Does the next poll target open a new round? The round-robin search starts
 * at `cursor` and found its target `n` slots on; a round begins at the head
 * of the table, or when the search wrapped past its end.
 */
static inline bool canPollOpensRound(uint8_t cursor, uint8_t n, uint8_t tableSize) {
    return cursor == 0 || (uint16_t)cursor + n >= tableSize;
}

/**
 * The next request opens a new round (the round-robin wrapped). Returns true
 * and starts the round if CAN_POLL_MIN_PID_INTERVAL_MS has passed since the
 * previous round started; otherwise holds canPollReady() until it has, so a
 * held round costs one comparison per call rather than a table scan (#508).
 */
static inline bool canPollStartRound(CanPollState& s, uint32_t now) {
    if (now - s.roundStartMs < CAN_POLL_MIN_PID_INTERVAL_MS) {
        s.roundHeld = true;
        return false;
    }
    s.roundHeld = false;
    s.roundStartMs = now;
    return true;
}

/**
 * May the next request go out now? lastTxMs is the last request, or the
 * answer that released it. A request that has waited past its timeout is
 * counted as a miss here, whether or not the next one may go yet: the
 * caller sees `pending` drop and can abort the frame (#506).
 */
static inline bool canPollReady(CanPollState& s, uint32_t now) {
    // Set only with nothing pending (a round starts after canPollReady)
    if (s.roundHeld) {
        if (now - s.roundStartMs < CAN_POLL_MIN_PID_INTERVAL_MS) return false;
        s.roundHeld = false;   // due; don't let a stale flag outlive a millis() wrap
    }
    uint32_t elapsed = now - s.lastTxMs;

    // A request times out after CAN_POLL_RESPONSE_TIMEOUT_MS even while
    // backed off, so the caller can abort an unACKed frame then, rather than
    // just before the next request a second later (#506)
    if (s.pending && elapsed >= CAN_POLL_RESPONSE_TIMEOUT_MS) {
        s.pending = false;
        if (s.missStreak < 0xFF) s.missStreak++;
    }

    uint32_t wait;
    if (s.missStreak >= CAN_POLL_BACKOFF_AFTER) {
        wait = CAN_POLL_BACKOFF_MS;
    } else if (s.pending) {
        wait = CAN_POLL_RESPONSE_TIMEOUT_MS;
    } else {
        wait = CAN_POLL_MIN_GAP_MS;
    }
    return elapsed >= wait;
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
