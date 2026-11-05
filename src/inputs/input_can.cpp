/*
 * input_can.cpp - CAN Input Subsystem
 *
 * Separate CAN input subsystem - independent from CAN output.
 * Receives frames from configured input CAN bus and populates frame cache.
 * Supports OBD-II, J1939, and custom CAN protocols.
 *
 * Uses HAL for platform abstraction (FlexCAN, TWAI, MCP2515).
 * Supports dual-bus on Teensy (input on different bus than output).
 */

#if INCLUDE_CAN

#include <Arduino.h>
#include "input_can.h"
#include "can_frame_parse.h"
#include "can_poll.h"
#include "isotp_rx.h"
#include "../sensors/drivers/can/can_scan.h"
#if SUPPORTS_JSON_EXPORT
#include "can_status_json.h"
#endif
#include "input_manager.h"
#include "../storage/system_config.h"
#include "../storage/bus_config.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../sensors/drivers/can/can_frame_cache.h"
#include "../hal/hal_can.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool canInputInitialized = false;
static uint8_t canInputBus = 0;  // Which bus we're reading from

// On a bus shared with CAN output, input is the only reader; OBD-II requests
// it drains are handed to output's responder through this (#495).
static CANRequestHandler requestHandler = nullptr;

// Active Mode 01 polling (#476) — see can_poll.h for the pacing rules
static CanPollState pollState;
static uint8_t pollCursor = 0;   // inputs[] slot to try next

// Multi-frame Mode 01 replies (#527) — see isotp_rx.h
static IsoTpRx isoTpRx;
static_assert(ISOTP_RX_MAX - 2 <= CAN_FRAME_DATA_MAX,
              "a reassembled reply's data must fit a frame cache entry");

#if SUPPORTS_JSON_EXPORT
// Counters behind BUS CAN JSON (#510). JSON is compiled out on AVR, and so
// are these: they exist only to be reported.
static uint32_t statFramesRx = 0;
static uint32_t statLastFrameMs = 0;
static uint32_t statRequests = 0;
static uint32_t statAnswers = 0;
static uint32_t statTxFail = 0;
static bool     statSharedBus = false;
// What initCANInput() actually brought up: BUS CAN INPUT edits systemConfig
// immediately but only take effect on reboot, so BUS CAN JSON reports
// rebootPending when the two differ (#510 review).
static uint8_t  statInitBus = 0xFF;
static uint8_t  statInitMode = CAN_INPUT_OFF;
static uint32_t statInitBaud = 0;
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize CAN input subsystem
 * Sets up input CAN bus based on systemConfig.buses.input_can_bus
 * Uses HAL for platform abstraction.
 *
 * @return true if initialized successfully, false otherwise
 */
bool initCANInput() {
    // Check if input is enabled (NORMAL or LISTEN mode)
    uint8_t mode = systemConfig.buses.can_input_mode;
#if SUPPORTS_JSON_EXPORT
    // Recorded before any early return: "what boot tried to bring up"
    statInitMode = mode;
    statInitBus = systemConfig.buses.input_can_bus;
    statInitBaud = systemConfig.buses.can_input_baudrate;
#endif
    if (mode == CAN_INPUT_OFF) {
        canInputInitialized = false;
        return false;
    }

    uint8_t bus = systemConfig.buses.input_can_bus;
    if (bus == 0xFF) {
        canInputInitialized = false;
        return false;  // No input bus configured
    }

    uint32_t baudrate = systemConfig.buses.can_input_baudrate;
    bool listenOnly = (mode == CAN_INPUT_LISTEN);

    // Check if sharing bus with output
    bool sharedBus = (bus == systemConfig.buses.output_can_bus &&
                     systemConfig.buses.can_output_enabled &&
                     bus != 0xFF);

    // Output starts a shared bus only when its module is enabled; otherwise
    // nothing would, and reading an unstarted FlexCAN stalls the loop into a
    // watchdog reset (#495). Input starts it itself in that case.
    bool outputStartsBus = sharedBus && systemConfig.outputEnabled[OUTPUT_CAN];
#if SUPPORTS_JSON_EXPORT
    statSharedBus = sharedBus;
    statInitBaud = sharedBus ? systemConfig.buses.can_output_baudrate : baudrate;
#endif

    if (outputStartsBus) {
        // Output subsystem will initialize the shared bus
        // Just verify baudrates match
        if (baudrate != systemConfig.buses.can_output_baudrate) {
            msg.debug.warn(TAG_CAN, "Shared bus: using output baudrate %lu",
                          systemConfig.buses.can_output_baudrate);
        }
        canInputBus = bus;
        msg.debug.info(TAG_CAN, "CAN input using shared bus %d (initialized by output)", bus);
    } else {
        // Independent bus - initialize via HAL
        if (!hal::can::begin(baudrate, bus, listenOnly)) {
            msg.debug.error(TAG_CAN, "CAN input init failed on bus %d", bus);
            canInputInitialized = false;
            return false;
        }
        canInputBus = bus;
        const char* modeStr = listenOnly ? "listen-only" : "normal";
        msg.debug.info(TAG_CAN, "CAN input initialized on bus %d (%lu bps, %s)", bus, baudrate, modeStr);
    }

    // Initialize CAN frame cache
    initCANFrameCache();

    canPollReset(pollState, millis());
    pollCursor = 0;
    isoTpRxReset(isoTpRx);

    canInputInitialized = true;
    return true;
}

/**
 * Shutdown CAN input subsystem
 *
 * NOTE: This does NOT disable the CAN bus hardware peripheral for two reasons:
 * 1. Output subsystem may still be using the same physical bus
 * 2. HAL doesn't provide a bus-safe shutdown API (multiple subsystems per bus)
 *
 * This is intentional behavior - CAN hardware stays active but frame processing stops.
 * Power consumption: ~5-10mA (MCP2515) or ~2-3mA (FlexCAN standby mode).
 *
 * For true power-down, disable INCLUDE_CAN at compile time or power-cycle the board.
 */
void shutdownCANInput() {
    canInputInitialized = false;
}

bool canInputIsRunning() {
    return canInputInitialized && systemConfig.buses.can_input_mode != CAN_INPUT_OFF;
}

bool canInputOwnsBus(uint8_t bus) {
    // The live mode, not just init state: BUS CAN INPUT DISABLE writes OFF at
    // runtime and updateCANInput() stops reading, so output must resume.
    return canInputInitialized && canInputBus == bus &&
           systemConfig.buses.can_input_mode != CAN_INPUT_OFF;
}

void setCANRequestHandler(CANRequestHandler handler) {
    requestHandler = handler;
}

#if SUPPORTS_JSON_EXPORT
FLASHMEM bool canInputIsPolled(const Input& in) {
    if (in.pin < PIN_CAN_BASE || in.pin > PIN_CAN_MAX) return false;
    if (!in.flags.isEnabled || !in.flags.useCustomCalibration) return false;
    if (in.calibrationType != CAL_CAN_IMPORT) return false;
    return systemConfig.buses.can_input_mode == CAN_INPUT_NORMAL &&
           isObdResponseId(in.customCalibration.can.source_can_id);
}

FLASHMEM bool canInputRunsNormal() {
    // Both the setting and what boot brought up: BUS CAN INPUT edits the
    // first at once, the controller follows only after a reboot
    return canInputIsRunning() && statInitMode == CAN_INPUT_NORMAL &&
           systemConfig.buses.can_input_mode == CAN_INPUT_NORMAL;
}

FLASHMEM void getCANInputStatus(CanBusSnapshot& s, uint32_t now) {
    s.inRunning = canInputIsRunning();
    // Configured (systemConfig, what the dump shows) vs brought up at boot
    s.rebootPending = s.inMode != statInitMode ||
                      (s.inMode != CAN_INPUT_OFF &&
                       (s.inBus != statInitBus || s.inBaudrate != statInitBaud));
    s.inShared = canInputInitialized && statSharedBus;
    s.framesRx = statFramesRx;
    s.haveFrame = statFramesRx != 0;
    s.lastFrameAgeMs = s.haveFrame ? now - statLastFrameMs : 0;
    s.pollActive = s.inRunning && systemConfig.buses.can_input_mode == CAN_INPUT_NORMAL;
    s.pollBackoff = s.pollActive && pollState.missStreak >= CAN_POLL_BACKOFF_AFTER;
    s.missStreak = pollState.missStreak;
    s.requests = statRequests;
    s.answers = statAnswers;
    s.txFail = statTxFail;
}
#endif

// ============================================================================
// FRAME RECEPTION
// ============================================================================

static void acceptCANValue(uint32_t can_id, uint8_t identifier, const uint8_t* value,
                           uint8_t stored, uint8_t payload, bool mode01, bool extended,
                           uint32_t now);

/**
 * A First or Consecutive Frame from an ECU (#527). Reassembles a Mode 01
 * reply and, once complete, stores it like a single-frame answer. Flow
 * Control goes out only for a reply to our own outstanding request, and
 * only in NORMAL mode: in LISTEN, or for another tester's request, that
 * tester sends it and this only listens along. If another tester asked the
 * same ECU for the same PID while ours is outstanding, both send Flow
 * Control; ISO 15765-2 has the ECU ignore the unexpected second one.
 * Returns true when the frame belonged to a reply (whatever became of it).
 */
static bool handleIsoTpFrame(uint16_t id, const uint8_t* data, uint8_t len, uint32_t now) {
    switch (isoTpRxFrame(isoTpRx, id, data, len, now)) {
    case ISOTP_RX_STARTED:
        if (pollState.pending && id == pollState.pendingId &&
            isoTpRx.buf[1] == pollState.pendingPid &&
            systemConfig.buses.can_input_mode == CAN_INPUT_NORMAL) {   // the poller's own guard
            uint8_t fc[8];
            buildIsoTpFlowControl(fc);
            bool ok = hal::can::write(obdRequestIdFor(id), fc, 8, false, canInputBus);
            if (ok) canPollOnProgress(pollState, id, isoTpRx.buf[1], now);   // else it times out
#if SUPPORTS_JSON_EXPORT
            if (!ok) statTxFail++;
#endif
        }
        return true;
    case ISOTP_RX_PROGRESS:
        // Only a reply to our request holds it open, not another tester's
        canPollOnProgress(pollState, id, isoTpRx.buf[1], now);
        return true;
    case ISOTP_RX_COMPLETE:
        // buf: 41 <pid> <data...>
        acceptCANValue(id, isoTpRx.buf[1], &isoTpRx.buf[2], isoTpRx.total - 2,
                       isoTpRx.total - 2, true, false, now);
        return true;
    case ISOTP_RX_ABORTED:
        return true;   // the request times out and the next round asks again
    case ISOTP_RX_IGNORED:
    default:
        return false;
    }
}

/**
 * Process a single CAN frame and extract data into cache
 * @param can_id    CAN identifier
 * @param data      Frame data buffer
 * @param len       Frame data length
 * @param extended  29-bit identifier (never a Mode 01 answer to our request)
 */
static void processCANFrame(uint32_t can_id, const uint8_t* data, uint8_t len,
                           bool extended) {
    // OBD-II request on a bus shared with output: output's responder owns it
    if (requestHandler && !extended && (can_id == 0x7DF || can_id == 0x7E0)) {
        requestHandler(can_id, data, len);
        return;
    }

    // Validate frame has minimum data
    if (len == 0) {
        #ifdef DEBUG
        msg.debug.warn(TAG_CAN, "Empty CAN frame (ID 0x%03X)", can_id);
        #endif
        return;
    }

    uint32_t now = millis();

    // A multi-frame Mode 01 reply (0x70 boost, 0x78 EGT, ...) is
    // reassembled before it is a value (#527)
    if (!extended && isObdResponseId((uint16_t)can_id) && handleIsoTpFrame((uint16_t)can_id, data, len, now)) {
        return;
    }

    // A negative response or any other ISO-TP PCI (a Flow Control, a frame
    // of a reply we aren't reassembling) from an ECU is not a value; it
    // would be keyed by its first byte (NRC as PID 0x03, a first frame as
    // 0x10). See can_frame_parse.h.
    if (!extended && isObdResponseId((uint16_t)can_id) && isObdNonValueFrame(data, len)) {
        // "PID not supported" still answers our request: release it now
        // rather than after the timeout, and don't count a miss (#507)
        if (isMode01NegativeResponse(data, len)) {
            bool answered = canPollOnNegative(pollState, (uint16_t)can_id, now);
#if SUPPORTS_JSON_EXPORT
            if (answered) statAnswers++;
#else
            (void)answered;
#endif
        }
        return;
    }

    // Detect protocol format and extract identifier (see can_frame_parse.h)
    uint8_t identifier;
    uint8_t data_offset;

    #ifdef DEBUG
    if (len == 2 && data[0] == 0x41) {
        // Too short for PID extraction; stored under the custom-protocol key
        msg.debug.warn(TAG_CAN, "Short OBD-II frame (ID 0x%03X, len=%d)", can_id, len);
    }
    #endif

    if (!parseCANInputFrame(data, len, identifier, data_offset)) {
        #ifdef DEBUG
        msg.debug.warn(TAG_CAN, "No data after protocol parsing (ID 0x%03X)", can_id);
        #endif
        return;
    }

    uint8_t data_length = len - data_offset;

    // An ISO-TP single frame carries its payload length (length byte - mode
    // - PID); the rest of the 8 bytes is padding.
    uint8_t payload = data_length;
    if (data_offset == 3 && data[0] >= 2 && (uint8_t)(data[0] - 2) < payload) {
        payload = data[0] - 2;
    }

    acceptCANValue(can_id, identifier, &data[data_offset], data_length, payload,
                   data_offset != 0, extended, now);
}

/**
 * Store a parsed value frame: SCAN PIDS bitmap, frame cache, scan results,
 * and the poller's answer. `stored` bytes go to the cache (a single frame's
 * padding included); `payload` is what the ECU actually sent. `mode01` is a
 * Mode 01 answer rather than a custom frame keyed by its first byte.
 */
static void acceptCANValue(uint32_t can_id, uint8_t identifier, const uint8_t* value,
                           uint8_t stored, uint8_t payload, bool mode01, bool extended,
                           uint32_t now) {
#if SUPPORTS_JSON_EXPORT
    // SCAN PIDS (#511): a supported-PID bitmap answer belongs to the
    // discovery, not to the frame cache or the scan results
    if (mode01 && !extended &&
        canScanOnBitmap((uint16_t)can_id, identifier, value, payload)) {
        return;
    }
#endif

    updateCANCache(can_id, identifier, value, stored);

    // The scan sees every parsed 11-bit frame (#510); a 29-bit ID would be
    // truncated to 16 bits here and could pose as OBD (0x18DA07E8 -> 0x7E8).
    if (!extended) {
        canScanOnFrame((uint16_t)can_id, identifier, value, payload, now);
    }

    // A Mode 01 response (offset 2 or 3; custom frames parse at offset 0)
    // answers our outstanding request and ends any backoff. A 29-bit ID
    // truncated to 16 bits could otherwise pose as 0x7E8.
    if (mode01 && !extended) {
        bool answered = canPollOnResponse(pollState, (uint16_t)can_id, identifier, now);
#if SUPPORTS_JSON_EXPORT
        if (answered) statAnswers++;
#else
        (void)answered;
#endif
    }
}

// ============================================================================
// ACTIVE POLLING (#476)
// ============================================================================

/**
 * Does inputs[slot] need its PID requested? Only an enabled, imported
 * (custom-calibration) CAN input whose source is an OBD-II ECU response ID
 * (0x7E8-0x7EF). A CAN input still on its preset carries the placeholder
 * PID 0x00 and is skipped; custom/J1939 IDs are broadcast data and are
 * never polled. A (source ID, PID) pair already owned by an earlier slot is
 * skipped so each pair is requested once per round.
 */
static bool pollTargetAt(uint8_t slot, uint16_t& responseId, uint8_t& pid) {
    const Input& in = inputs[slot];
    if (in.pin < PIN_CAN_BASE || in.pin > PIN_CAN_MAX) return false;
    if (!in.flags.isEnabled || !in.flags.useCustomCalibration) return false;
    if (in.calibrationType != CAL_CAN_IMPORT) return false;

    const CANSensorCalibration& cal = in.customCalibration.can;
    if (!isObdResponseId(cal.source_can_id)) return false;

    for (uint8_t j = 0; j < slot; j++) {
        const Input& other = inputs[j];
        if (other.pin < PIN_CAN_BASE || other.pin > PIN_CAN_MAX) continue;
        if (!other.flags.isEnabled || !other.flags.useCustomCalibration) continue;
        if (other.calibrationType != CAL_CAN_IMPORT) continue;
        if (other.customCalibration.can.source_can_id == cal.source_can_id &&
            other.customCalibration.can.source_pid == cal.source_pid) {
            return false;
        }
    }

    responseId = cal.source_can_id;
    pid = cal.source_pid;
    return true;
}

#if SUPPORTS_JSON_EXPORT
/**
 * SCAN PIDS (#511): send the next supported-PID bitmap request, functional
 * (0x7DF) so every ECU answers. The imported-PID poller is paused meanwhile
 * (one requester on the bus); the walk takes about 100 ms per block, under
 * a second in all. These requests are not counted in BUS CAN JSON's poll
 * block, which describes the imported-PID poller.
 */
static FLASHMEM void runCANDiscovery() {
    // LISTEN must never transmit; the mode can change under a running scan,
    // and the controller keeps its boot mode until a reboot
    if (!canInputRunsNormal()) {
        canScanDiscoveryStop();
        return;
    }
    uint32_t now = millis();
    uint8_t base;
    if (!canScanDiscoveryDue(now, base)) return;
    uint8_t frame[8];
    buildMode01Request(base, frame);
    // A refused write still waits out its window (can_discovery.h); count it
    // so BUS CAN JSON shows a TX problem rather than only "no ECU answered"
    if (!hal::can::write(0x7DF, frame, 8, false, canInputBus)) statTxFail++;
    canScanDiscoverySent(now);
}
#endif

/**
 * Send the next Mode 01 request, round-robin over the imported PIDs.
 * NORMAL mode only: LISTEN must never transmit, and on a bus shared with
 * CAN output the hardware is in TX mode, so this check is the guard.
 */

static void pollCANInputs() {
#if SUPPORTS_JSON_EXPORT
    if (canScanQuerying()) {   // before the mode check: it ends a scan left in LISTEN
        runCANDiscovery();
        // Answers to the poller's last request may still arrive; start its
        // next round clean once the scan ends
        canPollReset(pollState, millis());
        return;
    }
#endif
    if (systemConfig.buses.can_input_mode != CAN_INPUT_NORMAL) return;
    if (!inputManagerTableIsLive()) return;

    uint32_t now = millis();
    bool wasPending = pollState.pending;
    bool ready = canPollReady(pollState, now);

    // The last request just timed out. If no node ACKed it (ignition off,
    // ECU unpowered) the controller is still retransmitting it back to
    // back: stop that, so the bus is quiet until the next request (#506).
    // The driver acts only while the controller is error-passive or
    // bus-off, so on a healthy bus it never touches a frame output queued.
    // On a failing one (TEC >= 128 from a bad wire, say) it also drops
    // output's pending frames, e.g. the rest of an ISO-TP reply; the tester
    // retries.
    if (wasPending && !pollState.pending) hal::can::abortUnackedTx(canInputBus);
    if (!ready) return;

    for (uint8_t n = 0; n < MAX_INPUTS; n++) {
        uint8_t slot = (uint8_t)(((uint16_t)pollCursor + n) % MAX_INPUTS);

        uint16_t responseId;
        uint8_t pid;
        if (!pollTargetAt(slot, responseId, pid)) continue;

        // A new round waits out the round floor (#508). The cursor stays
        // put so the same slot is tried again once it is due.
        if (canPollOpensRound(pollCursor, n, MAX_INPUTS) &&
            !canPollStartRound(pollState, now)) {
            return;
        }
        pollCursor = (uint8_t)((slot + 1) % MAX_INPUTS);

        uint8_t frame[8];
        buildMode01Request(pid, frame);
        bool ok = hal::can::write(obdRequestIdFor(responseId), frame, 8, false, canInputBus);
        canPollOnSent(pollState, responseId, pid, ok, now);
#if SUPPORTS_JSON_EXPORT
        statRequests++;
        if (!ok) statTxFail++;
#endif
        return;
    }
    // Nothing to poll: rescan after the normal gap rather than every loop.
    pollState.lastTxMs = now;
}

/**
 * Update CAN input - poll for incoming frames and populate cache
 * Called from main loop - non-blocking
 * Uses HAL for platform abstraction.
 *
 * Supports:
 * - OBD-II Mode 01 responses (0x41), 1-5 data bytes - PID is the identifier
 * - J1939 and custom protocols - uses byte[0] as identifier
 * - Any CAN ID (not limited to 0x7E8)
 */
void updateCANInput() {
    // Check if input is enabled and initialized
    if (!canInputInitialized || systemConfig.buses.can_input_mode == CAN_INPUT_OFF) {
#if SUPPORTS_JSON_EXPORT
        canScanDiscoveryStop();   // input turned off under SCAN PIDS: end it, don't hang
#endif
        return;
    }

    // Poll for frames via HAL (handles all platforms)
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
    bool extended;

    while (hal::can::read(id, data, len, extended, canInputBus)) {
#if SUPPORTS_JSON_EXPORT
        statFramesRx++;
        statLastFrameMs = millis();
#endif
        processCANFrame(id, data, len, extended);
    }

    pollCANInputs();
}

#endif // INCLUDE_CAN
