// elm327_framed.cpp -- type 0x30 (ELM327) routing over the framed transport.
// See elm327_framed.h for design notes.

#include "elm327_framed.h"

#include "../config.h"

// Needs both the framed transport (the channel) and the ELM327 emulator (the
// parser). Either off -> empty stubs at the bottom of the file.
#if ENABLE_FRAMED_TRANSPORT && ENABLE_ELM327

#include <Arduino.h>
#include <string.h>

#include "frame_codec.h"
#include "transport_mux.h"
#include "../outputs/output_elm327.h"

namespace Elm327Framed {

namespace {

// In-memory Stream bridging a 0x30 frame's payload to/from the ELM327 parser.
// The parser does all I/O through Stream*, so we feed inbound payload to the
// read side and capture its response from the write side -- no parser changes.
//
//   inbound:  one frame's payload is loaded, then fully consumed by update().
//             Sized to comfortably hold an ELM327 command line; longer payloads
//             are fed in successive chunks (the parser accumulates across reads
//             until it sees a CR, so splitting mid-command is safe).
//   outbound: linear FIFO holding the response. Drained as 0x30 frame(s) by
//             handleInbound. outSent tracks how much has been sent so a
//             back-pressured send leaves the remainder for the next call.
//             Overflow drops bytes (only reachable under sustained TX back-
//             pressure, which BLE request/response traffic doesn't generate).
class FramedElmStream : public Stream {
public:
    // kInCap is only the per-feed transfer size between a frame and the parser;
    // it does NOT set the maximum command length. The effective line-length
    // limit is the parser's own ELM327_RX_BUF_SIZE (32): ELM327Output drops
    // chars once its line buffer fills, identical to the serial ELM327 path.
    // Real ELM327 commands (ATxx, OBD hex queries) are well under that.
    static constexpr size_t kInCap  = 64;
    static constexpr size_t kOutCap = 256;

    // ---- inbound (parser reads) ----
    void loadIn(const uint8_t* data, size_t n) {
        if (n > kInCap) n = kInCap;
        memcpy(inBuf, data, n);
        inLen = n;
        inPos = 0;
    }
    int available() override { return static_cast<int>(inLen - inPos); }
    int read() override {
        if (inPos >= inLen) return -1;
        return inBuf[inPos++];
    }
    int peek() override {
        if (inPos >= inLen) return -1;
        return inBuf[inPos];
    }

    // ---- outbound (parser writes) ----
    size_t write(uint8_t c) override {
        if (outLen < kOutCap) outBuf[outLen++] = c;
        return 1;  // claim acceptance even on drop; ELM327 has no flow contract
    }
    using Print::write;  // keep Print::write(const uint8_t*, size_t)

    size_t   outPending() const { return outLen - outSent; }
    const uint8_t* outPtr() const { return outBuf + outSent; }
    void     outConsume(size_t n) {
        outSent += n;
        if (outSent >= outLen) { outSent = 0; outLen = 0; }  // fully drained
    }

private:
    uint8_t inBuf[kInCap];
    size_t  inLen = 0;
    size_t  inPos = 0;
    uint8_t outBuf[kOutCap];
    size_t  outLen  = 0;
    size_t  outSent = 0;
};

FramedElmStream g_bridge;
ELM327Output    g_elm;  // dedicated instance: per-channel session state, kept
                        // independent of the serial-bound elm327Output singleton

// Send as many full 0x30 frames as the transport accepts; stop at the first
// back-pressured send and leave the remainder for the next handleInbound call.
void drainOut(MuxTransport& out) {
    while (g_bridge.outPending() > 0) {
        size_t n = g_bridge.outPending();
        if (n > FRAME_MAX_PAYLOAD) n = FRAME_MAX_PAYLOAD;
        if (!out.sendFrame(FRAME_TYPE_ELM327, g_bridge.outPtr(),
                           static_cast<uint16_t>(n))) {
            return;
        }
        g_bridge.outConsume(n);
    }
}

} // namespace

void init() {
    g_elm.setSerial(&g_bridge);
    g_elm.begin();
}

void handleInbound(const uint8_t* payload, uint16_t len, MuxTransport& out) {
    // Flush anything a prior call couldn't send before generating more output,
    // so responses stay in order on the wire.
    drainOut(out);

    size_t i = 0;
    do {
        size_t n = len - i;
        if (n > FramedElmStream::kInCap) n = FramedElmStream::kInCap;
        g_bridge.loadIn(payload + i, n);
        g_elm.update();          // consumes the chunk; may emit a response
        i += n;
        drainOut(out);
    } while (i < len);
}

} // namespace Elm327Framed

#else  // ENABLE_FRAMED_TRANSPORT && ENABLE_ELM327

namespace Elm327Framed {
void init() {}
void handleInbound(const uint8_t*, uint16_t, MuxTransport&) {}
}  // namespace Elm327Framed

#endif
