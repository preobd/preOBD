/*
 * cmd_output.cpp - OUTPUT: the outputs you add (#612 phase 1, ARCHITECTURE.md §14.5)
 *
 *   OUTPUT LIST [JSON]                         RUN ok
 *   OUTPUT <id>                                RUN ok
 *   OUTPUT ADD <format> <transport> [INTERVAL <ms>] [LONG|WIDE]
 *   OUTPUT <id> ENABLE | DISABLE
 *   OUTPUT <id> INTERVAL <ms>
 *   OUTPUT <id> CSV LONG | WIDE                (the CSV output on SD)
 *   OUTPUT DEL <id>
 *
 * Phase 1 has one output per format (output_table.h), on the transport that
 * format uses today; OUTPUT ADD refuses any other and says which command
 * moves it. The old OUTPUT <name> forms are gone, with no aliases.
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

// Phase 1: the transport must be the one the format already uses. Prints the
// reason and the command that changes it when it isn't.
static FLASHMEM bool transportAccepted(uint8_t id, const char* tok) {
    char current[24];
    outputTransportText(id, current, sizeof(current));
    switch (outputKindOf(id)) {
    case OTK_SD:
        if (strcasecmp(tok, "SD") == 0) return true;
        break;
    case OTK_CAN:
        if (strcmp(current, "NONE") == 0) {
            msg.control.println(F("ERROR: No CAN output bus is set"));
            msg.control.println(F("  Run: BUS CAN OUTPUT <CAN1|CAN2|CAN3> ENABLE first"));
            return false;
        }
        if (strcasecmp(tok, current) == 0) return true;
        if (strncasecmp(tok, "CAN", 3) != 0 || tok[3] < '1' || tok[3] > '3' || tok[4] != '\0') break;
        msg.control.print(F("ERROR: OBD answers on the CAN output bus, which is "));
        msg.control.println(current);
        msg.control.print(F("  BUS CAN OUTPUT "));
        msg.control.print(tok);
        msg.control.println(F(" ENABLE moves it"));
        return false;
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
        if (strncasecmp(tok, "SERIAL", 6) == 0 && isdigit((unsigned char)tok[6]) && tok[7] == '\0' &&
            tok[6] >= '1' && tok[6] <= '8') {
            const uint8_t port = (uint8_t)(tok[6] - '0');
            if (!elm327PortUsable(port)) return false;
            systemConfig.buses.elm327_serial_port = port;   // the port binding today's BUS SERIAL <n> ELM327 sets
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
    msg.control.println(outputKindOf(id) == OTK_SD ? "SD"
                        : outputKindOf(id) == OTK_SERIAL ? "SERIAL1-SERIAL8" : current);
    return false;
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
    const OutputTransportKind kind = (fmt == FMT_CSV && strcasecmp(argv[3], "SD") == 0) ? OTK_SD : OTK_STREAM;
    const uint8_t id = outputIdFor(fmt, kind);
    if (id == OUT_ID_NONE || !outputIdBuilt(id)) {
        msg.control.print(F("ERROR: "));
        msg.control.print(outputFormatName(fmt));
        if (fmt == FMT_CSV) msg.control.print(kind == OTK_SD ? F(" on SD") : F(" on a stream"));
        msg.control.println(F(" is not in this build"));
        return 1;
    }
    if (outputExists(id)) {
        msg.control.print(F("ERROR: Output "));
        msg.control.print(id);
        msg.control.print(F(" is already "));
        printOutputLabel(id);
        msg.control.println();
        msg.control.println(F("  This firmware has one output per format; OUTPUT DEL it first"));
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
    outputAddById(id);
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
#if INCLUDE_ELM327
    // The port goes too, as BUS SERIAL <n> ELM327 DISABLE does
    if (id == OUT_ID_ELM327) systemConfig.buses.elm327_serial_port = 0xFF;
#endif
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
