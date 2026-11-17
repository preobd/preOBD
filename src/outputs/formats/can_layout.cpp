/*
 * can_layout.cpp - the CAN layout engine (see can_layout.h): the input map,
 * frame encoding and pass pacing. Pure; test_can_layout includes it directly.
 */

#include "../../config.h"

#if INCLUDE_CAN_LAYOUTS

#include <Arduino.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include "can_layout.h"
#include "obd_encode.h"   // obdConvertUnits
#include "../../inputs/input.h"
#include "../../inputs/pressure_reference.h"   // applicationReference (pure)

static_assert(LAYOUT_STD_BARO_BAR == STD_BARO_BAR, "the engine's default baro is the inputs layer's");

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM void canLayoutSignalAt(const CanLayout& layout, uint8_t i, CanLayoutSignal& out) {
    memcpy_P(&out, &layout.signals[i], sizeof(CanLayoutSignal));
}

static uint16_t frameIdOf(uint32_t baseId, const uint16_t* frameIds, uint8_t f) {
    return frameIds ? (uint16_t)pgm_read_word(&frameIds[f]) : (uint16_t)(baseId + f);
}

FLASHMEM uint16_t canLayoutFrameId(const CanLayout& layout, uint8_t f) {
    return frameIdOf(layout.baseId, layout.frameIds, f);
}

// ---- The map ----

FLASHMEM void canLayoutBuildMap(const CanLayout& layout, const Input* inputs, uint8_t nInputs, uint8_t* map) {
    for (uint8_t i = 0; i < layout.count; i++) {
        CanLayoutSignal s;
        canLayoutSignalAt(layout, i, s);
        map[i] = LAYOUT_NO_INPUT;
        // The instance-th configured input of this application in pin order:
        // repeatedly take the lowest pin above the one taken before
        int prevPin = -1;
        uint8_t found = LAYOUT_NO_INPUT;
        for (uint8_t n = 0; n < s.instance; n++) {
            found = LAYOUT_NO_INPUT;
            int best = 1000;
            for (uint8_t j = 0; j < nInputs; j++) {
                if (inputs[j].pin == 0xFF || inputs[j].applicationIndex != s.app) continue;
                if ((int)inputs[j].pin > prevPin && (int)inputs[j].pin < best) {
                    best = inputs[j].pin;
                    found = j;
                }
            }
            if (found == LAYOUT_NO_INPUT) break;
            prevPin = best;
        }
        map[i] = found;
    }
}

void canLayoutMapMarkStale(CanLayoutMap& m) { m.stale = true; }

FLASHMEM bool canLayoutMapFresh(CanLayoutMap& m, const CanLayout& layout, const Input* inputs, uint8_t nInputs) {
    if (!m.stale) return false;
    canLayoutBuildMap(layout, inputs, nInputs, m.slot);
    m.stale = false;
    return true;
}

// ---- Frame encoding ----

bool canLayoutEncodeSignal(const CanLayoutSignal& sig, const Input& in, uint8_t routeBit, uint8_t* out,
                           float baroBar, bool reserveAllOnes) {
    if (!in.flags.isEnabled || !in.flags.hasReading || !(in.routeMask & routeBit)) return false;
    if (in.measurementType != sig.measurementType) return false;
    if (!isfinite(in.value)) return false;
    float bar = in.value;
    // An absolute field: a gauge application gets the baro added (§14.8), in
    // bar, before the unit conversion
    if (sig.flags & LAYOUT_ABSOLUTE) {
        bar += pressureReferenceOffset(applicationReference(sig.app), REF_ABSOLUTE, baroBar);
    }
    float v = obdConvertUnits(bar, sig.unitsIndex);
    // A huge finite value can overflow to inf in the conversion; it must still
    // clamp to the field's max, not read as no data
    if (v > FLT_MAX) v = FLT_MAX;
    else if (v < -FLT_MAX) v = -FLT_MAX;
    return encodeScaledField(v, sig.scale, sig.offset, sig.length, sig.flags & LAYOUT_FIELD_FLAGS,
                             reserveAllOnes, out) != 0;
}

bool canLayoutBuildFrame(const CanLayout& layout, const uint8_t* map, const Input* inputs,
                         uint8_t nInputs, uint8_t routeBit, uint8_t frame, uint8_t* out8, float baroBar) {
    const bool zeroNoData = (layout.flags & CAN_LAYOUT_ZERO_NO_DATA) != 0;
    bool mapped = false;
    memset(out8, 0, 8);
    for (uint8_t i = 0; i < layout.count; i++) {
        CanLayoutSignal s;
        canLayoutSignalAt(layout, i, s);
        if (s.frame != frame || s.startByte + s.length > 8) continue;
        uint8_t* field = out8 + s.startByte;
        if (map[i] < nInputs) {
            // A frame is sent when one of its signals has an input routed here.
            // A disabled or faulted one still counts: it sends 0, as a Haltech
            // ECU does for a sensor in error; an unrouted one is switched off (§14.6)
            if (inputs[map[i]].routeMask & routeBit) mapped = true;
            if (canLayoutEncodeSignal(s, inputs[map[i]], routeBit, field, baroBar, !zeroNoData)) continue;
        }
        if (zeroNoData) memset(field, 0, s.length);
        else            fieldAllOnes(s.length, field, s.flags & LAYOUT_FIELD_FLAGS);
    }
    return mapped || !zeroNoData;
}

// ---- Pacing ----

FLASHMEM void layoutPassInit(LayoutPass& p, uint8_t frames, uint32_t baseId, const uint16_t* frameIds) {
    memset(&p, 0, sizeof(p));
    p.frames = frames > LAYOUT_MAX_FRAMES ? LAYOUT_MAX_FRAMES : frames;
    p.baseId = baseId;
    p.frameIds = frameIds;
}

void layoutPassReset(LayoutPass& p) {
    p.pending = false;
    p.next = 0;
    p.primed = false;
}

void layoutPassPace(LayoutPass& p, uint32_t now, uint16_t intervalMs, uint8_t bus, const LayoutIO& io) {
    if (!p.primed) {
        p.primed = true;
        p.lastSend = now - intervalMs;   // the first pass is due now
    }
    if ((int32_t)(now - p.lastSend) < (int32_t)intervalMs) return;
    p.lastSend += intervalMs;
    if ((int32_t)(now - p.lastSend) >= (int32_t)intervalMs) p.lastSend = now;   // long gap: no catch-up burst

    if (p.pending) {
        // The last pass never left: nobody is ACKing, or the bus is stuck.
        // Drop what is queued so the queue stays bounded and a receiver that
        // appears gets current frames; if another consumer's exchange is in
        // flight, leave it alone and keep draining this pass.
        if (!io.abort || !io.abort(io.ctx, bus)) {
            if (p.skipped < 0xFFFF) p.skipped++;
            return;
        }
        if (p.unacked < 0xFFFF) p.unacked++;
    }
    p.sendMask = 0;
    for (uint8_t f = 0; f < p.frames; f++) {
        if (io.build(io.ctx, f, p.buf[f])) p.sendMask |= (uint16_t)(1u << f);
    }
    p.next = 0;
    p.pending = true;
    p.passes++;
}

bool layoutPassDrain(LayoutPass& p, uint8_t bus, const LayoutIO& io) {
    if (!p.pending) return true;
    while (p.next < p.frames) {
        if (p.sendMask & (1u << p.next)) {
            if (!io.write(io.ctx, bus, frameIdOf(p.baseId, p.frameIds, p.next), p.buf[p.next])) return false;
        }
        p.next++;
    }
    p.pending = false;
    return true;
}

uint8_t layoutTick(LayoutPort* ports, uint8_t n, uint32_t now, const LayoutIO* ios, uint8_t* rr) {
    for (uint8_t i = 0; i < n; i++) {
        if (ports[i].live) layoutPassPace(ports[i].pass, now, ports[i].interval, ports[i].bus, ios[i]);
    }
    uint8_t drains = 0;
    uint8_t busDone = 0;   // buses (bit per bus) already drained this tick
    for (uint8_t k = 0; k < n; k++) {
        const uint8_t i = (uint8_t)((*rr + k) % n);
        if (!ports[i].live || !ports[i].pass.pending) continue;
        const uint8_t bit = (uint8_t)(1u << (ports[i].bus & 7));
        if (busDone & bit) continue;
        busDone |= bit;
        layoutPassDrain(ports[i].pass, ports[i].bus, ios[i]);
        drains++;
    }
    if (n) *rr = (uint8_t)((*rr + 1) % n);
    return drains;
}

FLASHMEM uint16_t canLayoutBusLoadX10(uint8_t frames, uint16_t intervalMs, uint32_t baud) {
    if (baud == 0 || intervalMs == 0) return 0;
    const uint64_t bitsPerSecond = (uint64_t)frames * 111u * 1000u / intervalMs;
    const uint64_t x10 = bitsPerSecond * 1000u / baud;
    return x10 > 9999 ? 9999 : (uint16_t)x10;
}

#endif  // INCLUDE_CAN_LAYOUTS
