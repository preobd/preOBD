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

extern void initAlarmOutput();
extern void sendAlarmOutput(Input*);
extern void updateAlarmOutput();

// Define output modules array - always compiled, controlled by runtime flags
OutputModule outputModules[] = {
    {"CAN", false, initCAN, sendCAN, updateCAN, 100},
    {"RealDash", false, initRealdash, sendRealdash, updateRealdash, 100},
    {"Serial", false, initSerialOutput, sendSerialOutput, updateSerialOutput, 1000},
    {"SD_Log", false, initSDLog, sendSDLog, updateSDLog, 5000},
    {"Alarm", true, initAlarmOutput, sendAlarmOutput, updateAlarmOutput, 100},
};

const int numOutputModules = 5;  // Updated from 4 to 5

// Track last send time for each output module
static uint32_t lastOutputSend[sizeof(outputModules) / sizeof(outputModules[0])];

void initOutputModules() {
    // Apply runtime configuration from system config
    for (int i = 0; i < numOutputModules; i++) {
        outputModules[i].enabled = systemConfig.outputEnabled[i];
        outputModules[i].sendInterval = systemConfig.outputInterval[i];

        if (outputModules[i].enabled && outputModules[i].init != nullptr) {
            outputModules[i].init();
            Serial.print(F("✓ Initialized "));
            Serial.println(outputModules[i].name);
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

// ===== RUNTIME CONFIGURATION API =====

/**
 * Find output module by name
 * @param name Output name (case-insensitive)
 * @return Pointer to OutputModule, or nullptr if not found
 */
OutputModule* getOutputByName(const char* name) {
    for (int i = 0; i < numOutputModules; i++) {
        if (strcasecmp(outputModules[i].name, name) == 0) {
            return &outputModules[i];
        }
    }
    return nullptr;
}

/**
 * Enable or disable an output module
 * @param name Output name
 * @param enabled true to enable, false to disable
 * @return true if successful
 */
bool setOutputEnabled(const char* name, bool enabled) {
    OutputModule* output = getOutputByName(name);
    if (!output) return false;

    int index = output - outputModules;  // Calculate index
    output->enabled = enabled;
    systemConfig.outputEnabled[index] = enabled ? 1 : 0;

    // Initialize if enabling for first time
    if (enabled && output->init != nullptr) {
        output->init();
    }

    return true;
}

/**
 * Set output send interval
 * @param name Output name
 * @param interval Interval in milliseconds
 * @return true if successful
 */
bool setOutputInterval(const char* name, uint16_t interval) {
    OutputModule* output = getOutputByName(name);
    if (!output) return false;

    int index = output - outputModules;  // Calculate index
    output->sendInterval = interval;
    systemConfig.outputInterval[index] = interval;

    return true;
}

/**
 * List all outputs with their status
 */
void listOutputs() {
    Serial.println(F("=== Output Modules ==="));
    for (int i = 0; i < numOutputModules; i++) {
        Serial.print(outputModules[i].name);
        Serial.print(F(": "));
        if (outputModules[i].enabled) {
            Serial.print(F("Enabled, Interval: "));
            Serial.print(outputModules[i].sendInterval);
            Serial.println(F("ms"));
        } else {
            Serial.println(F("Disabled"));
        }
    }
}
