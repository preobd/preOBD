/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"
#include "output_table.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif
#include "../../config.h"
#include "../../inputs/input_manager.h"
#include "../../core/message_router.h"
#include "../../core/message_api.h"

// Output mask filtering relies on OUTPUT_CAN..OUTPUT_SD being 0-3.
// NUM_OUTPUTS is now unconditionally 7 (stable enum), so this assert is always safe.
static_assert(OUTPUT_CAN == 0 && OUTPUT_REALDASH == 1 &&
              OUTPUT_SERIAL == 2 && OUTPUT_SD == 3,
              "OutputID data output enum values must be 0-3 for per-input mask filtering");

// Declare external functions from output modules
extern void initCAN();
extern void stopCAN();
extern void updateCAN();

extern void initRealdash();
extern void sendRealdash(Input*);
extern void updateRealdash();

extern void initSerialOutput();
extern void sendSerialOutput(Input*);
extern void updateSerialOutput();

#include "../formats/output_sdlog.h"   // initSDLog / updateSDLog / closeSDLog
#include "../formats/output_format.h"  // sdLogFormatName for OUTPUT LIST

extern void initAlarmOutput();
extern void updateAlarmOutput();
extern void stopAlarmOutput();

#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_ELM327
#include "../formats/elm327_framed.h"
// Output 5's init: a fresh ELM327 session over BLE
static void initELM327Ble() { Elm327Framed::init(); }
#endif
#if INCLUDE_ELM327
extern void initELM327();
extern void updateELM327();
extern void stopELM327();
extern void noteELM327PortState();
extern uint8_t elm327BoundPort();
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
#if INCLUDE_FRAMED_TRANSPORT && INCLUDE_ELM327
    // ELM327 over BLE (#612 phase 2a): answers 0x30 frames from the nRF as
    // they arrive (elm327_framed.cpp checks this entry's live flag)
    {"ELM327_BLE", false, initELM327Ble,  nullptr,           nullptr},
#else
    {nullptr,   false, nullptr,           nullptr,           nullptr},
#endif
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

// A module slot's output id (#612): slots 0-3 are ids 0-3, ELM327's slot 6
// is id 4, ELM327 over BLE's slot 5 is id 5. Alarm is a built-in output (no id).
static uint8_t idOfSlot(int slot) {
    if (slot <= OUTPUT_SD) return (uint8_t)slot;
    if (slot == OUTPUT_ELM327) return OUT_ID_ELM327;
    if (slot == OUTPUT_ELM327_BLE) return OUT_ID_ELM327_BLE;
    return OUT_ID_NONE;
}

// What a slot's live state should be: switched on in the config, and for the
// outputs you add, not deleted (OUTPUT DEL). Built-ins only need the switch.
static bool slotWanted(int slot) {
    if (!systemConfig.outputEnabled[slot]) return false;
    const uint8_t id = idOfSlot(slot);
    return id == OUT_ID_NONE || outputExists(id);
}

void initOutputModules() {
    // Apply runtime configuration from system config
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;  // Placeholder slot

        outputModules[i].enabled = slotWanted(i);

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
                    if (i <= OUTPUT_SD && !(inputs[j].routeMask & (1 << i))) continue;
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

// The one place an output's live state changes: OUTPUT <id> ENABLE/DISABLE,
// a JSON IMPORT and LOAD all come here, so each takes effect immediately
// (#610). Live state only — systemConfig is the caller's (setOutputEnabled
// writes it; the sync reads it). Enabling runs init() even when the output is
// already on: OUTPUT <id> ENABLE is how a user retries a faulted SD log or
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
#if INCLUDE_CAN
    // Detach from the bus; it keeps running for CAN input
    if (index == OUTPUT_CAN) stopCAN();
#endif
#if INCLUDE_SD_LOGGING
    // Close the log file, or it stays open and unflushed for the session.
    if (index == OUTPUT_SD) closeSDLog();
#endif
    // Silence the buzzer and blank the LED, or they hold their last state
    // once updateAlarmOutput() stops running (#610).
    if (index == OUTPUT_ALARM) stopAlarmOutput();
#if INCLUDE_ELM327
    // Unbind from its serial port
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

// After a JSON IMPORT or LOAD: every output to the state the config asks for,
// at once (#610). An output that stays on but whose transport changed (OBD to
// another CAN bus, ELM327 to another serial port) is re-attached.
FLASHMEM void syncOutputsToConfig() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].name == nullptr) continue;  // not in this build
        const bool want = slotWanted(i);
        if (outputModules[i].enabled != want) {
            applyOutputEnabled(i, want);
#if INCLUDE_ELM327
            // initELM327() skips a port that isn't running yet, so say so
            if (want && i == OUTPUT_ELM327) noteELM327PortState();
#endif
            continue;
        }
        if (!want) continue;
#if INCLUDE_CAN
        if (i == OUTPUT_CAN && canOutputAttachedBus() != systemConfig.buses.obdBus) initCAN();
#endif
#if INCLUDE_ELM327
        if (i == OUTPUT_ELM327 && elm327BoundPort() != systemConfig.buses.elm327_serial_port) {
            initELM327();
            noteELM327PortState();
        }
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

// ===== The outputs you add, by id (#612) =====
// The table itself is output_table.cpp (a view over systemConfig); these are
// the parts that start and stop the modules behind it.

static int slotOfId(uint8_t id) {
    const uint8_t slot = outputLegacySlot(id);
    return slot == 0xFF ? -1 : (int)slot;
}

FLASHMEM bool outputSetEnabledById(uint8_t id, bool enabled) {
    if (!outputExists(id)) return false;
    const int slot = slotOfId(id);
    systemConfig.outputEnabled[slot] = enabled ? 1 : 0;
    applyOutputEnabled(slot, enabled);
    return true;
}

FLASHMEM bool outputSetIntervalById(uint8_t id, long ms) {
    if (!outputExists(id)) return false;
    return setOutputInterval((uint8_t)slotOfId(id), ms);
}

FLASHMEM bool outputLiveById(uint8_t id) {
    const int slot = slotOfId(id);
    return slot >= 0 && outputModules[slot].name != nullptr && outputModules[slot].enabled;
}

// Sets route bit <id> on every input (ADD: a new output is sent everything,
// as each output always was) or clears it (DEL).
static FLASHMEM void routeEveryInput(uint8_t id, bool on) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) continue;
        if (on) inputs[i].routeMask |= (uint8_t)(1u << id);
        else    inputs[i].routeMask &= (uint8_t)~(1u << id);
    }
}

FLASHMEM void outputDeleteById(uint8_t id) {
    if (!outputExists(id)) return;
    outputSetEnabledById(id, false);   // stops it, as DISABLE does
    outputMarkAbsent(id, true);
    routeEveryInput(id, false);
}

FLASHMEM bool outputAddById(uint8_t id) {
    if (!outputIdBuilt(id) || outputExists(id)) return false;
    outputMarkAbsent(id, false);
    routeEveryInput(id, true);
    // On, through the same path as OUTPUT <id> ENABLE
    outputSetEnabledById(id, true);
    return true;
}

// Transport text for an output, e.g. "CAN1", "USB", "USB+SERIAL1", "SD",
// "SERIAL2", or "NONE" when the setting it comes from has none.
static const char* const TRANSPORT_TOKENS[NUM_TRANSPORTS] = {
    "NONE", "USB", "SERIAL1", "SERIAL2", "SERIAL3", "SERIAL4",
    "SERIAL5", "SERIAL6", "SERIAL7", "SERIAL8", "ESP32_BT"};

static const char* transportToken(uint8_t t) {
    return t < NUM_TRANSPORTS ? TRANSPORT_TOKENS[t] : "NONE";
}

FLASHMEM size_t outputTransportText(uint8_t id, char* buf, size_t n) {
    if (n == 0) return 0;
    buf[0] = '\0';
    switch (outputKindOf(id)) {
    case OTK_CAN: {
        const uint8_t bus = systemConfig.buses.obdBus;
        if (bus == 0xFF) snprintf(buf, n, "NONE");
        else             snprintf(buf, n, "CAN%u", (unsigned)(bus + 1));
        break;
    }
    case OTK_STREAM: {
        const uint8_t p = systemConfig.router.data_primary;
        const uint8_t s = systemConfig.router.data_secondary;
        if (s != TRANSPORT_NONE && s != p)
            snprintf(buf, n, "%s+%s", transportToken(p), transportToken(s));
        else
            snprintf(buf, n, "%s", transportToken(p));
        break;
    }
    case OTK_SD:
        snprintf(buf, n, "SD");
        break;
    case OTK_SERIAL: {
        const uint8_t port = systemConfig.buses.elm327_serial_port;
        if (port == 0xFF || port == 0) snprintf(buf, n, "NONE");
        else                           snprintf(buf, n, "SERIAL%u", (unsigned)port);
        break;
    }
    case OTK_BLE:
        // The phone-facing channel of the framed nRF link
        snprintf(buf, n, "BLE");
        break;
    }
    return strlen(buf);
}

FLASHMEM void printOutputLabel(uint8_t id) {
    char t[24];
    outputTransportText(id, t, sizeof(t));
    msg.control.print(outputFormatName(outputFormatOf(id)));
    msg.control.print(F(" on "));
    msg.control.print(t);
}

// One line of OUTPUT LIST, also the head of OUTPUT <id>
static FLASHMEM void printOutputLine(uint8_t id) {
    msg.control.print(id);
    msg.control.print(F(": "));
    printOutputLabel(id);
    msg.control.print(F(" - "));
    msg.control.print(outputIsEnabled(id) ? F("enabled") : F("disabled"));
    if (outputKindOf(id) == OTK_BLE && !systemConfig.serial.framed_enabled_mask) {
        msg.control.print(F(" (no framed port: BUS SERIAL <n> FRAMED ENABLE)"));
    }
    if (outputTakesIntervalId(id)) {
        msg.control.print(F(", interval "));
        msg.control.print(outputIntervalOf(id));
        msg.control.print(F(" ms"));
    }
#if INCLUDE_SD_LOGGING
    if (id == OUT_ID_SDLOG) {
        msg.control.print(F(", "));
        msg.control.print(sdLogFormatName(systemConfig.sdLogFormat));
    }
#endif
    msg.control.println();
}

FLASHMEM void listOutputTable() {
    msg.control.println(F("=== Outputs ==="));
    if (outputCount() == 0) msg.control.println(F("(none - OUTPUT ADD <format> <transport>)"));
    for (uint8_t id = 0; id < NUM_OUTPUT_IDS; id++) {
        if (outputExists(id)) printOutputLine(id);
    }
    // Built-in outputs: always present, no id, no routes (ARCHITECTURE.md §14.3)
    msg.control.println(F("Built-in (no routes):"));
    msg.control.print(F("  Alarm - "));
    msg.control.println(systemConfig.outputEnabled[OUTPUT_ALARM] ? F("enabled") : F("disabled"));
#if INCLUDE_LCD || INCLUDE_OLED
    msg.control.println(F("  Display - see DISPLAY STATUS"));
#endif
#if INCLUDE_FRAMED_TRANSPORT
    msg.control.println(F("  App link (BLE)"));
#endif
}

FLASHMEM bool printOutputDetail(uint8_t id) {
    if (!outputExists(id)) return false;
    printOutputLine(id);
    msg.control.print(F("  Routed inputs:"));
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF || !(inputs[i].routeMask & (1u << id))) continue;
        msg.control.print(' ');
        msg.control.print(inputs[i].abbrName);
        n++;
    }
    if (n == 0) msg.control.print(F(" none"));
    msg.control.println();
    return true;
}
