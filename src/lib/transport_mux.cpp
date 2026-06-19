#include "transport_mux.h"

#include "../config.h"

#if ENABLE_FRAMED_TRANSPORT

#include <string.h>

#include "relay_broadcaster.h"

void MuxTransport::setFramedEnabled(bool enabled) {
    if (enabled == framedEnabled) return;
    framedEnabled = enabled;
    // Clean state on mode change so partial frames / accumulator residue
    // can't leak across the transition.
    rxHead = rxTail = 0;
    txAccLen = 0;
    frameDecoderReset(decoderState);
}

size_t MuxTransport::write(uint8_t c) {
    if (!framedEnabled) {
        awaitFlowClear();
        return serial->write(c);
    }
    // Framed mode: accumulate; flush at newline or buffer fill.
    if (txAccLen >= kTxAccSize) {
        flushTxAcc();
    }
    txAcc[txAccLen++] = c;
    txLastByteMs = millis();
    if (c == '\n' || txAccLen >= kTxAccSize) {
        flushTxAcc();
    }
    return 1;
}

size_t MuxTransport::write(const uint8_t* buffer, size_t size) {
    if (!framedEnabled) {
        // Transparent path: matches SerialTransport's Phase 3 behavior.
        if (flowInPin < 0) {
            return serial->write(buffer, size);
        }
        writeSerialChunked(buffer, size);
        return size;
    }
    // Framed: route each byte through the accumulator so flush triggers
    // (newline / buffer fill) fire correctly mid-buffer.
    for (size_t i = 0; i < size; ++i) {
        write(buffer[i]);
    }
    return size;
}

int MuxTransport::available() {
    if (!framedEnabled) return serial->available();
    pumpInbound();
    return static_cast<int>(rxRingAvailable());
}

int MuxTransport::read() {
    if (!framedEnabled) return serial->read();
    pumpInbound();
    return rxRingPop();
}

int MuxTransport::peek() {
    if (!framedEnabled) return serial->peek();
    pumpInbound();
    return rxRingPeek();
}

void MuxTransport::flush() {
    if (framedEnabled && txAccLen > 0) {
        flushTxAcc();
    }
    serial->flush();
}

void MuxTransport::update() {
    if (!framedEnabled) return;
    pumpInbound();
    // Stall flush so a partial outbound chunk doesn't sit forever waiting
    // for a newline that never comes (e.g. a prompt with no trailing \n).
    if (txAccLen > 0 && (millis() - txLastByteMs) >= kTxStallMs) {
        flushTxAcc();
    }
}

void MuxTransport::pumpInbound() {
    while (serial->available()) {
        const int b = serial->read();
        if (b < 0) break;
        FrameFeedResult r = frameFeedByte(decoderState, static_cast<uint8_t>(b), decoderFrame);
        if (r == FRAME_READY) {
            handleFrame(decoderFrame);
        }
        // NEED_MORE / RESYNCING: continue silently. Bytes outside a frame
        // are protocol noise and intentionally dropped (the framer hunts
        // for the next 0xAA).
    }
}

void MuxTransport::handleFrame(const Frame& f) {
    switch (f.type) {
    case FRAME_TYPE_TEXT_CMD:
        for (uint16_t i = 0; i < f.len; ++i) {
            if (rxRingFull()) break;  // text overflow -- shouldn't happen
            rxRingPush(f.payload[i]);  // for typical CLI commands
        }
        break;
    case FRAME_TYPE_PING: {
        // Echo immediately so the nRF's auto-detect upgrades the link.
        uint8_t pong[FRAME_HEADER_BYTES + 1];
        const size_t n = frameEncode(FRAME_TYPE_PING, nullptr, 0, pong);
        if (n > 0) writeSerialChunked(pong, n);
        break;
    }
    case FRAME_TYPE_RELAY:
        // Webapp-initiated relay write: forward to the broadcaster which
        // applies writable fields and emits a confirmation notify on the
        // next tick. RelayBroadcaster compiles to a no-op when relays are
        // disabled on this profile, so the unconditional call is safe.
        RelayBroadcaster::handleInbound(f.payload, f.len);
        break;
    default:
        // Status/Alarm/OTA/etc are MCU-originated; clients don't send them.
        break;
    }
}

void MuxTransport::flushTxAcc() {
    if (txAccLen == 0) return;
    uint8_t frame[FRAME_HEADER_BYTES + kTxAccSize + 1];
    const size_t n = frameEncode(FRAME_TYPE_TEXT_RESP,
                                 txAcc,
                                 static_cast<uint16_t>(txAccLen),
                                 frame);
    if (n > 0) {
        writeSerialChunked(frame, n);
    }
    txAccLen = 0;
}

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (!framedEnabled) return false;
    if (len > FRAME_MAX_PAYLOAD) return false;

    uint8_t frame[FRAME_MAX_BYTES];
    const size_t n = frameEncode(type, payload, len, frame);
    if (n == 0) return false;

    // Atomic check: enough TX buffer space AND flow-control gate open.
    // Refuse rather than block — caller retries on next tick.
    if (static_cast<size_t>(serial->availableForWrite()) < n) return false;
    if (flowInPin >= 0 && digitalRead(flowInPin) == HIGH) return false;

    // Capacity verified — commit. Bypass writeSerialChunked's awaitFlowClear
    // loop (the gate was just confirmed open) to keep the call truly non-
    // blocking; if the peer asserts mid-write, the bytes are already queued
    // in Serial's TX buffer and the hardware handles draining at its pace.
    serial->write(frame, n);
    return true;
}

void MuxTransport::writeSerialChunked(const uint8_t* data, size_t len) {
    if (flowInPin < 0) {
        serial->write(data, len);
        return;
    }
    // Phase 3 flow control: break into small chunks, re-check the gate
    // between each so a peer can throttle us partway through a long
    // response.
    size_t written = 0;
    while (written < len) {
        awaitFlowClear();
        const size_t remaining = len - written;
        const size_t chunk = remaining < kFlowChunkBytes
                                 ? remaining : kFlowChunkBytes;
        const size_t w = serial->write(data + written, chunk);
        if (w == 0) break;
        written += w;
    }
}

#endif // ENABLE_FRAMED_TRANSPORT
