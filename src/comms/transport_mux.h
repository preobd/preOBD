/*
 * transport_mux.h - Multiplexed UART Transport
 *
 * MuxTransport implements TransportInterface for the host-MCU end of the
 * nRF52840 coprocessor link. Has two modes selected at runtime:
 *
 *   TRANSPARENT (default): byte-for-byte equivalent to SerialTransport.
 *     Bytes flow unchanged through Serial1.write/read with optional Phase 3
 *     software flow control on a designated GPIO. Compatible with HM-10,
 *     stock Bluefruit, and pre-framed-mode nRF firmware.
 *
 *   FRAMED: bytes are wrapped in [0xAA][TYPE][LEN u16_le][PAYLOAD][CRC8]
 *     frames per ARCHITECTURE.md §3.3. Inbound type 0x01 (text command)
 *     payloads are buffered for the CLI dispatcher to read(); outbound
 *     write() bytes are accumulated and flushed as type 0x02 (text
 *     response) frames on newline, buffer-full, or 50ms idle stall.
 *     Type 0xFF (ping) frames are echoed immediately so the nRF's
 *     auto-detect succeeds. Other types are discarded in this MVP.
 *
 * Mode is set at boot from systemConfig.serial.framed_enabled_mask via
 * setFramedEnabled(); flag toggled at runtime by `BUS SERIAL <port> FRAMED
 * ENABLE|DISABLE` followed by SAVE + REBOOT.
 *
 * Usage:
 *   MuxTransport hwSerial1(&Serial1, "SERIAL1", 115200);
 *   // After initSystemConfig():
 *   hwSerial1.setFramedEnabled(systemConfig.serial.framed_enabled_mask & 0x01);
 *
 * Frame routing (#411 item 4): the mux used to reach directly into the two
 * output modules it multiplexes for (ActionBroadcaster, LiveValueBroadcaster)
 * -- a defect #401 named ("the multiplexer reaches into the broadcasters it
 * multiplexes"). It now routes through two small registration tables, same
 * shape as PinOwnerInputProbe (pinctrl/pin_owner.h) and SystemModeObserver
 * (core/system_mode.h): comms/ declares the callback types and dispatches,
 * the composition root (main.cpp) wires up who receives what. See
 * setMuxFrameHandler() / addMuxResyncHandler() below.
 */

#ifndef TRANSPORT_MUX_H
#define TRANSPORT_MUX_H

#include <Arduino.h>

#include "frame_codec.h"
#include "../core/transport_interface.h"
#include "../core/watchdog.h"

// ---------------------------------------------------------------------------
// Frame-handler registration (#411 item 4).
//
// A claimed inbound frame type (today: FRAME_TYPE_RELAY, the webapp's Action
// write) is dispatched to a handler registered for that type. Keyed by
// frame type rather than a single fixed slot so a second claimed type
// doesn't require another parallel mechanism later; only one type registers
// today. Fixed-size table, no dynamic allocation (embedded firmware) --
// registering past kMaxMuxFrameHandlers is silently dropped, which is the
// same "table exhausted" contract MAX_ACTIONS/MAX_INPUTS use elsewhere, and
// headroom (4 slots for the one user today) makes that a non-issue.
typedef void (*MuxFrameHandler)(const uint8_t* payload, uint16_t len);
constexpr uint8_t kMaxMuxFrameHandlers = 4;

/**
 * Claim `frameType`'s inbound payload for `handler`. Re-registering the same
 * frameType replaces the previous handler (last writer wins); nothing in
 * this firmware does that today.
 */
void setMuxFrameHandler(uint8_t frameType, MuxFrameHandler handler);

// FRAME_TYPE_PING marks a (re)connection; more than one broadcaster needs to
// invalidate its cache for it (today: Action + LiveValue), so this is a
// small fixed array rather than a single slot -- again no dynamic
// allocation.
typedef void (*MuxResyncHandler)();
constexpr uint8_t kMaxMuxResyncHandlers = 4;

/**
 * Register a handler to be called on every inbound FRAME_TYPE_PING. MUST be
 * called before framed frames can be dispatched -- i.e. in setup() before
 * the transport starts pumping (mirrors setSystemModeObserver()'s ordering
 * requirement, core/system_mode.h: main.cpp registers both before anything
 * that could invoke them). Returns false if every slot is already taken.
 */
bool addMuxResyncHandler(MuxResyncHandler handler);

class MuxTransport : public TransportInterface {
private:
    Stream* serial;
    const char* name;
    uint32_t baudRate;
    int16_t flowInPin;         // Phase 3 software flow-control input pin; -1 disables

    bool framedEnabled = false;

    // --- Framed-mode RX (decoded type 0x01 text payloads) ---
    // The nRF chunks a BLE client's text into 256-byte 0x01 frames, and
    // pumpInbound() decodes every frame already sitting in the UART buffer
    // before the CLI gets to read() any of it -- so the ring must hold more
    // than one frame or the second one's tail is dropped on the floor. Four
    // frames covers a bulk write whose echo blocks the CLI for one frame
    // time (#449); rxDropped counts what still doesn't fit. Sized 4*256+1
    // because the ring keeps one slot empty to tell full from empty -- at a
    // flat 1024 an exact four-frame burst drops its last byte, corrupting a
    // CLI line for the sake of one byte.
    static constexpr size_t kRxRingSize = 4 * 256 + 1;
    uint8_t rxRing[kRxRingSize];
    size_t rxHead = 0;         // producer
    size_t rxTail = 0;         // consumer
    uint32_t rxDropped = 0;    // text bytes discarded because the ring was full

    // --- Framed-mode TX accumulator (CLI bytes pending as 0x02 frame) ---
    // Sized to match the max payload chunk: full responses larger than this
    // split across multiple frames; the nRF concatenates them on the BLE side.
    static constexpr size_t kTxAccSize = 256;
    uint8_t txAcc[kTxAccSize];
    size_t txAccLen = 0;
    uint32_t txLastByteMs = 0;
    static constexpr uint32_t kTxStallMs = 50;

    // --- Framed-mode decoder ---
    // Per-instance state so multiple MuxTransports on different ports can
    // run framed mode concurrently without trampling each other's partial
    // frames. decoderFrame is the output target for completed frames.
    FrameDecoderState decoderState;
    Frame decoderFrame;
    // Inter-byte gap watchdog for the decoder (#449): a frame whose bytes
    // stop arriving mid-way (UART overrun ate its LEN, or the sender died) is
    // abandoned after kRxFrameGapMs so the decoder is back hunting SYNC before
    // the nRF's next PING. Well above one byte time at 115200 (~87 us) and
    // the nRF's own frame-at-a-time TX drain; well below its 2 s ping window.
    // Deliberately LONGER than the nRF's kOutboundStallMs (100 ms), which is
    // how long it will pause mid-frame waiting on our RTS: at equal values a
    // legitimately stalled frame would be abandoned at the same instant the
    // sender resumed it, turning a pause into a lost frame. (HOST_NRF_RTS_PIN
    // is held LOW today, so the nRF never actually stalls -- this keeps the
    // margin correct for when it does.)
    uint32_t rxLastByteMs = 0;
    static constexpr uint32_t kRxFrameGapMs = 250;

    // --- Phase 3 software flow control ---
    static constexpr size_t kFlowChunkBytes = 32;
    void awaitFlowClear() {
        if (flowInPin < 0) return;
        while (digitalRead(flowInPin) == HIGH) {
            watchdogReset();
            // Service inbound frames while stalled (#263): the nRF asserts
            // RTS precisely when a bulk text response is slow-draining over
            // BLE, and that can outlast an OBD app's request timeout. Pumping
            // here lets a concurrent ELM327 (0x30) request be parsed now and
            // its response go out at the next frame boundary, instead of
            // waiting for the whole dump to clear. No-op in transparent mode
            // and when called from inside a pump.
            pumpInbound();
            yield();
        }
    }

    // --- RX ring helpers ---
    bool rxRingFull() const {
        return ((rxHead + 1) % kRxRingSize) == rxTail;
    }
    bool rxRingEmpty() const { return rxHead == rxTail; }
    void rxRingPush(uint8_t b) {
        rxRing[rxHead] = b;
        rxHead = (rxHead + 1) % kRxRingSize;
    }
    int rxRingPop() {
        if (rxRingEmpty()) return -1;
        uint8_t b = rxRing[rxTail];
        rxTail = (rxTail + 1) % kRxRingSize;
        return b;
    }
    int rxRingPeek() const {
        if (rxRingEmpty()) return -1;
        return rxRing[rxTail];
    }
    size_t rxRingAvailable() const {
        if (rxHead >= rxTail) return rxHead - rxTail;
        return kRxRingSize - (rxTail - rxHead);
    }

    // --- Framed-mode pump helpers ---
    // pumpInbound is re-entrancy-guarded: it can be (and is) called from
    // inside blocking write paths (awaitFlowClear / writeSerialChunked), and
    // a handled frame may itself write (PING pong, ELM327 response). The
    // guard makes any nested pump a no-op, bounding the recursion at one
    // frame-handling level.
    bool pumping = false;
    // True while writeSerialChunked has a frame partially on the wire. Any
    // emission triggered from a pump in that window (sendFrame, PING pong)
    // must be deferred to the next frame boundary -- injecting a frame into
    // the middle of another would corrupt both at the nRF's decoder.
    bool writingFrame = false;
    bool pongPending = false;   // PING arrived mid-frame; echo at boundary
    void pumpInbound();
    void handleFrame(const Frame& f);
    void flushTxAcc();
    void writeSerialChunked(const uint8_t* data, size_t len);
    // Frame-boundary service: emit anything a mid-frame pump had to defer
    // (pong, queued ELM327 response bytes), then parse newly arrived input.
    void serviceFrameBoundary();

public:
    MuxTransport(Stream* serialPort, const char* transportName,
                 uint32_t baud = 115200, int16_t flow_in_pin = -1)
        : serial(serialPort), name(transportName), baudRate(baud),
          flowInPin(flow_in_pin) {}

    void setFramedEnabled(bool enabled);
    bool isFramedEnabled() const { return framedEnabled; }
    // Text bytes dropped because the framed RX ring was full (0 in a healthy
    // session). Surfaced by BUS SERIAL so a truncated bulk import is
    // diagnosable rather than "shouldn't happen" (#449).
    uint32_t framedRxDropped() const { return rxDropped; }

    // Send a complete framed packet of arbitrary type. Non-blocking and
    // atomic: returns false immediately if the transport isn't in framed
    // mode, if `len` exceeds FRAME_MAX_PAYLOAD, or if the underlying Serial
    // TX buffer can't absorb the entire encoded frame right now (or
    // Phase 3 flow control is asserted). Never writes a partial frame.
    // Callers that need delivery guarantees should retry on false.
    bool sendFrame(uint8_t type, const uint8_t* payload, uint16_t len);

    // ========== TransportInterface Implementation ==========

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    const char* getName() const override { return name; }

    uint8_t getCapabilities() const override {
        return CAP_READ | CAP_WRITE | CAP_BINARY | CAP_HARDWARE_SERIAL;
    }

    TransportState getState() const override { return TRANSPORT_CONNECTED; }

    bool begin() override { return true; }
    void end() override {}
    void update() override;
};

#endif // TRANSPORT_MUX_H
