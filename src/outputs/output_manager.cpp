/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif
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
extern void updateCAN();
extern bool canOutputBusStarted();
extern bool canOutputConfigured();

extern void initRealdash();
extern void sendRealdash(Input*);
extern void updateRealdash();

extern void initSerialOutput();
extern void sendSerialOutput(Input*);
extern void updateSerialOutput();

#include "output_sdlog.h"   // initSDLog / updateSDLog / closeSDLog
#include "output_format.h"  // sdLogFormatName for OUTPUT STATUS

extern void initAlarmOutput();
extern void updateAlarmOutput();
extern void stopAlarmOutput();

// Relay output module removed: a relay is now a control-engine Action of type
// RELAY, driven by rulesActionsUpdate() in the main loop (not an output
// module). The OUTPUT_RELAY slot below stays nullptr for EEPROM index
// stability (OutputID enum / outputEnabled[]).

#if INCLUDE_ELM327
extern void initELM327();
extern void updateELM327();
extern void stopELM327();
extern void noteELM327PortState();
#endif

// Define output modules array - always compiled, controlled by runtime flags.
// Which of these pace on outputInterval[] is outputTakesInterval()
// (system_config.h); a new paced output must be added there too (#608).
OutputModule outputModules[] = {
    {"CAN",     false, initCAN,           nullptr,           updateCAN},  // responder only (#528)
    {"RealDash",false, initRealdash,      sendRealdash,      updateRealdash},
    {"Serial",  false, initSerialOutput,  sendSerialOutput,  updateSerialOutput},
#if INCLUDE_SD_LOGGING
    // send = nullptr: a wide CSV row needs the whole sample at once, so
    // updateSDLog() owns the cadence (reading systemConfig.outputInterval
    // itself) rather than being driven one input at a time by sendToOutputs().
    {"SD_Log",  false, initSDLog,         nullptr,           updateSDLog},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr},
#endif
    // Alarm: updateAlarmOutput() scans every input each loop (#608)
    {"Alarm",   true,  initAlarmOutput,   nullptr,           updateAlarmOutput},
    {nullptr,   false, nullptr,           nullptr,           nullptr},  // OUTPUT_RELAY slot retired (control-engine Actions)
#if INCLUDE_ELM327
    // Pull-based: the phone asks, updateELM327() answers (#608)
    {"ELM327",  false, initELM327,        nullptr,           updateELM327},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr},
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
        if (now - lastOutputSend[i] >= systemConfig.outputInterval[i]) {
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

// The one place an output's live state changes: OUTPUT <name> ENABLE/DISABLE,
// a JSON IMPORT and LOAD all come here, so each takes effect immediately
// (#610). Live state only — systemConfig is the caller's (setOutputEnabled
// writes it; the sync reads it). Enabling runs init() even when the output is
// already on: OUTPUT <name> ENABLE is how a user retries a faulted SD log or
// an ELM327 whose port wasn't up. The sync only calls this on a change.
static FLASHMEM void applyOutputEnabled(int index, bool enabled) {
    OutputModule* output = &outputModules[index];
    const bool wasEnabled = output->enabled;
    output->enabled = enabled;

    if (enabled && output->init != nullptr) {
        output->init();
    }
    if (enabled || !wasEnabled) return;

    // Teardown on disable is called directly rather than through a per-module
    // deinit hook: a function pointer on every OutputModule costs 14 bytes of
    // SRAM on the mega, which has 182 bytes free.
#if INCLUDE_SD_LOGGING
    // Close the log file, or it stays open and unflushed for the session.
    if (index == OUTPUT_SD) closeSDLog();
#endif
    // Silence the buzzer and blank the LED, or they hold their last state
    // once updateAlarmOutput() stops running (#610).
    if (index == OUTPUT_ALARM) stopAlarmOutput();
#if INCLUDE_ELM327
    // Unbind from the port, as BUS SERIAL <n> ELM327 DISABLE does.
    if (index == OUTPUT_ELM327) stopELM327();
#endif
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
    systemConfig.outputEnabled[output - outputModules] = enabled ? 1 : 0;
    applyOutputEnabled(output - outputModules, enabled);
    return true;
}

FLASHMEM bool canOutputPendingReboot() {
    return !outputModules[OUTPUT_CAN].enabled && systemConfig.outputEnabled[OUTPUT_CAN] &&
           canOutputConfigured();
}

FLASHMEM void syncOutputsToConfig() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;  // not in this build
        const bool want = systemConfig.outputEnabled[i] != 0;
        if (outputModules[i].enabled == want) continue;
        // CAN buses are brought up at boot from this config: starting CAN
        // output now would re-begin a bus CAN input set up (its mode, baud and
        // filters). If initCAN() already started it this session, turning it
        // back on is just the flag; otherwise it starts at the next reboot,
        // says so, and OUTPUT STATUS shows it pending. Stopping touches no bus.
        if (want && i == OUTPUT_CAN) {
            if (canOutputBusStarted()) {
                outputModules[i].enabled = true;
            } else if (canOutputPendingReboot()) {
                // Like the import's serial NOTE: nothing is saved until SAVE
                msg.control.println(F("NOTE: CAN output turns on after SAVE and a reboot"));
            }
            continue;
        }
        applyOutputEnabled(i, want);
#if INCLUDE_ELM327
        // initELM327() quietly skips a port that isn't running yet (one this
        // same import enabled waits for the reboot), so say so
        if (want && i == OUTPUT_ELM327) noteELM327PortState();
#endif
    }
}

/**
 * Set a paced output's send interval
 * @return false for an unknown output, one without an interval, or a value
 *         outside outputIntervalValid() — nothing changes then
 */
bool setOutputInterval(uint8_t outputId, long interval) {
    // Every writer is held to the same rule (#608): a 0 here would make the
    // SD log write a row every loop and Serial flood its port.
    if (outputId >= NUM_OUTPUTS || !outputIntervalValid(outputId, interval)) return false;
    systemConfig.outputInterval[outputId] = (uint16_t)interval;  // valid => fits
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
        if (i == OUTPUT_CAN && canOutputPendingReboot()) {
            msg.control.println(F("Disabled (on after SAVE and reboot)"));
        } else if (outputModules[i].enabled) {
            msg.control.print(F("Enabled"));
            if (outputTakesInterval(i)) {   // the others have none (#608)
                msg.control.print(F(", Interval: "));
                msg.control.print(systemConfig.outputInterval[i]);
                msg.control.print(F("ms"));
            }
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
