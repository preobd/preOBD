/*
 * output_admission.cpp - see output_admission.h
 */

#include "output_admission.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// The console's or the log's transports name this stream (TransportID values
// are the stream OutputTransport values)
static bool consoleOrLogOn(uint8_t t, const SystemConfig& cfg) {
    return cfg.console.primary == t || cfg.console.secondary == t ||
           cfg.log.primary == t || cfg.log.secondary == t;
}

static bool framedPort(uint8_t t, const SystemConfig& cfg) {
    const uint8_t port = outputTransportSerialPort(t);
    return port != 0xFF && (cfg.serial.framed_enabled_mask & (1u << (port - 1)));
}

static bool needsStreamAlone(uint8_t format) {
    return format == FMT_REALDASH || format == FMT_ELM327;
}

FLASHMEM uint8_t outputOnStream(uint8_t transport, bool anyOutput, const OutputRecord* table) {
    if (outputTransportKind(transport) != OTK_STREAM) return OUT_ID_NONE;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const OutputRecord& r = table[id];
        if (r.format == FMT_NONE || !r.enabled || r.transport != transport) continue;
        if (anyOutput || needsStreamAlone(r.format)) return id;
    }
    return OUT_ID_NONE;
}

FLASHMEM const __FlashStringHelper* outputAdmitReason(uint8_t selfId, OutputFormat format, uint8_t transport,
                                                     const SystemConfig& cfg, const OutputRecord* table) {
    if (!outputFormatBuilt(format)) return F("is not in this build");
    if (!outputTransportAllowed(format, transport)) return F("can't use that transport");
    // BLE is the nRF's one data channel (the phone's FFE1): one enabled
    // output on it, ELM327 or RealDash (#612 2c; a second channel, #620, is not planned)
#if INCLUDE_FRAMED_TRANSPORT   // BLE exists only with the nRF link
    if (outputTransportKind(transport) == OTK_BLE) {
        for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
            if (id == selfId) continue;
            const OutputRecord& r = table[id];
            if (r.format == FMT_NONE || !r.enabled || outputTransportKind(r.transport) != OTK_BLE) continue;
            return F("can't share BLE with another enabled output (BLE carries one data output; OUTPUT LIST shows it)");
        }
        return nullptr;
    }
#endif
    if (outputTransportKind(transport) != OTK_STREAM) return nullptr;   // CAN, SD: no stream to share

    if (framedPort(transport, cfg)) {
        return format == FMT_ELM327 ? F("can't use the framed nRF link's port; ELM327 over it is OUTPUT ADD ELM327 BLE")
                                    : F("can't use the framed nRF link's port (it carries the console)");
    }
    if (needsStreamAlone(format) && consoleOrLogOn(transport, cfg)) {
        return F("needs the stream to itself, and the console or the log uses it (CONSOLE/LOG TRANSPORT to move them)");
    }
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (id == selfId) continue;
        const OutputRecord& r = table[id];
        if (r.format == FMT_NONE || !r.enabled || r.transport != transport) continue;
        return F("can't share a stream with another enabled output (one format per stream)");
    }
    return nullptr;
}

// The one-per-kind slot an output takes: OBD (any bus), CSV on SD, ELM327 on
// a serial port, ELM327 on BLE. Other outputs take none.
static bool singletonSlot(uint8_t format, uint8_t transport) {
    const OutputTransportKind kind = outputTransportKind(transport);
    return format == FMT_OBD || format == FMT_ELM327 || (format == FMT_CSV && kind == OTK_SD) ||
           (format == FMT_REALDASH && kind == OTK_BLE);
}

static bool sameSlot(const OutputRecord& r, uint8_t format, uint8_t transport) {
    if (r.format != format) return false;
    return format == FMT_OBD || outputTransportKind(r.transport) == outputTransportKind(transport);
}

FLASHMEM uint8_t outputSingletonHolder(uint8_t selfId, OutputFormat format, uint8_t transport,
                                       const OutputRecord* table) {
    if (!singletonSlot(format, transport)) return OUT_ID_NONE;
    if (selfId >= MAX_OUTPUTS) {   // OUTPUT ADD: any record in the slot holds it
        for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
            if (sameSlot(table[id], format, transport)) return id;
        }
        return OUT_ID_NONE;
    }
    // A table with two (a store or file OUTPUT ADD couldn't make): the lowest
    // enabled one keeps the slot, else the lowest
    uint8_t lowest = OUT_ID_NONE, lowestEnabled = OUT_ID_NONE;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (!sameSlot(table[id], format, transport)) continue;
        if (lowest == OUT_ID_NONE) lowest = id;
        if (table[id].enabled && lowestEnabled == OUT_ID_NONE) lowestEnabled = id;
    }
    const uint8_t winner = lowestEnabled != OUT_ID_NONE ? lowestEnabled : lowest;
    return winner == selfId ? OUT_ID_NONE : winner;
}

FLASHMEM const __FlashStringHelper* outputAddLimitReason(OutputFormat format, uint8_t transport,
                                                        const OutputRecord* table) {
    if (outputSingletonHolder(OUT_ID_NONE, format, transport, table) != OUT_ID_NONE) {
        return format == FMT_OBD ? F("there is already an OBD output; OUTPUT <id> TRANSPORT moves it")
                                 : F("there is already one of these; OUTPUT <id> TRANSPORT moves it");
    }
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (table[id].format == FMT_NONE) return nullptr;
    }
    return F("the output table is full (MAX_OUTPUTS); OUTPUT DEL one first");
}
