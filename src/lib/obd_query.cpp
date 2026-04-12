/*
 * obd_query.cpp — Platform-agnostic OBD-II Mode 01 query layer
 *
 * Moved from output_can.cpp static functions. Shared by output_can.cpp (CAN)
 * and output_elm327.cpp (BLE serial). See obd_query.h for API.
 */

#include "obd_query.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../outputs/output_base.h"
#include "message_api.h"
#include "log_tags.h"

// PID Lookup Table — Maps PIDs to Input pointers for fast lookup
#define MAX_PID_ENTRIES 64

struct PIDMapping {
    uint8_t pid;
    Input*  inputPtr;
};

static PIDMapping pidLookupTable[MAX_PID_ENTRIES];
static uint8_t    pidLookupCount = 0;

void obdQuery_buildLookupTable() {
    pidLookupCount = 0;

    for (uint8_t i = 0; i < MAX_INPUTS && pidLookupCount < MAX_PID_ENTRIES; i++) {
        if (inputs[i].flags.isEnabled && inputs[i].obd2pid != 0x00) {
            bool isDuplicate = false;
            for (uint8_t j = 0; j < pidLookupCount; j++) {
                if (pidLookupTable[j].pid == inputs[i].obd2pid) {
                    isDuplicate = true;
                    msg.debug.warn(TAG_CAN, "Duplicate PID 0x%02X - using first (%s)",
                                   inputs[i].obd2pid, pidLookupTable[j].inputPtr->abbrName);
                    break;
                }
            }
            if (!isDuplicate) {
                pidLookupTable[pidLookupCount].pid      = inputs[i].obd2pid;
                pidLookupTable[pidLookupCount].inputPtr = &inputs[i];
                pidLookupCount++;
            }
        }
    }

    msg.debug.info(TAG_CAN, "OBD-II PID table: %d PIDs", pidLookupCount);
}

Input* obdQuery_findByPID(uint8_t pid) {
    for (uint8_t i = 0; i < pidLookupCount; i++) {
        if (pidLookupTable[i].pid == pid) return pidLookupTable[i].inputPtr;
    }
    return nullptr;
}

void obdQuery_getSupportedPIDBitmap(uint8_t* bitmap) {
    memset(bitmap, 0, 4);
    for (uint8_t i = 0; i < pidLookupCount; i++) {
        uint8_t pid = pidLookupTable[i].pid;
        if (pid >= 0x01 && pid <= 0x20) {
            uint8_t byteIdx = (pid - 1) / 8;
            uint8_t bitIdx  = 7 - ((pid - 1) % 8);
            bitmap[byteIdx] |= (1 << bitIdx);
        }
    }
}

bool obdQuery_resolve(uint8_t mode, uint8_t pid, uint8_t* out, uint8_t* outLen) {
    if (mode != 0x01) return false;

    if (pid == 0x00) {
        uint8_t bitmap[4];
        obdQuery_getSupportedPIDBitmap(bitmap);
        out[0] = 0x41; out[1] = 0x00;
        out[2] = bitmap[0]; out[3] = bitmap[1];
        out[4] = bitmap[2]; out[5] = bitmap[3];
        *outLen = 6;
        return true;
    }

    Input* input = obdQuery_findByPID(pid);
    if (input == nullptr || isnan(input->value)) return false;

    byte frameData[8];
    if (!buildOBD2Frame(frameData, input)) return false;

    // frameData[0] = length (2 + dataBytes), [1]=0x41, [2]=pid, [3+]=data
    uint8_t len = frameData[0];
    out[0] = frameData[1];
    out[1] = frameData[2];
    for (uint8_t i = 0; i < len - 2; i++) out[2 + i] = frameData[3 + i];
    *outLen = len;
    return true;
}
