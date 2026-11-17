/*
 * cmd_output.cpp - OUTPUT: the outputs you add (#612, ARCHITECTURE.md §14.5)
 *
 *   OUTPUT LIST [JSON]                         RUN ok
 *   OUTPUT <id>                                RUN ok
 *   OUTPUT ADD <format> <transport> [INTERVAL <ms>] [LONG|WIDE]
 *   OUTPUT <id> ENABLE | DISABLE
 *   OUTPUT <id> INTERVAL <ms>
 *   OUTPUT <id> CSV LONG | WIDE                (the CSV output on SD)
 *   OUTPUT DEL <id>
 *
 * The ids are fixed (output_table.h). OUTPUT ADD binds OBD to a CAN bus and
 * ELM327 to a serial port or BLE at once; RealDash and CSV use the data
 * stream (TRANSPORT DATA moves it) until phase 2b gives them their own.
 */

#include "../config.h"
#include "command_handlers.h"
#include "command_helpers.h"
#include "../core/system_mode.h"
#include "../core/config_generation.h"
#include "../core/message_api.h"
#include "../core/message_router.h"
#include "../config/system_config.h"
#include "../outputs/table/output_base.h"
#include "../outputs/table/output_table.h"
#include "../outputs/formats/output_format.h"
#include "../buses/bus_config.h"
#include "../buses/serial_manager.h"
#if INCLUDE_CAN
#include "../hal/bus_defaults.h"   // NUM_CAN_BUSES
#endif
#if SUPPORTS_JSON_EXPORT
#include "../config/json_registry.h"
#endif
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// Every format name; OUTPUT ADD says which ones a build leaves out
#define OUTPUT_FORMATS_TEXT "CSV, REALDASH, OBD, ELM327"

static FLASHMEM void printUsage() {
    msg.control.println(F("  Usage: OUTPUT LIST | <id> | ADD <format> <transport> [INTERVAL <ms>] | <id> ENABLE|DISABLE|INTERVAL <ms>"
#if INCLUDE_SD_LOGGING
                          "|CSV LONG|WIDE"
#endif
                          " | DEL <id>"));
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

// Digits only, in range for this output; prints the range on failure.
static FLASHMEM bool parseInterval(uint8_t id, const char* arg, unsigned long* out) {
    char* end = nullptr;
    const unsigned long v = isdigit((unsigned char)arg[0]) ? strtoul(arg, &end, 10) : 0;
    if (end == nullptr || *end != '\0' || v < outputIntervalMinId(id) || v > OUTPUT_INTERVAL_MAX_MS) {
        msg.control.print(F("ERROR: Interval must be "));
        msg.control.print(outputIntervalMinId(id));
        msg.control.print(F("-"));
        msg.control.print(OUTPUT_INTERVAL_MAX_MS);
        msg.control.println(F(" ms"));
        return false;
    }
    *out = v;
    return true;
}

static FLASHMEM void printNoInterval(uint8_t id) {
    msg.control.print(F("ERROR: "));
    msg.control.print(outputFormatName(outputFormatOf(id)));
    msg.control.println(F(" answers requests as they arrive; it has no interval"));
}

#if INCLUDE_ELM327
// Can ELM327 take Serial<port_id>? Prints why not.
static FLASHMEM bool elm327PortUsable(uint8_t port_id) {
    if (!isSerialPortActive(port_id)) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port_id);
        msg.control.print(F(" is not enabled. Run: BUS SERIAL "));
        msg.control.print(port_id);
        msg.control.println(F(" ENABLE <baud> first"));
        return false;
    }
#if INCLUDE_FRAMED_TRANSPORT
    if (systemConfig.serial.framed_enabled_mask & (1u << (port_id - 1))) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port_id);
        msg.control.println(F(" is the framed nRF link; for ELM327 over it use OUTPUT ADD ELM327 BLE"));
        return false;
    }
#endif
    // The message router reads and writes its own ports
    const uint8_t t = TRANSPORT_SERIAL1 + (port_id - 1);
    const auto& r = systemConfig.router;
    if (r.control_primary == t || r.control_secondary == t || r.data_primary == t ||
        r.data_secondary == t || r.debug_primary == t || r.debug_secondary == t) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port_id);
        msg.control.println(F(" carries the console, data or debug stream (TRANSPORT); move it first"));
        return false;
    }
    return true;
}
#endif

// The transport must suit the format. OBD and ELM327 bind to the one named;
// RealDash and CSV must name the data stream they share. Prints the reason
// and the command that changes it when it doesn't suit.
static FLASHMEM bool transportAccepted(uint8_t id, const char* tok) {
    char current[24];
    outputTransportText(id, current, sizeof(current));
    switch (outputKindOf(id)) {
    case OTK_SD:
        if (strcasecmp(tok, "SD") == 0) return true;
        break;
    case OTK_BLE:
        if (strcasecmp(tok, "BLE") == 0) return true;
        break;
    case OTK_CAN: {
#if INCLUDE_CAN
        if (strncasecmp(tok, "CAN", 3) != 0 || tok[3] < '1' || tok[3] > '3' || tok[4] != '\0') break;
        const uint8_t bus = (uint8_t)(tok[3] - '1');
        if (bus >= NUM_CAN_BUSES) {
            msg.control.print(F("ERROR: "));
            msg.control.print(tok);
            msg.control.println(F(" not available on this platform"));
            return false;
        }
        const uint8_t mode = systemConfig.buses.can[bus].mode;
        if (mode != CAN_BUS_NORMAL) {
            msg.control.print(F("ERROR: "));
            msg.control.print(tok);
            msg.control.println(mode == CAN_BUS_OFF ? F(" is off") : F(" is LISTEN, and OBD transmits"));
            msg.control.print(F("  Run: BUS CAN "));
            msg.control.print(tok);
            msg.control.println(F(" ENABLE first"));
            return false;
        }
        systemConfig.buses.obdBus = bus;
        return true;
#else
        break;
#endif
    }
    case OTK_STREAM: {
        bool ok = false;
        const TransportID t = parseTransport(tok, &ok);
        if (ok && t != TRANSPORT_NONE && (uint8_t)t == systemConfig.router.data_primary) return true;
        if (!ok || t == TRANSPORT_NONE) break;
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(outputFormatOf(id)));
        msg.control.print(F(" goes to the data stream, which is on "));
        msg.control.println(current);
        msg.control.print(F("  TRANSPORT DATA "));
        msg.control.print(tok);
        msg.control.println(F(" moves it (for every output on the stream)"));
        return false;
    }
    case OTK_SERIAL:
#if INCLUDE_ELM327
        if (strncasecmp(tok, "SERIAL", 6) == 0 && tok[6] >= '1' && tok[6] <= '8' && tok[7] == '\0') {
            const uint8_t port = (uint8_t)(tok[6] - '0');
            if (!elm327PortUsable(port)) return false;
            systemConfig.buses.elm327_serial_port = port;
            return true;
        }
#endif
        break;
    }
    msg.control.print(F("ERROR: "));
    msg.control.print(outputFormatName(outputFormatOf(id)));
    msg.control.print(F(" can't use "));
    msg.control.println(tok);
    msg.control.print(F("  It uses "));
    switch (outputKindOf(id)) {
    case OTK_SD:     msg.control.println(F("SD")); break;
    case OTK_SERIAL: msg.control.println(F("SERIAL1-SERIAL8")); break;
    case OTK_CAN:    msg.control.println(F("CAN1-CAN3")); break;
    case OTK_BLE:    msg.control.println(F("BLE")); break;
    default:         msg.control.println(current); break;
    }
    return false;
}

// OBD and ELM327 on a serial port exist from the start with no transport
// (NONE) until OUTPUT ADD binds one; ADD on such an output binds it
static bool outputUnbound(uint8_t id) {
    if (id == OUT_ID_OBD)    return systemConfig.buses.obdBus == 0xFF;
    if (id == OUT_ID_ELM327) return systemConfig.buses.elm327_serial_port == 0xFF;
    return false;
}

// OUTPUT <id> ENABLE: an OBD or ELM327 output must have a transport it can
// run on now, or the switch would read "enabled" over an output that answers
// nothing. Prints why not.
static FLASHMEM bool transportReady(uint8_t id) {
    if (outputUnbound(id)) {
        msg.control.print(F("ERROR: Output "));
        msg.control.print(id);
        msg.control.print(F(" has no transport: OUTPUT ADD "));
        msg.control.println(id == OUT_ID_OBD ? F("OBD CAN<n>") : F("ELM327 SERIAL<n>"));
        return false;
    }
#if INCLUDE_CAN
    if (id == OUT_ID_OBD) {
        const uint8_t bus = systemConfig.buses.obdBus;
        const uint8_t mode = bus < CAN_BUS_SLOTS ? systemConfig.buses.can[bus].mode : CAN_BUS_OFF;
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
    return true;
}

static FLASHMEM int outputAdd(int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: OUTPUT ADD requires a format and a transport"));
        msg.control.println(F("  Usage: OUTPUT ADD <format> <transport> [INTERVAL <ms>]"
#if INCLUDE_SD_LOGGING
                              " [LONG|WIDE]"
#endif
                              ));
        msg.control.println(F("  Formats: " OUTPUT_FORMATS_TEXT));
        return 1;
    }
    const OutputFormat fmt = parseOutputFormat(argv[2]);
    if (fmt == FMT_NONE) {
        msg.control.print(F("ERROR: Unknown format '"));
        msg.control.print(argv[2]);
        msg.control.println(F("'"));
        msg.control.println(F("  Formats: " OUTPUT_FORMATS_TEXT));
        return 1;
    }
    // Two formats have two ids each: CSV (stream / SD) and ELM327 (serial / BLE)
    const OutputTransportKind kind =
        fmt == FMT_CSV    ? (strcasecmp(argv[3], "SD") == 0 ? OTK_SD : OTK_STREAM)
      : fmt == FMT_ELM327 ? (strcasecmp(argv[3], "BLE") == 0 ? OTK_BLE : OTK_SERIAL)
      : fmt == FMT_OBD    ? OTK_CAN : OTK_STREAM;
    const uint8_t id = outputIdFor(fmt, kind);
    if (id == OUT_ID_NONE || !outputIdBuilt(id)) {
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(fmt));
        if (fmt == FMT_CSV) msg.control.print(kind == OTK_SD ? F(" on SD") : F(" on a stream"));
        if (fmt == FMT_ELM327) msg.control.print(kind == OTK_BLE ? F(" over BLE") : F(" on a serial port"));
        msg.control.println(F(" is not in this build"));
        return 1;
    }
    const bool bindExisting = outputExists(id) && outputUnbound(id);
    if (outputExists(id) && !bindExisting) {
        msg.control.print(F("ERROR: Output "));
        msg.control.print(id);
        msg.control.print(F(" is already "));
        printOutputLabel(id);
        msg.control.println();
        msg.control.println(F("  One output per format and transport kind; OUTPUT DEL it first"));
        return 1;
    }

    // Options, all checked before anything changes
    unsigned long interval = 0;
    bool haveInterval = false;
    int layout = -1;
    for (int a = 4; a < argc; a++) {
        if (strcasecmp(argv[a], "INTERVAL") == 0) {
            if (a + 1 >= argc) { msg.control.println(F("ERROR: INTERVAL requires a time in ms")); return 1; }
            if (!outputTakesIntervalId(id)) { printNoInterval(id); return 1; }
            if (!parseInterval(id, argv[a + 1], &interval)) return 1;
            haveInterval = true;
            a++;
            continue;
        }
#if INCLUDE_SD_LOGGING
        SdLogFormat f;
        if (id == OUT_ID_SDLOG && parseSdLogFormat(argv[a], f)) { layout = (int)f; continue; }
#endif
        msg.control.print(F("ERROR: Unexpected '"));
        msg.control.print(argv[a]);
        msg.control.println(F("'"));
        return 1;
    }

    if (!transportAccepted(id, argv[3])) return 1;

#if INCLUDE_SD_LOGGING
    if (layout >= 0) systemConfig.sdLogFormat = (uint8_t)layout;
#endif
    if (bindExisting) outputSetEnabledById(id, true);   // binds and starts it
    else              outputAddById(id);
    // After the add: an output that doesn't exist yet takes no interval
    if (haveInterval) outputSetIntervalById(id, (long)interval);
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
    if (id == OUT_ID_NONE || !outputExists(id)) { printNoSuchOutput(argv[2]); return 1; }
    outputDeleteById(id);
    // Its transport goes too: OUTPUT ADD binds a new one
    if (id == OUT_ID_ELM327) systemConfig.buses.elm327_serial_port = 0xFF;
    if (id == OUT_ID_OBD) systemConfig.buses.obdBus = 0xFF;
    msg.control.print(F("Output "));
    msg.control.print(id);
    msg.control.println(F(" deleted, and its routes"));
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
    const char* verb = argv[2];

    if (streq_P(verb, PSTR("ENABLE")) || streq_P(verb, PSTR("DISABLE"))) {
        const bool on = streq_P(verb, PSTR("ENABLE"));
        if (on && !transportReady(id)) return 1;
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

    if (streq_P(verb, PSTR("INTERVAL"))) {
        if (!outputTakesIntervalId(id)) { printNoInterval(id); return 1; }
        if (argc < 4) {
            msg.control.println(F("ERROR: INTERVAL requires a time in ms"));
            return 1;
        }
        unsigned long ms;
        if (!parseInterval(id, argv[3], &ms)) return 1;
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
        if (id != OUT_ID_SDLOG) {
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
        systemConfig.sdLogFormat = (uint8_t)f;
        msg.control.print(F("Output "));
        msg.control.print(id);
        msg.control.print(F(" CSV layout set to "));
        msg.control.println(sdLogFormatName(systemConfig.sdLogFormat));
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
