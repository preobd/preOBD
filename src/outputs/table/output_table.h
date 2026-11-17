/*
 * output_table.h - The outputs you add (#612, ARCHITECTURE.md §14.5)
 *
 * An output is a format on a transport, with an id. Phase 2b-1 stores them
 * as records in the config-store arena (input_store.cpp): `OUTPUT ADD` takes
 * the lowest free id, `OUTPUT DEL` frees it. The id is also the output's
 * route bit on every input (Input::routeMask), so an id means one output for
 * as long as it exists, and nothing ties an id to a format.
 *
 * Records are indexed by id: outputRecord(id). A free slot has format NONE.
 * The live state of each output (started, last send) is output_manager's.
 *
 * Built-in outputs (display, app link, alarm) are not in this table and take
 * no routes.
 */

#ifndef OUTPUT_TABLE_H
#define OUTPUT_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include "../../config/system_config.h"

#ifndef MAX_OUTPUTS
#define MAX_OUTPUTS 8
#endif

// One route bit per output id, on each input (Input::routeMask)
typedef uint8_t RouteMask;
static_assert(MAX_OUTPUTS >= 1 && MAX_OUTPUTS <= 8 * sizeof(RouteMask),
              "every output id needs a route bit");

enum OutputFormat : uint8_t {
    FMT_NONE = 0,
    FMT_CSV,
    FMT_REALDASH,
    FMT_OBD,
    FMT_ELM327,
    FMT_PREOBD,   // our own CAN frames for gauges (#612 phase 3); INCLUDE_CAN_LAYOUTS
    FMT_HALTECH,  // Haltech's ECU broadcast (#612 phase 4); INCLUDE_CAN_LAYOUTS. Stored: append only
    NUM_FORMATS
};

// Where an output runs, in one byte. Streams take the TransportID values
// (core/transport_interface.h), so a stream output's transport is its TransportID.
enum OutputTransport : uint8_t {
    OT_NONE     = 0,
    OT_USB      = 1,     // TRANSPORT_USB_SERIAL
    OT_SERIAL1  = 2,     // TRANSPORT_SERIAL1 .. OT_SERIAL1 + 7 = TRANSPORT_SERIAL8
    OT_ESP32_BT = 10,    // TRANSPORT_ESP32_BT
    OT_CAN1     = 0x20,  // .. OT_CAN1 + 2 = CAN3
    OT_SD       = 0x30,
    OT_BLE      = 0x40,  // the phone-facing side of the framed nRF link
};

// The kind of transport, which decides what a format may use (§14.4)
enum OutputTransportKind : uint8_t {
    OTK_NONE,
    OTK_STREAM,    // USB, a serial port, ESP32 BT
    OTK_CAN,       // a CAN bus
    OTK_SD,        // the SD card
    OTK_BLE,       // the framed nRF link's phone channel
};

// One stored output: 8 bytes, no padding (written to EEPROM as is)
struct OutputRecord {
    uint8_t  id;          // its slot, kept for validation on load
    uint8_t  format;      // OutputFormat; FMT_NONE = free slot
    uint8_t  transport;   // OutputTransport
    uint8_t  enabled;     // 0/1
    uint8_t  option;      // CSV on SD: SdLogFormat (LONG/WIDE); else 0
    uint8_t  spare;       // reserved (phase 3: a layout)
    uint16_t interval;    // ms, push formats only; 0 otherwise
};
static_assert(sizeof(OutputRecord) == 8, "OutputRecord is stored as is: 8 bytes, no padding");

static const uint8_t OUT_ID_NONE = 0xFF;
static const uint16_t OUTPUT_INTERVAL_MAX_MS = 60000;

// ---- The table (RAM; persisted by the config-store arena) ----
extern OutputRecord outputRecords[MAX_OUTPUTS];

OutputRecord* outputRecord(uint8_t id);             // nullptr when free or out of range
bool      outputExists(uint8_t id);
bool      outputIsEnabled(uint8_t id);
OutputFormat outputFormatOf(uint8_t id);            // FMT_NONE when free
uint8_t   outputTransportOf(uint8_t id);            // OT_NONE when free
uint8_t   outputCount();
RouteMask outputsExistingMask();                    // a route bit for every output that exists
// The first output with this format on this kind of transport (the singletons:
// OBD, CSV on SD, ELM327 on a serial port, ELM327 on BLE). OUT_ID_NONE if none.
uint8_t   outputFind(OutputFormat format, OutputTransportKind kind);
uint8_t   outputFreeId();                           // lowest free id, OUT_ID_NONE when full

// ---- Changes to the records (live start/stop is output_manager's) ----
// Writes a record at the lowest free id and returns it (OUT_ID_NONE: full).
// The default interval and option for the format come with it.
uint8_t   outputCreate(OutputFormat format, uint8_t transport, bool enabled);
void      outputErase(uint8_t id);
void      outputsClear();                           // every slot free
// The defaults for a fresh or rejected config: OBD on CAN_WIRED_BUS
// (disabled: enabling it attaches and ACKs), ELM327 on BLE when the framed
// link is built (enabled). Nothing else.
void      outputsSeedDefaults();
// Is this record usable as stored? (id matches its slot, format built,
// transport allowed for the format, interval in range). Used on load.
bool      outputRecordValid(const OutputRecord& r, uint8_t slot);

// Is this format a CAN layout (PREOBD, HALTECH: pushes fixed frames)?
inline bool outputFormatIsLayout(uint8_t format) { return format == FMT_PREOBD || format == FMT_HALTECH; }
// Does this format transmit on its CAN bus? (OBD answers, the layouts push.)
// Such outputs are refused on LISTEN and on the bus CAN input reads (#632).
inline bool outputFormatSendsCan(uint8_t format) { return format == FMT_OBD || outputFormatIsLayout(format); }
// The CAN baud rate a format needs on its bus, 0 for any: HALTECH is 1 Mbit/s
// (Haltech's protocol; refused elsewhere at admission, ARCHITECTURE §14.7 item 3)
inline uint32_t outputFormatCanBaud(uint8_t format) { return format == FMT_HALTECH ? 1000000UL : 0; }
// The enabled CAN-sending outputs on this bus: a bit per output id (live table)
RouteMask outputCanSendersOnBus(uint8_t bus);

// ---- Formats and transports ----
bool      outputFormatBuilt(OutputFormat format);   // the format's code is in this image
OutputTransportKind outputTransportKind(uint8_t transport);
// May `format` run on `transport` at all (§14.4 compatibility table)? And is
// it a transport this board has (a serial port it has, a CAN bus it has)?
bool      outputTransportAllowed(OutputFormat format, uint8_t transport);
bool      outputFormatPaced(OutputFormat format);   // push formats have an interval
uint16_t  outputIntervalMin(OutputFormat format, uint8_t transport);
uint16_t  outputIntervalDefault(OutputFormat format, uint8_t transport);
bool      outputIntervalValid(OutputFormat format, uint8_t transport, long ms);
// The serial port (1-8) / CAN bus (0-2) a transport names, 0xFF if none
uint8_t   outputTransportSerialPort(uint8_t transport);
uint8_t   outputTransportCanBus(uint8_t transport);

// Names: CLI tokens and JSON values
const char*  outputFormatName(OutputFormat format); // "CSV", "REALDASH", "OBD", "ELM327"
OutputFormat parseOutputFormat(const char* s);      // case-insensitive; FMT_NONE if unknown
// "USB", "SERIAL2", "ESP32_BT", "CAN1", "SD", "BLE", "NONE"
size_t       outputTransportName(uint8_t transport, char* buf, size_t n);
// Parses those tokens (case-insensitive). OT_NONE for "NONE"; false if unknown.
bool         parseOutputTransport(const char* s, uint8_t* out);

#endif // OUTPUT_TABLE_H
