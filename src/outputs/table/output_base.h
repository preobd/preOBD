/*
 * output_base.h - Base interface for output modules
 */

#ifndef OUTPUT_BASE_H
#define OUTPUT_BASE_H

#include "../../inputs/input.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/sensor_library.h"
#include "../formats/obd_encode.h"
#include "../../config/system_config.h"
#include "output_table.h"
#include <Arduino.h>   // Print

// ===== The outputs, run from their records (#612 phase 2b-1) =====
// output_table.h holds the records; this is the part that starts and stops
// the code behind each one, paces the push formats and prints the table.

void initOutputModules();          // boot: start every enabled output, and the alarm
void sendToOutputs(uint32_t now);  // push formats (CSV, RealDash), each on its own timer
void updateOutputs();              // every loop: answer formats, the SD log, the alarm

// Every output to the state its record asks for, at once: started, stopped,
// or restarted when its transport changed (JSON IMPORT, LOAD; #610, #612).
void syncOutputsToConfig();

// Changes by id; each returns false when there is no output <id>. They write
// the record and apply it live. The CLI and JSON IMPORT check admission first
// (output_admission.h).
bool    outputSetEnabledById(uint8_t id, bool enabled);  // ENABLE also retries a failed start
// Registered by main.cpp: has the console fallen back to USB (#558)? Stream
// outputs on USB pause meanwhile, so the console's replies stay readable
typedef bool (*OutputConsoleFallbackProbe)();
void setOutputConsoleFallbackProbe(OutputConsoleFallbackProbe probe);

bool    outputSetIntervalById(uint8_t id, long ms);      // false unless outputIntervalValid()
bool    outputSetTransportById(uint8_t id, uint8_t transport);  // OUTPUT <id> TRANSPORT (same kind)
bool    outputSetOptionById(uint8_t id, uint8_t option); // CSV on SD: LONG/WIDE
bool    outputLiveById(uint8_t id);                      // running now
// ADD: a new record at the lowest free id, every input routed to it, started.
// OUT_ID_NONE when the table is full.
uint8_t outputAddRecord(OutputFormat format, uint8_t transport);
void    outputDeleteById(uint8_t id);                    // DEL: stops it, frees the id and its routes

// The built-in alarm (ALARM ENABLE|DISABLE)
void setAlarmEnabled(bool enabled);

uint8_t canOutputAttachedBus();    // the bus the OBD output is attached to, 0xFF if none

void printOutputLabel(uint8_t id);   // "CSV on USB"
void listOutputTable();              // OUTPUT LIST
bool printOutputDetail(uint8_t id);  // OUTPUT <id>

// ===== Format encoders: one input onto a stream (#612: no data plane) =====
void sendCsvLine(Input* in, Print& out);       // output_serial.cpp
void sendRealdashFrame(Input* in, Print& out); // output_realdash.cpp

// ===== OBDII FRAME BUILDING =====

// One CAN frame of an input's Mode 01 answer (ISO 15765-4 single-frame
// layout), for the frame-at-a-time consumers: CAN broadcast and RealDash.
//   frameData[0] = 2 + data bytes, [1] = 0x41, [2] = PID, [3..7] = data
// The encoding is the PID's (obd_encode.h, #516). A multi-frame answer
// (0x70 boost, 0x78 EGT) is cut to its first 5 data bytes, which hold the
// support byte and preOBD's one sensor value; the OBD-II responders send
// the whole answer instead.
// Returns false if the input has no answerable PID or no valid value.
inline bool buildOBD2Frame(byte* frameData, Input* ptr) {
    uint8_t answer[OBD_ANSWER_MAX];
    uint8_t n = obdBuildAnswer(*ptr, answer);
    if (n < 3) return false;
    if (n > 7) n = 7;
    for (int i = 0; i < 8; i++) frameData[i] = 0;
    frameData[0] = n;
    for (uint8_t i = 0; i < n; i++) frameData[1 + i] = answer[i];
    return true;
}

#endif
