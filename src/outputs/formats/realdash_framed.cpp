/*
 * realdash_framed.cpp - see realdash_framed.h
 */

#include "realdash_framed.h"
#include "../../config.h"

#ifndef DMAMEM   // Teensy 4: RAM2; elsewhere ordinary RAM
#define DMAMEM
#endif

#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_REALDASH

#include "../../transports/transport_mux.h"
#include "../../transports/frame_codec.h"

extern MuxTransport hwSerial1;   // the framed port (main.cpp); as the applink broadcasters

namespace RealdashFramed {

namespace {

constexpr size_t kUnit = 16;                       // one RealDash frame
constexpr size_t kUnitsPerFrame = 3;               // 48 B payload + 5 = 53 B on the wire
static_assert(kUnitsPerFrame * kUnit + FRAME_HEADER_BYTES + 1 <= kMuxMaxSendableFrame,
              "a RealDash 0x31 frame must fit the smallest UART TX buffer (#528)");

// The pass's bytes. On Teensy 4 in RAM2 (DMAMEM): RAM1 is the stack's
DMAMEM uint8_t s_passBuf[MAX_INPUTS * kUnit];

// One pass: every routed input's frame, whole units only
class PassBuffer : public Print {
public:
    static constexpr size_t kCap = sizeof(s_passBuf);
    size_t len = 0;    // bytes collected
    size_t sent = 0;   // bytes sent (whole units)
    size_t write(uint8_t c) override {
        if (len >= kCap) return 0;
        s_passBuf[len++] = c;
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
        if (n > kCap - len) n = kCap - len;
        memcpy(s_passBuf + len, b, n);
        len += n;
        return n;
    }
    bool empty() const { return sent >= (len / kUnit) * kUnit; }
    void clear() { len = sent = 0; }
};

PassBuffer s_pass;
bool s_active = false;
uint32_t s_skipped = 0;

}  // namespace

void start() { s_pass.clear(); s_active = true; }
void stop()  { s_active = false; s_pass.clear(); }

Print* beginPass() {
    if (!s_active) return nullptr;
    if (!s_pass.empty()) { s_skipped++; return nullptr; }
    s_pass.clear();
    return &s_pass;
}

void tick() {
    if (!s_active) return;
    const size_t whole = (s_pass.len / kUnit) * kUnit;   // a short tail is never sent
    while (s_pass.sent < whole) {
        size_t n = whole - s_pass.sent;
        if (n > kUnitsPerFrame * kUnit) n = kUnitsPerFrame * kUnit;
        if (!hwSerial1.sendFrame(FRAME_TYPE_HM10_STREAM, s_passBuf + s_pass.sent, (uint16_t)n)) return;
        s_pass.sent += n;
    }
}

uint32_t skippedPasses() { return s_skipped; }

}  // namespace RealdashFramed

#else

namespace RealdashFramed {
void start() {}
void stop() {}
Print* beginPass() { return nullptr; }
void tick() {}
uint32_t skippedPasses() { return 0; }
}  // namespace RealdashFramed

#endif
