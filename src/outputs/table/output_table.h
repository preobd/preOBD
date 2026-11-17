/*
 * output_table.h - The outputs you add (#612, ARCHITECTURE.md §14.5)
 *
 * An output is a format on a transport, with an id. Today's modules are one
 * per format and transport kind, so each output's id is fixed:
 *
 *   id 0  OBD       on a CAN bus              (BUS CAN <bus> / OUTPUT ADD OBD CAN<n>)
 *   id 1  REALDASH  on the data stream
 *   id 2  CSV       on the data stream
 *   id 3  CSV       on the SD card
 *   id 4  ELM327    on a serial port          (OUTPUT ADD ELM327 SERIAL<n>)
 *   id 5  ELM327    over BLE (the framed nRF link; OUTPUT ADD ELM327 BLE)
 *
 * The table is a view over systemConfig: outputEnabled[] / outputInterval[] /
 * sdLogFormat, the transport settings (buses.obdBus, buses.elm327_serial_port),
 * and router.outputsAbsent for the outputs that were deleted. Several outputs
 * per format (phase 2b) replace the view with a stored table.
 *
 * Built-in outputs (display, app link, alarm) are not in this table and take
 * no routes.
 */

#ifndef OUTPUT_TABLE_H
#define OUTPUT_TABLE_H

#include <stdint.h>
#include "../../config/system_config.h"

// One route bit per output id, on each input (Input::routeMask)
typedef uint8_t RouteMask;

enum OutputFormat : uint8_t {
    FMT_NONE = 0,
    FMT_CSV,
    FMT_REALDASH,
    FMT_OBD,
    FMT_ELM327,
};

// Fixed ids (see the header comment)
enum OutputIdFixed : uint8_t {
    OUT_ID_OBD      = 0,
    OUT_ID_REALDASH = 1,
    OUT_ID_CSV      = 2,
    OUT_ID_SDLOG    = 3,
    OUT_ID_ELM327   = 4,
    OUT_ID_ELM327_BLE = 5,
    NUM_OUTPUT_IDS  = 6,
    OUT_ID_NONE     = 0xFF,
};
static_assert(NUM_OUTPUT_IDS <= 8 * sizeof(RouteMask), "every output id needs a route bit");
static_assert(OUT_ID_OBD == OUTPUT_CAN && OUT_ID_REALDASH == OUTPUT_REALDASH &&
              OUT_ID_CSV == OUTPUT_SERIAL && OUT_ID_SDLOG == OUTPUT_SD,
              "ids 0-3 index outputEnabled[] directly (sendToOutputs' route check)");

// The kind of transport an output runs on
enum OutputTransportKind : uint8_t {
    OTK_CAN,       // a CAN bus (buses.obdBus)
    OTK_STREAM,    // the data stream (TRANSPORT DATA)
    OTK_SD,        // the SD card
    OTK_SERIAL,    // a serial port (buses.elm327_serial_port)
    OTK_BLE,       // the phone-facing side of the framed nRF link
};

// Static facts about an id
OutputFormat        outputFormatOf(uint8_t id);
OutputTransportKind outputKindOf(uint8_t id);
uint8_t             outputLegacySlot(uint8_t id);   // index into outputEnabled[]/outputInterval[]
bool                outputIdBuilt(uint8_t id);      // the format's code is in this image
bool                outputTakesIntervalId(uint8_t id);
uint16_t            outputIntervalMinId(uint8_t id);

// The table as it stands (reads systemConfig)
bool      outputExists(uint8_t id);                 // built and not deleted
bool      outputIsEnabled(uint8_t id);              // exists and switched on
uint16_t  outputIntervalOf(uint8_t id);
uint8_t   outputCount();
RouteMask outputsExistingMask();                    // a route bit for every output that exists
uint8_t   outputIdFor(OutputFormat format, OutputTransportKind kind);  // OUT_ID_NONE if no such id

// Changes (write systemConfig; live start/stop is output_manager's job)
void outputMarkAbsent(uint8_t id, bool absent);     // DEL / ADD
// Is `tok` (a JSON "transport") the kind of transport output <id> uses?
// CAN<n>/NONE for OBD, SERIAL<n>/NONE for ELM327, BLE for ELM327 over BLE,
// SD for the SD log, and a stream (anything but CAN<n>, SD, BLE or NONE) for
// RealDash and CSV.
bool outputTransportKindOk(uint8_t id, const char* tok);

// Names: the CLI token and JSON value of a format ("CSV", "REALDASH", "OBD", "ELM327")
const char*  outputFormatName(OutputFormat format);
OutputFormat parseOutputFormat(const char* s);      // case-insensitive; FMT_NONE if unknown

#endif // OUTPUT_TABLE_H
