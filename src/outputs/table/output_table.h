/*
 * output_table.h - The outputs you add (#612 phase 1, ARCHITECTURE.md §14.5)
 *
 * An output is a format on a transport, with an id. Phase 1 keeps today's
 * modules, one per format, so each output's id is fixed by its format and
 * matches the per-input route bit it has always used:
 *
 *   id 0  OBD       on the CAN output bus     (was the CAN module)
 *   id 1  REALDASH  on the data stream        (was RealDash)
 *   id 2  CSV       on the data stream        (was Serial)
 *   id 3  CSV       on the SD card            (was SD_Log)
 *   id 4  ELM327    on its serial port        (was ELM327; new route bit)
 *
 * The table is a view over the settings that already hold this state:
 * systemConfig.outputEnabled[] / outputInterval[] / sdLogFormat, plus
 * router.outputsAbsent for the outputs that were deleted. Nothing new is
 * stored, so a unit flashed from an older build keeps every output, and an
 * older build flashed back keeps them too. Phase 2 (several outputs per
 * format) replaces the view with a stored table that starts from these ids.
 *
 * Built-in outputs (display, app link, alarm, ELM327 over BLE) are not in
 * this table and take no routes.
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

// Phase-1 ids, fixed per format (see the header comment)
enum OutputIdPhase1 : uint8_t {
    OUT_ID_OBD      = 0,
    OUT_ID_REALDASH = 1,
    OUT_ID_CSV      = 2,
    OUT_ID_SDLOG    = 3,
    OUT_ID_ELM327   = 4,
    NUM_OUTPUT_IDS  = 5,
    OUT_ID_NONE     = 0xFF,
};
static_assert(NUM_OUTPUT_IDS <= 8 * sizeof(RouteMask), "every output id needs a route bit");
static_assert(OUT_ID_OBD == OUTPUT_CAN && OUT_ID_REALDASH == OUTPUT_REALDASH &&
              OUT_ID_CSV == OUTPUT_SERIAL && OUT_ID_SDLOG == OUTPUT_SD,
              "ids 0-3 are the old per-input output bits, so stored masks keep their meaning");

// Where an output's transport comes from in phase 1
enum OutputTransportKind : uint8_t {
    OTK_CAN,       // the CAN output bus (BUS CAN OUTPUT)
    OTK_STREAM,    // the data stream (TRANSPORT DATA)
    OTK_SD,        // the SD card
    OTK_SERIAL,    // a serial port bound to ELM327 (BUS SERIAL <n> ELM327)
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
// A deleted output is always off. An older build ignores outputsAbsent, so it
// can switch a deleted output back on and SAVE: that output is present again.
// Run after every systemConfig load, before anything reads outputEnabled[].
void outputsReconcileAbsent();
// Is `tok` (a JSON "transport") the kind of transport output <id> uses?
// CAN<n>/NONE for OBD, SERIAL<n>/NONE for ELM327, SD for the SD log, and a
// stream (anything but CAN<n>, SD or NONE) for RealDash and CSV.
bool outputTransportKindOk(uint8_t id, const char* tok);

// Names: the CLI token and JSON value of a format ("CSV", "REALDASH", "OBD", "ELM327")
const char*  outputFormatName(OutputFormat format);
OutputFormat parseOutputFormat(const char* s);      // case-insensitive; FMT_NONE if unknown

// An old (pre-phase-1) per-input mask in today's layout: bits 0-3 keep their
// meaning, and the ELM327 output gets the bit the CAN module's had, because
// the one CAN bit used to decide what both OBD-II responders answered.
RouteMask routeMaskFromLegacy(uint8_t legacyMask);

#endif // OUTPUT_TABLE_H
