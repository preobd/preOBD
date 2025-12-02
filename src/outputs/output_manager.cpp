/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"
#include "../config.h"
#include "../inputs/input_manager.h"

// Declare external functions from output modules
extern void initCAN();
extern void sendCAN(Input*);
extern void updateCAN();

extern void initRealdash();
extern void sendRealdash(Input*);
extern void updateRealdash();

extern void initSerialOutput();
extern void sendSerialOutput(Input*);
extern void updateSerialOutput();

extern void initSDLog();
extern void sendSDLog(Input*);
extern void updateSDLog();

// Define output modules array with send intervals
OutputModule outputModules[] = {
    #ifdef ENABLE_CAN
    {"CAN", true, initCAN, sendCAN, updateCAN, CAN_OUTPUT_INTERVAL_MS},
    #endif

    #ifdef ENABLE_REALDASH
    {"RealDash", true, initRealdash, sendRealdash, updateRealdash, REALDASH_INTERVAL_MS},
    #endif

    #ifdef ENABLE_SERIAL_OUTPUT
    {"Serial", true, initSerialOutput, sendSerialOutput, updateSerialOutput, SERIAL_CSV_INTERVAL_MS},
    #endif

    #ifdef ENABLE_SD_LOGGING
    {"SD_Log", true, initSDLog, sendSDLog, updateSDLog, SD_LOG_INTERVAL_MS},
    #endif
};

const int numOutputModules = sizeof(outputModules) / sizeof(outputModules[0]);

// Track last send time for each output module
static uint32_t lastOutputSend[sizeof(outputModules) / sizeof(outputModules[0])];

void initOutputModules() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].enabled && outputModules[i].init != nullptr) {
            outputModules[i].init();
        }
        lastOutputSend[i] = 0;  // Initialize timing
    }
}

// Send data to all outputs at their configured intervals
void sendToOutputs(uint32_t now) {
    for (int i = 0; i < numOutputModules; i++) {
        if (!outputModules[i].enabled) continue;

        // Check if enough time has elapsed for this output
        if (now - lastOutputSend[i] >= outputModules[i].sendInterval) {
            // Send all enabled inputs to this output
            for (uint8_t j = 0; j < MAX_INPUTS; j++) {
                if (inputs[j].flags.isEnabled && !isnan(inputs[j].value)) {
                    outputModules[i].send(&inputs[j]);
                }
            }
            lastOutputSend[i] = now;
        }
    }
}

// Housekeeping - called every loop (drain buffers, handle RX, etc.)
void updateOutputs() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].enabled && outputModules[i].update != nullptr) {
            outputModules[i].update();
        }
    }
}
