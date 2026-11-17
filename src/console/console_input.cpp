/*
 * console_input.cpp - see console_input.h
 */
#include "../config.h"

#include "console_input.h"
#include "console_routing.h"
#include "line_reader.h"
#include "cli_tokenize.h"
#include "command_table.h"                // dispatchCommand
#include "json_import_stream.h"
#include "../transports/transport_registry.h"
#include "../core/message_api.h"
#include <ctype.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static const char PROMPT[] = "preOBD> ";

// One line per source. Sources are the primary, the secondary and the framed
// ports, and only Serial1 can be framed (main.cpp, setFramedEnabled), so three
// slots cover every source at once; a board without the framed link has two.
// A slot is taken on a source's first byte and given back at the end of the
// pump in which that transport stopped being a source, so they can't run out.
#if INCLUDE_FRAMED_TRANSPORT
static const uint8_t kLineSlots = 3;
#else
static const uint8_t kLineSlots = 2;
#endif

struct LineSlot {
    uint8_t    owner;                      // TransportID, TRANSPORT_NONE = free
    LineReader reader;
    char       buf[CLI_CMD_BUFFER_SIZE];
};
static LineSlot s_slots[kLineSlots];
static uint8_t  s_activeId = TRANSPORT_NONE;

struct Source {
    TransportInterface* t;
    uint8_t             id;                // TransportID
    bool                needConnected;     // the console's own transports must be connected
};

// The sources in reading order: primary, secondary, then every framed port.
// One transport is listed once, whatever roles it has.
FLASHMEM static uint8_t listSources(Source out[], uint8_t max) {
    uint8_t n = 0;
    auto add = [&](uint8_t id, TransportInterface* t, bool needConnected) {
        if (t == nullptr || n == max) return;
        for (uint8_t i = 0; i < n; i++) if (out[i].id == id) return;
        out[n++] = {t, id, needConnected};
    };
    add(consoleOnUsbFallback() ? (uint8_t)TRANSPORT_USB_SERIAL : (uint8_t)planeTransportId(PLANE_CONTROL, false),
        consoleTransport(false), true);
    add(planeTransportId(PLANE_CONTROL, true), consoleTransport(true), true);
    for (uint8_t i = 0; i < NUM_TRANSPORTS; i++) {
        TransportInterface* t = transportGet(i);
        if (t != nullptr && (t->getCapabilities() & CAP_FRAMED)) add(i, t, false);
    }
    return n;
}

// At most the primary, the secondary and Serial1 framed: kLineSlots. A
// second framed transport would be left off the list (listSources stops at
// `max`) and need a slot added above. The lists sit on the stack, small on the Mega.
static const uint8_t kMaxSources = kLineSlots;

FLASHMEM bool consoleIsSource(uint8_t id) {
    Source src[kMaxSources];
    const uint8_t n = listSources(src, kMaxSources);
    for (uint8_t i = 0; i < n; i++) if (src[i].id == id) return true;
    return false;
}

FLASHMEM uint8_t consoleActiveSourceId() { return s_activeId; }

// This source's slot, taking a free one on its first byte; nullptr if none
// is free (can't happen with kLineSlots above, so the byte is just dropped)
FLASHMEM static LineSlot* slotFor(uint8_t id) {
    LineSlot* free = nullptr;
    for (uint8_t i = 0; i < kLineSlots; i++) {
        if (s_slots[i].owner == id) return &s_slots[i];
        if (s_slots[i].owner == TRANSPORT_NONE && free == nullptr) free = &s_slots[i];
    }
    if (free != nullptr) free->owner = id;
    return free;
}

// A transport that is no longer a source gives its slot back; its half-typed
// line is dropped, never finished by whoever takes the slot next
FLASHMEM static void releaseSlotsOfNonSources() {
    for (uint8_t i = 0; i < kLineSlots; i++) {
        if (s_slots[i].owner == TRANSPORT_NONE || consoleIsSource(s_slots[i].owner)) continue;
        lineReaderReset(s_slots[i].reader);
        s_slots[i].owner = TRANSPORT_NONE;
    }
}

FLASHMEM static void echo(char c) { msg.control.write(c); }

// One completed line: split it into words and run it. The command name is
// upper-cased in place; its arguments keep their case (names, labels).
FLASHMEM static void runLine(char* line) {
    char* argv[CLI_MAX_ARGS];
    const uint8_t argc = cliTokenize(line, argv, CLI_MAX_ARGS);
    if (argc == 0) return;                          // a blank line: just the prompt
    for (char* p = argv[0]; *p; p++) *p = (char)toupper((unsigned char)*p);
    dispatchCommand(argc, argv);
}

// One byte from source `id`. Returns true when it ended something (a command
// ran, or an import finished), after which the configuration may have changed.
FLASHMEM static bool feedByte(uint8_t id, char c) {
#if SUPPORTS_JSON_IMPORT_STREAM
    if (jsonImportActive() && jsonImportSource() == id) {
        jsonImportFeed(c);
        if (jsonImportActive()) return false;
        msg.control.print(F("\r\n"));               // the import's result, then a prompt
        msg.control.print(PROMPT);
        return true;
    }
#endif
    LineSlot* slot = slotFor(id);
    if (slot == nullptr) return false;
    if (!lineReaderFeed(slot->reader, c, echo)) return false;
    runLine(slot->buf);
    lineReaderClear(slot->reader);
    msg.control.print(PROMPT);
    return true;
}

// Read one source until it is empty, or until a command it ran stopped it
// being a source (BUS SERIAL <n> DISABLE from that port: a port that was
// turned off must not be read again, #558)
FLASHMEM static void pumpSource(const Source& s) {
    if (s.needConnected && !s.t->isConnected()) return;
    s_activeId = s.id;
    consoleSetActive(s.t);
    while (s.t->available()) {
        const char c = (char)s.t->read();
        if (feedByte(s.id, c) && (!consoleIsSource(s.id) || transportGet(s.id) != s.t)) break;
    }
}

#if SUPPORTS_JSON_IMPORT_STREAM
FLASHMEM static const char* transportName(uint8_t id) {
    TransportInterface* t = transportRegistered(id);
    return t != nullptr ? t->getName() : "?";
}

// An import whose source went away (a port turned off, CONSOLE TRANSPORT
// moved) is cancelled at once, and the console is told, since the importer
// can't be. One whose sender went quiet ends after 45 s, on its own link: a
// BLE phone that walked away looks just like this, because the Teensy can't
// see a BLE central disconnect.
FLASHMEM static void checkImport() {
    if (!jsonImportActive()) return;
    const uint8_t id = jsonImportSource();
    if (!consoleIsSource(id)) {
        jsonImportCancel();
        consoleSetActive(nullptr);
        msg.control.print(F("ERROR: JSON import cancelled: "));
        msg.control.print(transportName(id));
        msg.control.println(F(" is no longer a console link"));
        msg.control.print(PROMPT);
        return;
    }
    if (jsonImportIdleExpired()) {
        jsonImportCancel();
        s_activeId = id;
        consoleSetActive(transportGet(id));
        msg.control.println(F("ERROR: JSON import timed out (no data received)"));
        msg.control.print(PROMPT);
    }
}
#endif

FLASHMEM void consolePump() {
    // Each source once, in order. The list is made again after each one,
    // because a command may have changed it (CONSOLE TRANSPORT ... SECONDARY,
    // BUS SERIAL ... DISABLE, or USB standing in for a primary turned off);
    // the next source is the first one not read yet, so none is skipped
    uint16_t read = 0;                              // bit per TransportID
    static_assert(NUM_TRANSPORTS <= 16, "one bit per transport in `read`");
    Source src[kMaxSources];
    for (;;) {
        const uint8_t n = listSources(src, kMaxSources);
        uint8_t k = 0;
        while (k < n && (read & (1u << src[k].id))) k++;
        if (k == n) break;
        read |= (uint16_t)(1u << src[k].id);
        pumpSource(src[k]);
    }
    releaseSlotsOfNonSources();
#if SUPPORTS_JSON_IMPORT_STREAM
    checkImport();
#endif
    // Unsolicited output (alarms, notes) goes to primary + secondary again
    s_activeId = TRANSPORT_NONE;
    consoleSetActive(nullptr);
}

FLASHMEM void consoleInputInit() {
    for (uint8_t i = 0; i < kLineSlots; i++) {
        s_slots[i].owner = TRANSPORT_NONE;
        lineReaderInit(s_slots[i].reader, s_slots[i].buf, sizeof(s_slots[i].buf));
    }
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  preOBD Serial Configuration"));
    msg.control.println(F("  Type 'HELP' for commands"));
    msg.control.println(F("========================================"));
    msg.control.println();
    msg.control.print(PROMPT);
}
