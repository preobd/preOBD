/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"
#include "../config.h"
#include "../inputs/input_manager.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"

// Output mask filtering relies on OUTPUT_CAN..OUTPUT_SD being 0-3.
// NUM_OUTPUTS is now unconditionally 7 (stable enum), so this assert is always safe.
static_assert(OUTPUT_CAN == 0 && OUTPUT_REALDASH == 1 &&
              OUTPUT_SERIAL == 2 && OUTPUT_SD == 3,
              "OutputID data output enum values must be 0-3 for per-input mask filtering");

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

#ifdef ENABLE_RELAY_OUTPUT
extern void initRelayOutput();
extern void sendRelayOutput(Input*);
extern void updateRelayOutput();
#endif

#ifdef ENABLE_ELM327_OUTPUT
extern void initELM327();
extern void sendELM327(Input*);
extern void updateELM327();
#endif

// Define output modules array - always compiled, controlled by runtime flags
OutputModule outputModules[] = {
    {"CAN",     false, initCAN,           sendCAN,           updateCAN,           100},
    {"RealDash",false, initRealdash,      sendRealdash,      updateRealdash,      100},
    {"Serial",  false, initSerialOutput,  sendSerialOutput,  updateSerialOutput,  1000},
    {"SD_Log",  false, initSDLog,         sendSDLog,         updateSDLog,         5000},
    {"Alarm",   true,  initAlarmOutput,   sendAlarmOutput,   updateAlarmOutput,   100},
#ifdef ENABLE_RELAY_OUTPUT
    {"Relay",   true,  initRelayOutput,   sendRelayOutput,   updateRelayOutput,   100},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr,             0},
#endif
#ifdef ENABLE_ELM327_OUTPUT
    // sendInterval = UINT16_MAX: ELM327 is pull-based, send() is a no-op.
    // Setting interval to max prevents sendToOutputs() from iterating all
    // inputs every loop just to call the no-op.
    {"ELM327",  false, initELM327,        sendELM327,        updateELM327,        UINT16_MAX},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr,             0},
#endif
};

// Always 7 entries — matches NUM_OUTPUTS enum (stable regardless of compile flags)
const int numOutputModules = 7;

// Track last send time for each output module
static uint32_t lastOutputSend[sizeof(outputModules) / sizeof(outputModules[0])];

void initOutputModules() {
    // Apply runtime configuration from system config
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;  // Placeholder slot

        outputModules[i].enabled = systemConfig.outputEnabled[i];
        outputModules[i].sendInterval = systemConfig.outputInterval[i];

        if (outputModules[i].enabled && outputModules[i].init != nullptr) {
            outputModules[i].init();
        }
        lastOutputSend[i] = 0;  // Initialize timing
    }
}

// Send data to all outputs at their configured intervals
void sendToOutputs(uint32_t now) {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;  // Placeholder slot
        if (!outputModules[i].enabled) continue;

        // Check if enough time has elapsed for this output
        if (now - lastOutputSend[i] >= outputModules[i].sendInterval) {
            // Send all enabled inputs to this output
            for (uint8_t j = 0; j < MAX_INPUTS; j++) {
                if (inputs[j].flags.isEnabled && !isnan(inputs[j].value)) {
                    // For data outputs (CAN/RealDash/Serial/SD), check per-input mask
                    if (i <= OUTPUT_SD && !(inputs[j].outputMask & (1 << i))) continue;
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
        if (outputModules[i].name == nullptr) continue;  // Placeholder slot
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
        if (outputModules[i].name == nullptr) continue;
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
    msg.control.println(F("=== Output Modules ==="));
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;
        msg.control.print(outputModules[i].name);
        msg.control.print(F(": "));
        if (outputModules[i].enabled) {
            msg.control.print(F("Enabled, Interval: "));
            msg.control.print(outputModules[i].sendInterval);
            msg.control.println(F("ms"));
        } else {
            msg.control.println(F("Disabled"));
        }
    }
}

/**
 * List available output module names
 */
void listOutputModules() {
    msg.control.println(F("=== Available Output Modules ==="));
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;
        msg.control.println(outputModules[i].name);
    }
}
