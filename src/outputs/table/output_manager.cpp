/*
 * output_manager.cpp - Runs the outputs from their records (#612 phase 2b-1)
 *
 * output_table.h holds the records (what exists, where, enabled, interval).
 * This file starts and stops the code behind each record, paces the push
 * formats on a timer per output, and prints the table.
 *
 *   CSV on a stream, REALDASH  push, several (one per stream): sendToOutputs()
 *                              writes each routed input to the output's
 *                              transport, every `interval` ms
 *   CSV on SD                  one; output_sdlog.cpp owns its cadence
 *   OBD                        one; attaches to its CAN bus (output_can.cpp)
 *   ELM327 on a serial port    one; binds the port (output_elm327.cpp)
 *   ELM327 on BLE              one; the framed link's 0x30 channel (elm327_framed.cpp)
 *   the alarm                  built in, no record (systemConfig.alarmEnabled)
 */

#include "output_base.h"
#include "output_table.h"
#include "output_admission.h"
#include "../../config.h"
#include "../../inputs/input_manager.h"
#include "../../core/message_api.h"
#include "../../transports/transport_registry.h"
#include "../../core/system_mode.h"
#include "../formats/output_sdlog.h"
#include "../formats/output_format.h"   // sdLogFormatName for OUTPUT LIST
#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_ELM327
#include "../formats/elm327_framed.h"
#endif
#include "../formats/realdash_framed.h"   // RealDash on BLE (#612 2c); stubs when not built
#if INCLUDE_FRAMED_TRANSPORT
#include "../../transports/transport_mux.h"
extern MuxTransport hwSerial1;   // the framed port (main.cpp), as the applink broadcasters
#endif

// BLE outputs (ELM327, RealDash) run only while the framed nRF link is up
static bool bleLinkUp() {
#if INCLUDE_FRAMED_TRANSPORT
    return hwSerial1.isFramedEnabled();
#else
    return false;
#endif
}

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

extern void initCAN(uint8_t id);
extern void stopCAN();
extern void updateCAN();

extern void initAlarmOutput();
extern void updateAlarmOutput();
extern void stopAlarmOutput();

#if INCLUDE_ELM327
extern void initELM327(uint8_t id);
extern void updateELM327();
extern void stopELM327();
extern void noteELM327PortState(uint8_t id);
extern uint8_t elm327BoundPort();
#endif

// What each output runs as now: its format and transport when it started
// (FMT_NONE: not running). A record that no longer matches is restarted.
static uint8_t  liveFormat[MAX_OUTPUTS];
static uint8_t  liveTransport[MAX_OUTPUTS];
static uint32_t lastSend[MAX_OUTPUTS];
static bool     alarmLive = false;

static bool isLive(uint8_t id) { return id < MAX_OUTPUTS && liveFormat[id] != FMT_NONE; }

// Start the code behind output <id> from its record. It counts as running
// only once its transport took it (OBD attached, ELM327 bound, a stream
// registered and started); otherwise it stays stopped and the next sync
// (LOAD, JSON IMPORT, BUS ... ENABLE) or OUTPUT <id> ENABLE tries again.
// Admission runs here too, so a stored record that no longer fits the
// console, the log or the framed ports (SystemConfig reset or LOADed under it)
// is left stopped with the reason rather than run on someone else's stream.
static FLASHMEM bool startOutput(uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    if (!r) return false;
    const OutputFormat fmt = (OutputFormat)r->format;
    const __FlashStringHelper* why = outputAdmitReason(id, fmt, r->transport, systemConfig, outputRecords);
    if (!why && outputSingletonHolder(id, fmt, r->transport, outputRecords) != OUT_ID_NONE) {
        why = F("an output of this kind with a lower id runs instead");
    }
    if (why) {
        msg.control.print(F("NOTE: output "));
        msg.control.print(id);
        msg.control.print(F(" not started: it "));
        msg.control.println(why);
        return false;
    }
    bool running = true;
    switch (fmt) {
#if INCLUDE_CAN
    case FMT_OBD:
        initCAN(id);
        running = canOutputAttachedBus() != 0xFF;
        break;
#endif
#if INCLUDE_ELM327
    case FMT_ELM327:
#if INCLUDE_FRAMED_TRANSPORT
        // BLE outputs run only while the framed link is up (#612 2c)
        if (r->transport == OT_BLE) { Elm327Framed::start(id); running = bleLinkUp(); break; }
#endif
        initELM327(id);
        running = elm327BoundPort() != 0xFF;
        break;
#endif
    case FMT_CSV:
#if INCLUDE_SD_LOGGING
        if (r->transport == OT_SD) { initSDLog(id); break; }
#endif
        running = transportGet(r->transport) != nullptr;
        break;
    case FMT_REALDASH:
        if (r->transport == OT_BLE) { RealdashFramed::start(); running = bleLinkUp(); break; }
        running = transportGet(r->transport) != nullptr;
        break;
    default: break;
    }
    if (!running) {
        // Undo a start that didn't take, so nothing runs that isn't live
        if (r->transport == OT_BLE && fmt == FMT_REALDASH) RealdashFramed::stop();
#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_ELM327
        if (r->transport == OT_BLE && fmt == FMT_ELM327) Elm327Framed::stop();
#endif
        return false;
    }
    liveFormat[id] = r->format;
    liveTransport[id] = r->transport;
    lastSend[id] = 0;
    return true;
}

// Stop it, as what it was started as
static FLASHMEM void stopOutput(uint8_t id) {
    if (!isLive(id)) return;
    const uint8_t fmt = liveFormat[id], t = liveTransport[id];
    liveFormat[id] = FMT_NONE;
    liveTransport[id] = OT_NONE;
    switch (fmt) {
#if INCLUDE_CAN
    case FMT_OBD: stopCAN(); break;
#endif
#if INCLUDE_ELM327
    case FMT_ELM327:
#if INCLUDE_FRAMED_TRANSPORT
        if (t == OT_BLE) { Elm327Framed::stop(); break; }
#endif
        stopELM327();
        break;
#endif
#if INCLUDE_SD_LOGGING
    case FMT_CSV:
        if (t == OT_SD) closeSDLog();
        break;
#endif
    case FMT_REALDASH:
        if (t == OT_BLE) RealdashFramed::stop();
        break;
    default: break;
    }
    (void)t;
}

FLASHMEM void initOutputModules() {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        liveFormat[id] = FMT_NONE;
        if (outputIsEnabled(id)) startOutput(id);
    }
    alarmLive = systemConfig.alarmEnabled != 0;
    if (alarmLive) initAlarmOutput();
}

// A transport as a Print, for the encoders
class TransportPrint : public Print {
public:
    TransportInterface* t = nullptr;
    size_t write(uint8_t c) override { return t->write(c); }
    size_t write(const uint8_t* b, size_t n) override { return t->write(b, n); }
};
static TransportPrint streamOut;

// Has the console fallen back to USB (#558)? Asked of the console through a
// probe main.cpp registers (console/console_routing.h: consoleOnUsbFallback),
// so the answer is the live console's, and outputs/ needn't include console/.
// Unregistered: never.
static OutputConsoleFallbackProbe s_consoleFallbackProbe = nullptr;
void setOutputConsoleFallbackProbe(OutputConsoleFallbackProbe probe) { s_consoleFallbackProbe = probe; }
static bool consoleFellBackToUsb() { return s_consoleFallbackProbe && s_consoleFallbackProbe(); }

// Where a stream output writes: its transport if registered and started, and
// not USB while the console has fallen back to it (#558): the console wins
static Print* streamFor(uint8_t transport) {
    if (transport == OT_USB && consoleFellBackToUsb()) return nullptr;
    TransportInterface* t = transportGet(transport);
    if (!t) return nullptr;
    streamOut.t = t;
    return &streamOut;
}

void sendToOutputs(uint32_t now) {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const uint8_t fmt = liveFormat[id];
        if (fmt != FMT_CSV && fmt != FMT_REALDASH) continue;
        if (liveTransport[id] == OT_SD) continue;   // the SD log paces itself
        const OutputRecord* r = outputRecord(id);
        if (!r || now - lastSend[id] < r->interval) continue;
        lastSend[id] = now;
        // CSV stays quiet in CONFIG: it shares its stream with the console
        if (fmt == FMT_CSV && isInConfigMode()) continue;
        // RealDash on BLE: a whole pass, or none while the last one is still
        // leaving (realdash_framed.h)
        Print* out = r->transport == OT_BLE ? RealdashFramed::beginPass() : streamFor(r->transport);
        if (!out) continue;
        const RouteMask bit = (RouteMask)(1u << id);
        for (uint8_t j = 0; j < MAX_INPUTS; j++) {
            Input& in = inputs[j];
            if (!in.flags.isEnabled || isnan(in.value) || !(in.routeMask & bit)) continue;
            if (fmt == FMT_CSV) sendCsvLine(&in, *out);
            else                sendRealdashFrame(&in, *out);
        }
        if (r->transport == OT_BLE) RealdashFramed::tick();
    }
}

void updateOutputs() {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        switch (liveFormat[id]) {
#if INCLUDE_CAN
        case FMT_OBD: updateCAN(); break;
#endif
#if INCLUDE_ELM327
        case FMT_ELM327: if (liveTransport[id] != OT_BLE) updateELM327(); break;
#endif
#if INCLUDE_SD_LOGGING
        case FMT_CSV: if (liveTransport[id] == OT_SD) updateSDLog(); break;
#endif
        case FMT_REALDASH: if (liveTransport[id] == OT_BLE) RealdashFramed::tick(); break;
        default: break;
        }
    }
    if (alarmLive) updateAlarmOutput();
}

// ===== Changes by id =====

FLASHMEM bool outputSetEnabledById(uint8_t id, bool enabled) {
    OutputRecord* r = outputRecord(id);
    if (!r) return false;
    r->enabled = enabled ? 1 : 0;
    // ENABLE always restarts: it is how a user retries a faulted SD log or an
    // ELM327 whose port wasn't up (#610)
    stopOutput(id);
    if (enabled) startOutput(id);
    return true;
}

FLASHMEM bool outputSetIntervalById(uint8_t id, long ms) {
    OutputRecord* r = outputRecord(id);
    if (!r || !outputIntervalValid((OutputFormat)r->format, r->transport, ms)) return false;
    r->interval = (uint16_t)ms;
    return true;
}

FLASHMEM bool outputSetTransportById(uint8_t id, uint8_t transport) {
    OutputRecord* r = outputRecord(id);
    if (!r || outputTransportKind(transport) != outputTransportKind(r->transport)) return false;
    r->transport = transport;
    // Restart an enabled one, also one that wasn't running: the new port may be up
    if (r->enabled) { stopOutput(id); startOutput(id); }
    return true;
}

FLASHMEM bool outputSetOptionById(uint8_t id, uint8_t option) {
    OutputRecord* r = outputRecord(id);
    if (!r || !(r->format == FMT_CSV && r->transport == OT_SD)) return false;
    r->option = option;
    return true;
}

bool outputLiveById(uint8_t id) { return isLive(id); }

// Route bit <id> on every input: set on ADD (a new output is sent everything),
// cleared on DEL (a later output at this id starts clean)
static FLASHMEM void routeEveryInput(uint8_t id, bool on) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) continue;
        if (on) inputs[i].routeMask |= (uint8_t)(1u << id);
        else    inputs[i].routeMask &= (uint8_t)~(1u << id);
    }
}

FLASHMEM uint8_t outputAddRecord(OutputFormat format, uint8_t transport) {
    const uint8_t id = outputCreate(format, transport, true);
    if (id == OUT_ID_NONE) return OUT_ID_NONE;
    routeEveryInput(id, true);
    startOutput(id);
    return id;
}

FLASHMEM void outputDeleteById(uint8_t id) {
    if (!outputExists(id)) return;
    stopOutput(id);
    routeEveryInput(id, false);
    outputErase(id);
}

FLASHMEM void syncOutputsToConfig() {
    // Two passes: every stop before any start. The one-per-kind formats run
    // from one set of state each, so a stop at a higher id after a start at a
    // lower one (an import or LOAD that moved OBD from id 1 to id 0) would
    // tear down what was just started.
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const OutputRecord* r = outputRecord(id);
        if (!isLive(id)) continue;
        bool stop = !r || !r->enabled || liveFormat[id] != r->format || liveTransport[id] != r->transport;
        // ... or that no longer fits where it runs: a LOAD or import that moved
        // the console, the log or a framed port onto its stream, or turned
        // OBD's bus off or to LISTEN (the start pass then says why)
        if (!stop && outputAdmitReason(id, (OutputFormat)r->format, r->transport, systemConfig, outputRecords)) stop = true;
#if INCLUDE_CAN
        if (!stop && r->format == FMT_OBD) {
            const uint8_t bus = outputTransportCanBus(r->transport);
            if (bus != 0xFF && systemConfig.buses.can[bus].mode != CAN_BUS_NORMAL) stop = true;
        }
#endif
        if (stop) stopOutput(id);
    }
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const OutputRecord* r = outputRecord(id);
        if (!r || !r->enabled || isLive(id)) continue;
        startOutput(id);
#if INCLUDE_ELM327
        // initELM327() skips a port that isn't running yet, so say so
        if (r->format == FMT_ELM327 && r->transport != OT_BLE) noteELM327PortState(id);
#endif
    }
    const bool alarmWant = systemConfig.alarmEnabled != 0;
    if (alarmWant != alarmLive) setAlarmEnabled(alarmWant);
}

FLASHMEM void setAlarmEnabled(bool enabled) {
    systemConfig.alarmEnabled = enabled ? 1 : 0;
    if (enabled) {
        alarmLive = true;
        initAlarmOutput();
    } else if (alarmLive) {
        alarmLive = false;
        // Silence the buzzer and blank the LED, or they hold their last state
        // once updateAlarmOutput() stops running (#610)
        stopAlarmOutput();
    }
}

// ===== Printing =====

FLASHMEM void printOutputLabel(uint8_t id) {
    char t[12];
    outputTransportName(outputTransportOf(id), t, sizeof(t));
    msg.control.print(outputFormatName(outputFormatOf(id)));
    msg.control.print(F(" on "));
    msg.control.print(t);
}

// One line of OUTPUT LIST, also the head of OUTPUT <id>
static FLASHMEM void printOutputLine(uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    if (!r) return;
    msg.control.print(id);
    msg.control.print(F(": "));
    printOutputLabel(id);
    msg.control.print(F(" - "));
    msg.control.print(r->enabled ? F("enabled") : F("disabled"));
    // Enabled but stopped: its transport isn't up, or it wasn't admitted
    // (OUTPUT <id> ENABLE retries and says why)
    if (r->enabled && !isLive(id)) msg.control.print(F(", not running"));
#if INCLUDE_FRAMED_TRANSPORT
    if (r->transport == OT_BLE && !systemConfig.serial.framed_enabled_mask) {
        msg.control.print(F(" (no framed port: BUS SERIAL <n> FRAMED ENABLE)"));
    }
#endif
    if (outputFormatPaced((OutputFormat)r->format)) {
        msg.control.print(F(", interval "));
        msg.control.print(r->interval);
        msg.control.print(F(" ms"));
    }
#if INCLUDE_SD_LOGGING
    if (r->format == FMT_CSV && r->transport == OT_SD) {
        msg.control.print(F(", "));
        msg.control.print(sdLogFormatName(r->option));
    }
#endif
    msg.control.println();
}

FLASHMEM void listOutputTable() {
    msg.control.println(F("=== Outputs ==="));
    if (outputCount() == 0) msg.control.println(F("(none - OUTPUT ADD <format> <transport>)"));
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (outputExists(id)) printOutputLine(id);
    }
    // Built-in outputs: always present, no id, no routes (ARCHITECTURE.md §14.3)
    msg.control.println(F("Built-in (no routes):"));
    msg.control.print(F("  Alarm - "));
    msg.control.println(systemConfig.alarmEnabled ? F("enabled") : F("disabled"));
#if INCLUDE_LCD || INCLUDE_OLED
    msg.control.println(F("  Display - see DISPLAY STATUS"));
#endif
#if INCLUDE_FRAMED_TRANSPORT
    msg.control.println(F("  App link (BLE)"));
#endif
}

FLASHMEM bool printOutputDetail(uint8_t id) {
    if (!outputExists(id)) return false;
    printOutputLine(id);
    msg.control.print(F("  Routed inputs:"));
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF || !(inputs[i].routeMask & (1u << id))) continue;
        msg.control.print(' ');
        msg.control.print(inputs[i].abbrName);
        n++;
    }
    if (n == 0) msg.control.print(F(" none"));
    msg.control.println();
#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_REALDASH
    // Passes skipped because the phone wasn't keeping up (realdash_framed.h)
    if (isLive(id) && liveFormat[id] == FMT_REALDASH && liveTransport[id] == OT_BLE) {
        msg.control.print(F("  Passes skipped (link busy): "));
        msg.control.println(RealdashFramed::skippedPasses());
    }
#endif
    return true;
}
