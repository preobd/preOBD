/*
 * output_base.h - Base interface for output modules
 */

#ifndef OUTPUT_BASE_H
#define OUTPUT_BASE_H

#include "../../inputs/input.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/sensor_library.h"
#include "../formats/obd_encode.h"
#include "../../config/system_config.h"  // For OutputID enum

// Output module structure
typedef struct {
    const char* name;
    bool enabled;
    void (*init)(void);
    void (*send)(Input*);   // Per-input push, paced by systemConfig.outputInterval[]
                            // (the only copy, so CLI and JSON IMPORT apply at
                            // once, #608); nullptr for outputs that own their
                            // cadence (SD log), answer requests (CAN, ELM327)
                            // or run every loop (Alarm)
    void (*update)(void);   // Called each loop iteration
} OutputModule;

// The module table, indexed by OutputID (output_manager.cpp)
extern OutputModule outputModules[];

// Output module functions
void initOutputModules();
void sendToOutputs(uint32_t now);  // Send data to all outputs (time-sliced)
void updateOutputs();              // Housekeeping (drain buffers, etc.)

// Runtime configuration API
bool setOutputEnabled(const char* name, bool enabled);
// Turn each output on or off to match systemConfig.outputEnabled[], through
// the same path as OUTPUT <id> ENABLE/DISABLE (init on enable, teardown on
// disable). Only outputs whose state differs are touched, plus an enabled
// OBD or ELM327 output whose bus or port changed (re-attached). JSON IMPORT
// and LOAD call it, so changes apply without a reboot (#610, #612).
void syncOutputsToConfig();
uint8_t canOutputAttachedBus();   // the bus the OBD output is attached to, 0xFF if none
bool setOutputInterval(uint8_t outputId, long interval);  // false unless outputIntervalValid(); nothing changes
OutputModule* getOutputByName(const char* name);
int getOutputIndexByName(const char* name);  // OutputID index, or -1 if unknown

// ===== The outputs you add, by id (#612 phase 1, output_table.h) =====
// Start/stop and print; the table itself (which exist, intervals, names) is
// output_table.h. Each returns false when there is no output <id>.
bool outputSetEnabledById(uint8_t id, bool enabled);   // also starts or stops it
bool outputSetIntervalById(uint8_t id, long ms);       // false: no output, or outputIntervalValid() fails
bool outputLiveById(uint8_t id);                       // running now
bool outputAddById(uint8_t id);                        // ADD: routes every input to it and turns it on
void outputDeleteById(uint8_t id);                     // DEL: turns it off, removes it and its routes
size_t outputTransportText(uint8_t id, char* buf, size_t n);  // "CAN1", "USB", "SD", "SERIAL2", "NONE"
void printOutputLabel(uint8_t id);                     // "CSV on USB"
void listOutputTable();                                // OUTPUT LIST
bool printOutputDetail(uint8_t id);                    // OUTPUT <id>

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
