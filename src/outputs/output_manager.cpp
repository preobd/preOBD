/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"
#include "../config.h"
#include "../inputs/input_manager.h"
#include "../core/message_router.h"
#include "../core/message_api.h"

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

#include "output_sdlog.h"   // initSDLog / updateSDLog / closeSDLog
#include "output_format.h"  // sdLogFormatName for OUTPUT STATUS

extern void initAlarmOutput();
extern void sendAlarmOutput(Input*);
extern void updateAlarmOutput();

// Relay output module removed: a relay is now a control-engine Action of type
// RELAY, driven by rulesActionsUpdate() in the main loop (not an output
// module). The OUTPUT_RELAY slot below stays nullptr for EEPROM index
// stability (OutputID enum / outputEnabled[]).

#if INCLUDE_ELM327
extern void initELM327();
extern void sendELM327(Input*);
extern void updateELM327();
#endif

// Define output modules array - always compiled, controlled by runtime flags
OutputModule outputModules[] = {
    {"CAN",     false, initCAN,           sendCAN,           updateCAN,           100},
    {"RealDash",false, initRealdash,      sendRealdash,      updateRealdash,      100},
    {"Serial",  false, initSerialOutput,  sendSerialOutput,  updateSerialOutput,  1000},
#if INCLUDE_SD_LOGGING
    // send = nullptr: a wide CSV row needs the whole sample at once, so
    // updateSDLog() owns the cadence (reading systemConfig.outputInterval
    // itself) rather than being driven one input at a time by sendToOutputs().
    {"SD_Log",  false, initSDLog,         nullptr,           updateSDLog,         5000},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr,             0},
#endif
    {"Alarm",   true,  initAlarmOutput,   sendAlarmOutput,   updateAlarmOutput,   100},
    {nullptr,   false, nullptr,           nullptr,           nullptr,             0},  // OUTPUT_RELAY slot retired (control-engine Actions)
#if INCLUDE_ELM327
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
        if (outputModules[i].send == nullptr) continue;  // Owns its own cadence

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
 * Find an output module's index by name
 * @param name Output name (case-insensitive)
 * @return OutputID index, or -1 if not found
 *
 * Lets callers outside this TU identify a specific slot (e.g. OUTPUT_SD for the
 * SD-only FORMAT verb) without the outputModules array having to be extern'd.
 */
int getOutputIndexByName(const char* name) {
    OutputModule* output = getOutputByName(name);
    return output ? (int)(output - outputModules) : -1;
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
#if INCLUDE_SD_LOGGING
    bool wasEnabled = output->enabled;
#endif
    output->enabled = enabled;
    systemConfig.outputEnabled[index] = enabled ? 1 : 0;

    // Initialize if enabling for first time
    if (enabled && output->init != nullptr) {
        output->init();
    }

#if INCLUDE_SD_LOGGING
    // Close the log file on disable — otherwise it stays open and unflushed for
    // the rest of the session. Called directly rather than through a per-module
    // deinit hook: a function pointer on every OutputModule costs 14 bytes of
    // SRAM on the mega, which has 182 bytes free and cannot do SD logging at all.
    if (!enabled && wasEnabled && index == OUTPUT_SD) {
        closeSDLog();
    }
#endif

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
            msg.control.print(F("ms"));
#if INCLUDE_SD_LOGGING
            if (i == OUTPUT_SD) {
                msg.control.print(F(", Format: "));
                msg.control.print(sdLogFormatName(systemConfig.sdLogFormat));
            }
#endif
            msg.control.println();
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
