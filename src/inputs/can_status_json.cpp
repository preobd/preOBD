/*
 * can_status_json.cpp - see can_status_json.h
 */

#include <Arduino.h>   // FLASHMEM (Teensy); before the fallback below

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)

#include "can_status_json.h"
#include "can_poll.h"   // isObdResponseId

#ifndef FLASHMEM
#define FLASHMEM
#endif

static const char* const SCAN_STATE_NAMES[] = {"idle", "listening", "done"};
static const char* const INPUT_MODE_NAMES[] = {"OFF", "NORMAL", "LISTEN"};
static const uint32_t CAN_BAUDRATES[] = {125000, 250000, 500000, 1000000};

FLASHMEM void canScanStatusToJson(const CanScanSnapshot& s, JsonObject out) {
    out["state"] = SCAN_STATE_NAMES[s.state <= CAN_SCAN_JSON_DONE ? s.state : 0];
    out["scanId"] = s.scanId;
    out["durationMs"] = s.durationMs;
    out["remainingMs"] = s.remainingMs;
    out["found"] = s.found;
    // The scan only listens: it reports what is on the bus, and requests
    // nothing itself (active discovery is #511).
    out["passive"] = true;
}

FLASHMEM void canScanEntryToJson(const CanScanEntrySnapshot& e, JsonObject out) {
    out["canId"] = e.canId;
    out["pid"] = e.pid;
    out["obd"] = isObdResponseId(e.canId);
    out["dataLength"] = e.dataLength;
    out["frames"] = e.frames;
    out["ageMs"] = e.ageMs;
    char hex[17];
    static const char digits[] = "0123456789ABCDEF";
    uint8_t n = e.dataLength > 8 ? 8 : e.dataLength;
    for (uint8_t i = 0; i < n; i++) {
        hex[2 * i]     = digits[e.data[i] >> 4];
        hex[2 * i + 1] = digits[e.data[i] & 0x0F];
    }
    hex[2 * n] = '\0';
    out["data"] = hex;   // copied: ArduinoJson duplicates a char* value
}

FLASHMEM JsonArray canBusStatusToJson(const CanBusSnapshot& s, JsonObject out) {
    JsonObject caps = out["caps"].to<JsonObject>();
    caps["buses"] = s.buses;
    JsonArray rates = caps["baudrates"].to<JsonArray>();
    for (uint32_t r : CAN_BAUDRATES) rates.add(r);
    caps["listenShared"] = false;   // BUS refuses a LISTEN input on the output's bus
    caps["maxScanResults"] = s.maxScanResults;
    JsonArray scanMs = caps["scanMs"].to<JsonArray>();
    scanMs.add(1000);
    scanMs.add(60000);

    JsonObject in = out["input"].to<JsonObject>();
    in["bus"] = s.inBus;
    in["mode"] = INPUT_MODE_NAMES[s.inMode <= 2 ? s.inMode : 0];
    in["baudrate"] = s.inBaudrate;
    in["running"] = s.inRunning;
    in["sharedWithOutput"] = s.inShared;
    // bus/mode/baudrate are the configured values (as in SYSTEM DUMP JSON);
    // BUS CAN INPUT takes effect on reboot, so they can run ahead of the
    // hardware until then.
    in["rebootPending"] = s.rebootPending;
    in["framesRx"] = s.framesRx;
    if (s.haveFrame) in["lastFrameAgeMs"] = s.lastFrameAgeMs;
    else             in["lastFrameAgeMs"] = nullptr;
    JsonObject poll = in["poll"].to<JsonObject>();
    poll["active"] = s.pollActive;
    poll["backoff"] = s.pollBackoff;
    poll["missStreak"] = s.missStreak;
    poll["requests"] = s.requests;
    poll["answers"] = s.answers;
    poll["txFail"] = s.txFail;

    JsonObject o = out["output"].to<JsonObject>();
    o["bus"] = s.outBus;
    o["enabled"] = s.outEnabled;
    o["module"] = s.outModule;
    o["baudrate"] = s.outBaudrate;

    return out["imports"].to<JsonArray>();
}

FLASHMEM void canImportToJson(const CanImportSnapshot& i, JsonObject out) {
    out["pin"] = i.pin;
    out["pinLabel"] = i.pinLabel;
    out["sourceCanId"] = i.sourceCanId;
    out["sourcePid"] = i.sourcePid;
    out["polled"] = i.polled;
    if (i.haveFrame) out["ageMs"] = i.ageMs;
    else             out["ageMs"] = nullptr;
}

#endif // SUPPORTS_JSON_EXPORT || NATIVE_TEST
