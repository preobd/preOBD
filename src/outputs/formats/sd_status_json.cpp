/*
 * sd_status_json.cpp - see sd_status_json.h
 */

#include <Arduino.h>   // FLASHMEM (Teensy); before the fallback below

#include "sd_status_json.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif

FLASHMEM const char* sdStatusStateName(SdStatusState state) {
    switch (state) {
        case SD_STATE_NONE:    return "none";
        case SD_STATE_NOCARD:  return "nocard";
        case SD_STATE_REMOVED: return "removed";
        case SD_STATE_OFF:     return "off";
        case SD_STATE_ERROR:   return "error";
        case SD_STATE_IDLE:    return "idle";
        case SD_STATE_LOGGING: return "logging";
    }
    return "none";
}

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)

FLASHMEM void sdStatusToJson(const SdStatusSnapshot& s, JsonObject out) {
    const SdStatusState state = sdStatusState(s);
    JsonObject sd = out["sd"].to<JsonObject>();
    sd["state"] = sdStatusStateName(state);
    if (s.fileName && s.fileName[0]) sd["file"] = s.fileName;
    else                             sd["file"] = nullptr;
    sd["rows"] = s.rows;
    sd["errors"] = s.errors;
    // A pulled card's mount-time figure describes a card that is not there.
    const bool sized = s.capacityKnown && state != SD_STATE_NONE &&
                       state != SD_STATE_NOCARD && state != SD_STATE_REMOVED;
    if (sized) {
        sd["freeMB"] = (uint32_t)(s.freeBytes / 1000000ULL);
        sd["totalMB"] = (uint32_t)(s.totalBytes / 1000000ULL);
    } else {
        sd["freeMB"] = nullptr;
        sd["totalMB"] = nullptr;
    }
}

#endif
