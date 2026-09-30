/*
 * can_status_json.h - Machine-readable CAN scan and bus status (#510)
 *
 * The contract the webapp and mobile app build their CAN UIs on:
 *   SCAN STATUS JSON   small, polled ~1 Hz during a scan
 *   SCAN RESULTS JSON  the scan's findings, fetched when `found` changes
 *   BUS CAN JSON       live input/output/poll state + per-import age
 * Key names follow SYSTEM DUMP JSON (numeric bus index, 255 = none;
 * `baudrate`; `pin` + `pinLabel`; `sourceCanId`/`sourcePid`); config choices
 * are the UPPERCASE CLI tokens, machine states lowercase. PID names are not
 * repeated here: LIST PIDS JSON is the catalog.
 *
 * Pure: plain snapshot structs in, JsonObject/JsonArray out. No HAL, no
 * messaging, no globals, so the native tests include the .cpp directly. The
 * CLI glue (command_table.cpp / cmd_bus.cpp) gathers the snapshots.
 */

#ifndef CAN_STATUS_JSON_H
#define CAN_STATUS_JSON_H

#include <stdint.h>
#include <ArduinoJson.h>

// ---- SCAN STATUS JSON ------------------------------------------------------
enum CanScanJsonState : uint8_t { CAN_SCAN_JSON_IDLE, CAN_SCAN_JSON_LISTENING, CAN_SCAN_JSON_DONE };

struct CanScanSnapshot {
    CanScanJsonState state;
    uint8_t  scanId;        // increments per SCAN CAN start (0 = never scanned)
    uint16_t durationMs;
    uint32_t remainingMs;   // 0 unless listening
    uint8_t  found;
};

void canScanStatusToJson(const CanScanSnapshot& s, JsonObject out);

// ---- SCAN RESULTS JSON (one element) ----------------------------------------
struct CanScanEntrySnapshot {
    uint16_t canId;
    uint8_t  pid;           // OBD PID, or first data byte for other frames
    uint8_t  dataLength;
    uint32_t frames;
    uint32_t ageMs;
    const uint8_t* data;    // dataLength bytes
};

void canScanEntryToJson(const CanScanEntrySnapshot& e, JsonObject out);

// ---- BUS CAN JSON ----------------------------------------------------------
struct CanBusSnapshot {
    // caps
    uint8_t  buses;
    uint8_t  maxScanResults;
    // input
    uint8_t  inBus;             // 255 = none
    uint8_t  inMode;            // CanInputMode: 0 OFF, 1 NORMAL, 2 LISTEN
    uint32_t inBaudrate;
    bool     inRunning;
    bool     inShared;
    bool     rebootPending;     // bus/mode/baudrate differ from what is running
    uint32_t framesRx;
    bool     haveFrame;         // false: lastFrameAgeMs is null
    uint32_t lastFrameAgeMs;
    bool     pollActive;
    bool     pollBackoff;
    uint8_t  missStreak;
    uint32_t requests;
    uint32_t answers;
    uint32_t txFail;
    // output
    uint8_t  outBus;            // 255 = none
    bool     outEnabled;        // buses.can_output_enabled
    bool     outModule;         // outputEnabled[OUTPUT_CAN]
    uint32_t outBaudrate;
};

struct CanImportSnapshot {
    uint8_t  pin;
    const char* pinLabel;
    uint16_t sourceCanId;
    uint8_t  sourcePid;
    bool     polled;
    bool     haveFrame;         // false: ageMs is null
    uint32_t ageMs;
};

// Writes caps/input/output; the caller appends one object per import to the
// returned "imports" array with canImportToJson().
JsonArray canBusStatusToJson(const CanBusSnapshot& s, JsonObject out);
void canImportToJson(const CanImportSnapshot& i, JsonObject out);

#endif // CAN_STATUS_JSON_H
