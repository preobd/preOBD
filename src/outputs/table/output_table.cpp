/*
 * output_table.cpp - The stored output table (#612 phase 2b-1).
 * See output_table.h. Dependency-light (system_config.h, board sizing): the
 * native tests build it on its own.
 */

#include "output_table.h"
#include "../../hal/bus_defaults.h"          // NUM_SERIAL_PORTS, NUM_CAN_BUSES
#include "../../buses/pinctrl/bus_wiring.h"  // BUS_WIRED_CAN_ID
#include "../formats/output_format.h"        // SDLOG_FORMAT_WIDE
#include <string.h>
#include <stdio.h>
#ifndef __AVR__
#include <strings.h>   // strcasecmp (AVR has it in string.h)
#endif

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

OutputRecord outputRecords[MAX_OUTPUTS];

// ---- the table ----

OutputRecord* outputRecord(uint8_t id) {
    if (id >= MAX_OUTPUTS || outputRecords[id].format == FMT_NONE) return nullptr;
    return &outputRecords[id];
}

bool outputExists(uint8_t id) { return outputRecord(id) != nullptr; }

bool outputIsEnabled(uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    return r && r->enabled;
}

OutputFormat outputFormatOf(uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    return r ? (OutputFormat)r->format : FMT_NONE;
}

uint8_t outputTransportOf(uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    return r ? r->transport : (uint8_t)OT_NONE;
}

uint8_t outputCount() {
    uint8_t n = 0;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++)
        if (outputExists(id)) n++;
    return n;
}

RouteMask outputsExistingMask() {
    RouteMask m = 0;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++)
        if (outputExists(id)) m |= (RouteMask)(1u << id);
    return m;
}

uint8_t outputFind(OutputFormat format, OutputTransportKind kind) {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const OutputRecord* r = outputRecord(id);
        if (r && r->format == format && outputTransportKind(r->transport) == kind) return id;
    }
    return OUT_ID_NONE;
}

RouteMask outputCanSendersOnBus(uint8_t bus) {
    RouteMask m = 0;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        const OutputRecord* r = outputRecord(id);
        if (r && r->enabled && outputFormatSendsCan(r->format) &&
            outputTransportCanBus(r->transport) == bus) m |= (RouteMask)(1u << id);
    }
    return m;
}

uint8_t outputFreeId() {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++)
        if (outputRecords[id].format == FMT_NONE) return id;
    return OUT_ID_NONE;
}

// ---- changes ----

FLASHMEM uint8_t outputCreate(OutputFormat format, uint8_t transport, bool enabled) {
    const uint8_t id = outputFreeId();
    if (id == OUT_ID_NONE) return OUT_ID_NONE;
    OutputRecord& r = outputRecords[id];
    r.id = id;
    r.format = format;
    r.transport = transport;
    r.enabled = enabled ? 1 : 0;
    r.option = (format == FMT_CSV && transport == OT_SD) ? (uint8_t)SDLOG_FORMAT_WIDE : 0;
    r.spare = 0;
    r.interval = outputIntervalDefault(format, transport);
    return id;
}

void outputErase(uint8_t id) {
    if (id < MAX_OUTPUTS) memset(&outputRecords[id], 0, sizeof(OutputRecord));
}

void outputsClear() {
    memset(outputRecords, 0, sizeof(outputRecords));
}

FLASHMEM void outputsSeedDefaults() {
    outputsClear();
#if INCLUDE_CAN && BUS_WIRED_CAN_ID != 0xFF
    // Off: enabling it attaches to the bus, which then ACKs on a car's network
    outputCreate(FMT_OBD, (uint8_t)(OT_CAN1 + BUS_WIRED_CAN_ID), false);
#endif
#if INCLUDE_ELM327 && INCLUDE_FRAMED_TRANSPORT
    outputCreate(FMT_ELM327, OT_BLE, true);
#endif
}

FLASHMEM bool outputRecordValid(const OutputRecord& r, uint8_t slot) {
    if (r.format == FMT_NONE) return true;   // a free slot
    if (r.id != slot || r.format >= NUM_FORMATS) return false;
    if (!outputFormatBuilt((OutputFormat)r.format)) return false;
    if (!outputTransportAllowed((OutputFormat)r.format, r.transport)) return false;
    if (r.enabled > 1) return false;
    if (outputFormatPaced((OutputFormat)r.format)) {
        if (!outputIntervalValid((OutputFormat)r.format, r.transport, r.interval)) return false;
    } else if (r.interval != 0) {
        return false;
    }
    if (r.format == FMT_CSV && r.transport == OT_SD) {
        if (r.option > SDLOG_FORMAT_LONG) return false;
    } else if (r.option != 0) {
        return false;
    }
    return true;
}

// ---- formats and transports ----

bool outputFormatBuilt(OutputFormat format) {
    switch (format) {
#if INCLUDE_SERIAL_OUTPUT || INCLUDE_SD_LOGGING
    case FMT_CSV:      return true;
#endif
#if INCLUDE_REALDASH
    case FMT_REALDASH: return true;
#endif
#if INCLUDE_CAN
    case FMT_OBD:      return true;
#endif
#if INCLUDE_ELM327
    case FMT_ELM327:   return true;
#endif
#if INCLUDE_CAN_LAYOUTS
    case FMT_PREOBD:   return true;
    case FMT_HALTECH:  return true;
#endif
    default:           return false;
    }
}

OutputTransportKind outputTransportKind(uint8_t t) {
    if (t == OT_USB || t == OT_ESP32_BT || (t >= OT_SERIAL1 && t < OT_SERIAL1 + 8)) return OTK_STREAM;
    if (t >= OT_CAN1 && t < OT_CAN1 + 3) return OTK_CAN;
    if (t == OT_SD) return OTK_SD;
    if (t == OT_BLE) return OTK_BLE;
    return OTK_NONE;
}

uint8_t outputTransportSerialPort(uint8_t t) {
    return (t >= OT_SERIAL1 && t < OT_SERIAL1 + 8) ? (uint8_t)(t - OT_SERIAL1 + 1) : 0xFF;
}

uint8_t outputTransportCanBus(uint8_t t) {
    return (t >= OT_CAN1 && t < OT_CAN1 + 3) ? (uint8_t)(t - OT_CAN1) : 0xFF;
}

// Does this board have the transport at all?
static bool transportOnBoard(uint8_t t) {
    const uint8_t port = outputTransportSerialPort(t);
    if (port != 0xFF) return port <= NUM_SERIAL_PORTS;
    const uint8_t bus = outputTransportCanBus(t);
    if (bus != 0xFF) return bus < NUM_CAN_BUSES;
    switch (t) {
    case OT_USB: return true;
#if defined(ESP32)
    case OT_ESP32_BT: return true;
#endif
#if INCLUDE_SD_LOGGING
    case OT_SD: return true;
#endif
#if INCLUDE_FRAMED_TRANSPORT
    case OT_BLE: return true;
#endif
    default: return false;
    }
}

bool outputTransportAllowed(OutputFormat format, uint8_t t) {
    if (!transportOnBoard(t)) return false;
    const OutputTransportKind k = outputTransportKind(t);
    switch (format) {
    case FMT_CSV:
#if !INCLUDE_SERIAL_OUTPUT
        if (k == OTK_STREAM) return false;
#endif
#if !INCLUDE_SD_LOGGING
        if (k == OTK_SD) return false;
#endif
        return k == OTK_STREAM || k == OTK_SD;
    // RealDash on a stream, or on BLE: the nRF's data channel (FFE1, #612 2c)
    case FMT_REALDASH: return k == OTK_STREAM || k == OTK_BLE;
    case FMT_OBD:      return k == OTK_CAN;
    // ELM327 runs on a serial port or BLE (USB is the console's; ESP32 BT has
    // no ELM327 binding)
    case FMT_ELM327:   return (k == OTK_STREAM && outputTransportSerialPort(t) != 0xFF) ||
                              k == OTK_BLE;   // OT_BLE is on the board only with the framed link
#if INCLUDE_CAN_LAYOUTS
    case FMT_PREOBD:
    case FMT_HALTECH:  return k == OTK_CAN;
#endif
    default:           return false;
    }
}

bool outputFormatPaced(OutputFormat format) {
#if INCLUDE_CAN_LAYOUTS
    if (outputFormatIsLayout(format)) return true;   // pushed every interval
#endif
    return format == FMT_CSV || format == FMT_REALDASH;
}

// Floors (#608), set by what a 115200-baud link (UART or the BLE bridge)
// carries, ~11.5 KB/s: a CSV line per input stalls the loop below ~100 ms;
// RealDash's 16-byte frames for 20 inputs are 6.4 KB/s at 50 ms. The SD log
// buffers its rows, and 1 ms rows are the #537 watchdog soak load
// (tools/watchdog_bench.py), so 1 ms only costs card space. RealDash on BLE
// shares the 115200-baud nRF link with the app's status, live values and
// alarms, so its floor is 100 ms (#612 2c).
uint16_t outputIntervalMin(OutputFormat format, uint8_t transport) {
#if INCLUDE_CAN_LAYOUTS
    if (format == FMT_PREOBD) return 20;   // 9 frames (34 signals): ~40% of a 125 kbit/s bus
    if (format == FMT_HALTECH) return 20;  // 11 frames: ~6% of its 1 Mbit/s bus (Haltech's fastest is 50 Hz)
#endif
    if (format == FMT_REALDASH) return transport == OT_BLE ? 100 : 50;
    if (format == FMT_CSV) return transport == OT_SD ? 1 : 100;
    return 0;
}

uint16_t outputIntervalDefault(OutputFormat format, uint8_t transport) {
#if INCLUDE_CAN_LAYOUTS
    if (format == FMT_PREOBD) return 100;
    if (format == FMT_HALTECH) return 20;  // 50 Hz, Haltech's rate for RPM and MAP (decision 1)
#endif
    if (format == FMT_REALDASH) return REALDASH_INTERVAL_MS;
    if (format == FMT_CSV) return transport == OT_SD ? SD_LOG_INTERVAL_MS : SERIAL_CSV_INTERVAL_MS;
    return 0;
}

bool outputIntervalValid(OutputFormat format, uint8_t transport, long ms) {
    return outputFormatPaced(format) && ms >= outputIntervalMin(format, transport) &&
           ms <= OUTPUT_INTERVAL_MAX_MS;
}

static_assert(REALDASH_INTERVAL_MS >= 50 && REALDASH_INTERVAL_MS <= 60000, "REALDASH_INTERVAL_MS outside RealDash's range (#608)");
static_assert(SERIAL_CSV_INTERVAL_MS >= 100 && SERIAL_CSV_INTERVAL_MS <= 60000, "SERIAL_CSV_INTERVAL_MS outside CSV's range (#608)");
static_assert(SD_LOG_INTERVAL_MS >= 1 && SD_LOG_INTERVAL_MS <= 60000, "SD_LOG_INTERVAL_MS outside the SD log's range (#608)");

// ---- names ----

const char* outputFormatName(OutputFormat format) {
    switch (format) {
    case FMT_CSV:      return "CSV";
    case FMT_REALDASH: return "REALDASH";
    case FMT_OBD:      return "OBD";
    case FMT_ELM327:   return "ELM327";
#if INCLUDE_CAN_LAYOUTS
    case FMT_PREOBD:   return "PREOBD";
    case FMT_HALTECH:  return "HALTECH";
#endif
    default:           return "NONE";
    }
}

OutputFormat parseOutputFormat(const char* s) {
    if (s == nullptr) return FMT_NONE;
    if (strcasecmp(s, "CSV") == 0)      return FMT_CSV;
    if (strcasecmp(s, "REALDASH") == 0) return FMT_REALDASH;
    if (strcasecmp(s, "OBD") == 0)      return FMT_OBD;
    if (strcasecmp(s, "ELM327") == 0)   return FMT_ELM327;
#if INCLUDE_CAN_LAYOUTS
    if (strcasecmp(s, "PREOBD") == 0)   return FMT_PREOBD;
    if (strcasecmp(s, "HALTECH") == 0)  return FMT_HALTECH;
#endif
    return FMT_NONE;
}

size_t outputTransportName(uint8_t t, char* buf, size_t n) {
    if (n == 0) return 0;
    const uint8_t port = outputTransportSerialPort(t);
    const uint8_t bus = outputTransportCanBus(t);
    if (port != 0xFF)          snprintf(buf, n, "SERIAL%u", (unsigned)port);
    else if (bus != 0xFF)      snprintf(buf, n, "CAN%u", (unsigned)(bus + 1));
    else if (t == OT_USB)      snprintf(buf, n, "USB");
    else if (t == OT_ESP32_BT) snprintf(buf, n, "ESP32_BT");
    else if (t == OT_SD)       snprintf(buf, n, "SD");
    else if (t == OT_BLE)      snprintf(buf, n, "BLE");
    else                       snprintf(buf, n, "NONE");
    return strlen(buf);
}

bool parseOutputTransport(const char* s, uint8_t* out) {
    if (s == nullptr) return false;
    uint8_t t = 0xFF;
    if (strcasecmp(s, "USB") == 0)           t = OT_USB;
    else if (strcasecmp(s, "ESP32_BT") == 0) t = OT_ESP32_BT;
    else if (strcasecmp(s, "SD") == 0)       t = OT_SD;
    else if (strcasecmp(s, "BLE") == 0)      t = OT_BLE;
    else if (strcasecmp(s, "NONE") == 0)     t = OT_NONE;
    else if (strncasecmp(s, "SERIAL", 6) == 0 && s[6] >= '1' && s[6] <= '8' && s[7] == '\0')
        t = (uint8_t)(OT_SERIAL1 + (s[6] - '1'));
    else if (strncasecmp(s, "CAN", 3) == 0 && s[3] >= '1' && s[3] <= '3' && s[4] == '\0')
        t = (uint8_t)(OT_CAN1 + (s[3] - '1'));
    if (t == 0xFF) return false;
    *out = t;
    return true;
}
