/*
 * output_admission.cpp - see output_admission.h
 */

#include "output_admission.h"
#include "../../hal/bus_defaults.h"   // NUM_CAN_BUSES: the refusal's hint
#if INCLUDE_CAN_LAYOUTS
#include "../formats/can_layout_output.h"   // PREOBD_BASE_ID, PREOBD_FRAMES
#include "../../buses/bus_config.h"         // CAN_BUS_LISTEN, CAN_BUS_SLOTS
#endif

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

FLASHMEM bool outputOnCanInputBus(uint8_t transport, const SystemConfig& cfg) {
    if (outputTransportKind(transport) != OTK_CAN || cfg.buses.canInputBus == 0xFF) return false;
    return outputTransportCanBus(transport) == cfg.buses.canInputBus;
}

#if INCLUDE_CAN_LAYOUTS
// The CAN IDs each sending format owns on its bus: OBD 0x7DF/0x7E0/0x7E8,
// PREOBD [PREOBD_BASE_ID, +PREOBD_FRAMES)
static bool ownsCanId(uint8_t format, uint16_t id) {
    if (format == FMT_OBD) return id == 0x7DF || id == 0x7E0 || id == 0x7E8;
    if (format == FMT_PREOBD) return id >= PREOBD_BASE_ID && id < PREOBD_BASE_ID + PREOBD_FRAMES;
    return false;
}

FLASHMEM bool outputCanIdsOverlap(OutputFormat a, OutputFormat b) {
    static const uint16_t kObdIds[] = {0x7DF, 0x7E0, 0x7E8};
    for (uint8_t i = 0; i < sizeof(kObdIds) / sizeof(kObdIds[0]); i++) {
        if (ownsCanId(a, kObdIds[i]) && ownsCanId(b, kObdIds[i])) return true;
    }
    for (uint16_t id = PREOBD_BASE_ID; id < PREOBD_BASE_ID + PREOBD_FRAMES; id++) {
        if (ownsCanId(a, id) && ownsCanId(b, id)) return true;
    }
    return false;
}
#endif

FLASHMEM const __FlashStringHelper* outputAdmitReason(uint8_t selfId, OutputFormat format, uint8_t transport,
                                                     const SystemConfig& cfg, const OutputRecord* table,
                                                     bool yieldToAll) {
    (void)yieldToAll;   // used only by the CAN-ID check (INCLUDE_CAN_LAYOUTS)
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
    // One job per CAN bus (#632): CAN input reads the car on its bus, and
    // nothing that sends may run there. Keyed on the transport, not the format,
    // so the #612 layouts inherit it. A disabled record there is fine (the
    // default seed makes one); it is refused when it is enabled or started.
    if (outputTransportKind(transport) == OTK_CAN) {
        if (!outputOnCanInputBus(transport, cfg)) {
#if INCLUDE_CAN_LAYOUTS
            // Every CAN sender: not on a LISTEN bus (it never transmits), and no
            // two enabled outputs on one bus with CAN IDs in common. The lowest
            // id keeps the IDs where a loaded table holds two.
            const uint8_t bus = outputTransportCanBus(transport);
            if (bus < CAN_BUS_SLOTS && cfg.buses.can[bus].mode == CAN_BUS_LISTEN) {
                return F("sends on a LISTEN bus, which never transmits (BUS CAN <bus> ENABLE)");
            }
            const bool selfOn = !yieldToAll && selfId < MAX_OUTPUTS && table[selfId].enabled;
            for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
                if (id == selfId) continue;
                const OutputRecord& r = table[id];
                if (r.format == FMT_NONE || !r.enabled || outputTransportCanBus(r.transport) != bus) continue;
                if (selfOn && id > selfId) continue;
                if (outputCanIdsOverlap((OutputFormat)r.format, format)) {
                    return F("shares CAN IDs with another enabled output on this bus (one PREOBD per bus)");
                }
            }
#endif
            return nullptr;
        }
#if NUM_CAN_BUSES >= 2
        return F("can't share CAN input's bus (one job per bus): OUTPUT <id> TRANSPORT, or BUS CAN <bus> INPUT DISABLE");
#else
        return F("can't share CAN input's bus (one job per bus)");   // nowhere to move it; mega near-PROGMEM is tight
#endif
    }
    if (outputTransportKind(transport) != OTK_STREAM) return nullptr;   // SD: no stream to share

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
