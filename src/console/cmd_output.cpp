/*
 * cmd_output.cpp - OUTPUT: the outputs you add (#612, ARCHITECTURE.md §14.5)
 *
 *   OUTPUT LIST [JSON]                         RUN ok
 *   OUTPUT <id>                                RUN ok
 *   OUTPUT ADD <format> <transport> [INTERVAL <ms>] [LONG|WIDE]
 *   OUTPUT <id> ENABLE | DISABLE
 *   OUTPUT <id> INTERVAL <ms>
 *   OUTPUT <id> TRANSPORT <transport>          the same kind: CAN<n>, SERIAL<n>, a stream
 *   OUTPUT <id> CSV LONG | WIDE                (the CSV output on SD)
 *   OUTPUT DEL <id>
 *
 * Outputs are stored records (output_table.h): ADD takes the lowest free id.
 * Every change that could put an output where it can't run goes through
 * output_admission.h first.
 */

#include "../config.h"
#include "command_handlers.h"
#include "command_helpers.h"
#include "../core/system_mode.h"
#include "../core/config_generation.h"
#include "../core/message_api.h"
#include "../transports/transport_registry.h"
#include "../config/system_config.h"
#include "../outputs/table/output_base.h"
#include "../outputs/table/output_table.h"
#include "../outputs/table/output_admission.h"
#include "../outputs/formats/output_format.h"
#include "../buses/bus_config.h"
#include "../buses/serial_manager.h"
#if SUPPORTS_JSON_EXPORT
#include "../config/json_registry.h"
#endif
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static FLASHMEM void printUsage() {
    msg.control.println(F("  Usage: OUTPUT LIST | <id> | ADD <format> <transport> [INTERVAL <ms>] | DEL <id>"));
    msg.control.println(F("         OUTPUT <id> ENABLE|DISABLE|INTERVAL <ms>|TRANSPORT <transport>"
#if INCLUDE_SD_LOGGING
                          "|CSV LONG|WIDE"
#endif
                          ));
}

// One digit -> that id (which may not exist); anything else -> OUT_ID_NONE
static uint8_t parseOutputIdToken(const char* s) {
    if (s == nullptr || !isdigit((unsigned char)s[0]) || s[1] != '\0') return OUT_ID_NONE;
    return (uint8_t)(s[0] - '0');
}

static FLASHMEM void printNoSuchOutput(const char* token) {
    msg.control.print(F("ERROR: No output '"));
    msg.control.print(token);
    msg.control.println(F("'"));
    msg.control.println(F("  OUTPUT LIST shows the outputs and their ids"));
}

// "ERROR: <FORMAT> on <transport> <reason>"
static FLASHMEM void printRefusal(OutputFormat fmt, uint8_t transport, const __FlashStringHelper* why) {
    char t[12];
    outputTransportName(transport, t, sizeof(t));
    msg.control.print(F("ERROR: "));
    msg.control.print(outputFormatName(fmt));
    msg.control.print(F(" on "));
    msg.control.print(t);
    msg.control.print(' ');
    msg.control.println(why);
}

// What each format may use, for the "can't use" refusal
static FLASHMEM void printTransportsFor(OutputFormat fmt) {
    msg.control.print(F("  It uses "));
    switch (fmt) {
    case FMT_CSV:      msg.control.println(F("USB, SERIAL1-8, ESP32_BT or SD")); break;
    case FMT_REALDASH: msg.control.println(F("USB, SERIAL1-8 or ESP32_BT")); break;
    case FMT_OBD:      msg.control.println(F("CAN1-CAN3")); break;
    case FMT_ELM327:   msg.control.println(F("SERIAL1-8 or BLE")); break;
    default:           msg.control.println(F("nothing in this build")); break;
    }
}

// Digits only, in range for this output; prints the range on failure.
static FLASHMEM bool parseInterval(OutputFormat fmt, uint8_t transport, const char* arg, unsigned long* out) {
    char* end = nullptr;
    const unsigned long v = isdigit((unsigned char)arg[0]) ? strtoul(arg, &end, 10) : 0;
    if (end == nullptr || *end != '\0' || !outputIntervalValid(fmt, transport, (long)v)) {
        msg.control.print(F("ERROR: Interval must be "));
        msg.control.print(outputIntervalMin(fmt, transport));
        msg.control.print(F("-"));
        msg.control.print(OUTPUT_INTERVAL_MAX_MS);
        msg.control.println(F(" ms"));
        return false;
    }
    *out = v;
    return true;
}

static FLASHMEM void printNoInterval(OutputFormat fmt) {
    msg.control.print(F("ERROR: "));
    msg.control.print(outputFormatName(fmt));
    msg.control.println(F(" answers requests as they arrive; it has no interval"));
}

// Can the output run on `transport` right now (beyond admission)? A serial
// port must be enabled, a CAN bus on and not LISTEN for OBD. Prints why not.
static FLASHMEM bool transportReady(OutputFormat fmt, uint8_t transport) {
    const uint8_t port = outputTransportSerialPort(transport);
    if (port != 0xFF && !isSerialPortActive(port)) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port);
        msg.control.print(F(" is not enabled. Run: BUS SERIAL "));
        msg.control.print(port);
        msg.control.println(F(" ENABLE <baud> first"));
        return false;
    }
    if (transport == OT_ESP32_BT && transportGet(transport) == nullptr) {
        msg.control.println(F("ERROR: ESP32_BT is not available (Bluetooth didn't start)"));
        return false;
    }
#if INCLUDE_CAN
    const uint8_t bus = outputTransportCanBus(transport);
    if (fmt == FMT_OBD && bus != 0xFF) {
        const uint8_t mode = systemConfig.buses.can[bus].mode;
        if (mode != CAN_BUS_NORMAL) {
            msg.control.print(F("ERROR: CAN"));
            msg.control.print(bus + 1);
            msg.control.println(mode == CAN_BUS_OFF ? F(" is off") : F(" is LISTEN, and OBD transmits"));
            msg.control.print(F("  Run: BUS CAN CAN"));
            msg.control.print(bus + 1);
            msg.control.println(F(" ENABLE first"));
            return false;
        }
    }
#endif
    (void)fmt;
    return true;
}

// Admission for output `selfId` (OUT_ID_NONE: a new one) enabled as fmt on
// transport, then the runtime check. Prints the refusal.
static FLASHMEM bool mayRun(uint8_t selfId, OutputFormat fmt, uint8_t transport) {
    const __FlashStringHelper* why = outputAdmitReason(selfId, fmt, transport, systemConfig, outputRecords);
    if (why) { printRefusal(fmt, transport, why); return false; }
    return transportReady(fmt, transport);
}

static FLASHMEM int outputAdd(int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: OUTPUT ADD requires a format and a transport"));
        msg.control.println(F("  Usage: OUTPUT ADD <format> <transport> [INTERVAL <ms>]"
#if INCLUDE_SD_LOGGING
                              " [LONG|WIDE]"
#endif
                              ));
        msg.control.println(F("  Formats: CSV, REALDASH, OBD, ELM327"));
        return 1;
    }
    const OutputFormat fmt = parseOutputFormat(argv[2]);
    if (fmt == FMT_NONE) {
        msg.control.print(F("ERROR: Unknown format '"));
        msg.control.print(argv[2]);
        msg.control.println(F("'"));
        msg.control.println(F("  Formats: CSV, REALDASH, OBD, ELM327"));
        return 1;
    }
    if (!outputFormatBuilt(fmt)) {
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(fmt));
        msg.control.println(F(" is not in this build"));
        return 1;
    }
    uint8_t transport;
    if (!parseOutputTransport(argv[3], &transport) || !outputTransportAllowed(fmt, transport)) {
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(fmt));
        msg.control.print(F(" can't use "));
        msg.control.println(argv[3]);
        printTransportsFor(fmt);
        return 1;
    }
    const __FlashStringHelper* full = outputAddLimitReason(fmt, transport, outputRecords);
    if (full) {
        msg.control.print(F("ERROR: "));
        msg.control.println(full);
        return 1;
    }

    // Options, all checked before anything changes
    unsigned long interval = 0;
    bool haveInterval = false;
    int layout = -1;
    for (int a = 4; a < argc; a++) {
        if (strcasecmp(argv[a], "INTERVAL") == 0) {
            if (a + 1 >= argc) { msg.control.println(F("ERROR: INTERVAL requires a time in ms")); return 1; }
            if (!outputFormatPaced(fmt)) { printNoInterval(fmt); return 1; }
            if (!parseInterval(fmt, transport, argv[a + 1], &interval)) return 1;
            haveInterval = true;
            a++;
            continue;
        }
#if INCLUDE_SD_LOGGING
        SdLogFormat f;
        if (fmt == FMT_CSV && transport == OT_SD && parseSdLogFormat(argv[a], f)) { layout = (int)f; continue; }
#endif
        msg.control.print(F("ERROR: Unexpected '"));
        msg.control.print(argv[a]);
        msg.control.println(F("'"));
        return 1;
    }

    if (!mayRun(OUT_ID_NONE, fmt, transport)) return 1;

    const uint8_t id = outputAddRecord(fmt, transport);
    if (id == OUT_ID_NONE) {
        msg.control.println(F("ERROR: the output table is full (MAX_OUTPUTS); OUTPUT DEL one first"));
        return 1;
    }
    if (haveInterval) outputSetIntervalById(id, (long)interval);
    if (layout >= 0) outputSetOptionById(id, (uint8_t)layout);
    msg.control.print(F("Output "));
    msg.control.print(id);
    msg.control.print(F(" added: "));
    printOutputLabel(id);
    msg.control.println();
    printSaveReminder();
    return 0;
}

static FLASHMEM int outputDel(int argc, const char* const* argv) {
    if (argc < 3) {
        msg.control.println(F("ERROR: OUTPUT DEL requires an output id"));
        return 1;
    }
    const uint8_t id = parseOutputIdToken(argv[2]);
    if (!outputExists(id)) { printNoSuchOutput(argv[2]); return 1; }
    outputDeleteById(id);
    msg.control.print(F("Output "));
    msg.control.print(id);
    msg.control.println(F(" deleted, and its routes"));
    printSaveReminder();
    return 0;
}

// OUTPUT <id> TRANSPORT <t>: within the same kind of transport, keeping the
// id and the routes (a different kind is DEL + ADD)
static FLASHMEM int outputTransport(uint8_t id, int argc, const char* const* argv) {
    const OutputRecord* r = outputRecord(id);
    uint8_t t;
    if (argc < 4 || !parseOutputTransport(argv[3], &t)) {
        msg.control.println(F("ERROR: TRANSPORT requires a transport (USB, SERIAL<n>, ESP32_BT, CAN<n>, SD, BLE)"));
        return 1;
    }
    const OutputFormat fmt = (OutputFormat)r->format;
    if (!outputTransportAllowed(fmt, t)) {
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(fmt));
        msg.control.print(F(" can't use "));
        msg.control.println(argv[3]);
        printTransportsFor(fmt);
        return 1;
    }
    if (outputTransportKind(t) != outputTransportKind(r->transport)) {
        msg.control.println(F("ERROR: a different kind of transport; OUTPUT DEL it, then OUTPUT ADD"));
        return 1;
    }
    if (r->enabled && !mayRun(id, fmt, t)) return 1;
    outputSetTransportById(id, t);
    msg.control.print(F("Output "));
    msg.control.print(id);
    msg.control.print(F(" is now "));
    printOutputLabel(id);
    msg.control.println();
    printSaveReminder();
    return 0;
}

FLASHMEM int cmd_output(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: OUTPUT requires a subcommand"));
        printUsage();
        return 1;
    }

    // RUN-allowed reads
    if (streq_P(argv[1], PSTR("LIST"))) {
#if SUPPORTS_JSON_EXPORT
        if (argc >= 3 && streq_P(argv[2], PSTR("JSON"))) {
            msg.control.println();
            writeOutputsJson(msg.control);
            msg.control.println();
            return 0;
        }
#endif
        listOutputTable();
        return 0;
    }
    const uint8_t id = parseOutputIdToken(argv[1]);
    if (id != OUT_ID_NONE && argc == 2) {
        if (!printOutputDetail(id)) { printNoSuchOutput(argv[1]); return 1; }
        return 0;
    }

    if (id != OUT_ID_NONE && !outputExists(id)) { printNoSuchOutput(argv[1]); return 1; }

    // Everything else changes the configuration
    if (isInRunMode()) {
        msg.control.print(F("ERROR: OUTPUT "));
        msg.control.print(argv[1]);
        msg.control.println(F(" is config-only; enter CONFIG mode first"));
        return 1;
    }
    configGenerationBump();   // past the CONFIG gate (#455)

    if (streq_P(argv[1], PSTR("ADD"))) return outputAdd(argc, argv);
    if (streq_P(argv[1], PSTR("DEL"))) return outputDel(argc, argv);

    if (id == OUT_ID_NONE || !outputExists(id)) {
        printNoSuchOutput(argv[1]);
        return 1;
    }
    const OutputRecord* r = outputRecord(id);
    const OutputFormat fmt = (OutputFormat)r->format;
    const char* verb = argv[2];

    if (streq_P(verb, PSTR("ENABLE")) || streq_P(verb, PSTR("DISABLE"))) {
        const bool on = streq_P(verb, PSTR("ENABLE"));
        if (on && !mayRun(id, fmt, r->transport)) return 1;
        outputSetEnabledById(id, on);
        msg.control.print(F("Output "));
        msg.control.print(id);
        msg.control.print(F(" ("));
        printOutputLabel(id);
        msg.control.print(on ? F(") enabled") : F(") disabled"));
        msg.control.println();
        printSaveReminder();
        return 0;
    }

    if (streq_P(verb, PSTR("TRANSPORT"))) return outputTransport(id, argc, argv);

    if (streq_P(verb, PSTR("INTERVAL"))) {
        if (!outputFormatPaced(fmt)) { printNoInterval(fmt); return 1; }
        if (argc < 4) {
            msg.control.println(F("ERROR: INTERVAL requires a time in ms"));
            return 1;
        }
        unsigned long ms;
        if (!parseInterval(fmt, r->transport, argv[3], &ms)) return 1;
        outputSetIntervalById(id, (long)ms);
        msg.control.print(F("Output "));
        msg.control.print(id);
        msg.control.print(F(" interval set to "));
        msg.control.print(ms);
        msg.control.println(F(" ms"));
        printSaveReminder();
        return 0;
    }

#if INCLUDE_SD_LOGGING
    if (streq_P(verb, PSTR("CSV"))) {
        if (!(fmt == FMT_CSV && r->transport == OT_SD)) {
            msg.control.println(F("ERROR: CSV LONG|WIDE applies to the CSV output on SD"));
            return 1;
        }
        SdLogFormat f;
        if (argc < 4 || !parseSdLogFormat(argv[3], f)) {
            msg.control.println(F("ERROR: CSV requires LONG or WIDE"));
            msg.control.println(F("  WIDE: one row per sample, a column per input"));
            msg.control.println(F("  LONG: one row per reading (Time,Sensor,Value,Units)"));
            return 1;
        }
        outputSetOptionById(id, (uint8_t)f);
        msg.control.print(F("Output "));
        msg.control.print(id);
        msg.control.print(F(" CSV layout set to "));
        msg.control.println(sdLogFormatName((uint8_t)f));
        printSaveReminder();
        return 0;
    }
#endif

    msg.control.print(F("ERROR: Unknown OUTPUT subcommand '"));
    msg.control.print(verb);
    msg.control.println(F("'"));
    printUsage();
    return 1;
}
