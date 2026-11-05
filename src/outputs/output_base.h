/*
 * output_base.h - Base interface for output modules
 */

#ifndef OUTPUT_BASE_H
#define OUTPUT_BASE_H

#include "../inputs/input.h"
#include "../sensors/calibration_types.h"
#include "../sensors/sensor_library.h"
#include "obd_encode.h"
#include "../storage/system_config.h"  // For OutputID enum

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
// the same path as OUTPUT <name> ENABLE/DISABLE (init on enable, teardown on
// disable). Only outputs whose state differs are touched. JSON IMPORT and LOAD
// call it, so changes apply without a reboot (#610). The exception is turning
// CAN output on when its bus wasn't started this session: that waits for the
// reboot (NOTE; STATUS and LIST OUTPUTS JSON show it pending).
void syncOutputsToConfig();
// CAN output is enabled in the config, configured to start (a bus selected
// and on), but not running: an import/LOAD enable waiting for SAVE + reboot
// (#610). One predicate for the sync's NOTE, OUTPUT STATUS and LIST OUTPUTS JSON.
bool canOutputPendingReboot();
bool setOutputInterval(uint8_t outputId, long interval);  // false unless outputIntervalValid(); nothing changes
OutputModule* getOutputByName(const char* name);
int getOutputIndexByName(const char* name);  // OutputID index, or -1 if unknown
void listOutputs();          // Show output status (enabled/disabled + intervals)
void listOutputModules();    // Show available output module names

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
