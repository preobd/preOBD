/*
 * sd_status_json.h - SD card and logging status for clients (#536)
 *
 *   SYSTEM SD          one line of text
 *   SYSTEM SD JSON     {"sd":{"state":...}} — the app's System tab polls it
 *
 * Answers "is my drive being logged?", which SYSTEM DUMP JSON cannot: the dump
 * says whether the SD output is ENABLED (config), this says whether it is
 * WORKING (status). Kept out of the dump on purpose, because the dump
 * round-trips through JSON IMPORT and profiles, and runtime state must not.
 *
 * Read-only and cheap: everything here is state the logger and sd_manager
 * already hold. Building a snapshot never touches the card (no media probe —
 * on the SPI paths that can be a seconds-long remount), so a removal is seen
 * by the logger's own pre-write probe, i.e. only while logging.
 *
 * Pure: snapshot struct in, state / JsonObject out. The native test includes
 * the .cpp directly; output_sdlog.cpp gathers the snapshot.
 */

#ifndef SD_STATUS_JSON_H
#define SD_STATUS_JSON_H

#include <stdint.h>

enum SdStatusState : uint8_t {
    SD_STATE_NONE,      // this build has no SD logging (no slot, or compiled out)
    SD_STATE_NOCARD,    // no card mounted at boot (cards are only mounted at boot)
    SD_STATE_REMOVED,   // card pulled since boot (Teensy only: other platforms have
                        // no probe, so a pulled card ends as ERROR)
    SD_STATE_OFF,       // SD logging output disabled
    SD_STATE_ERROR,     // logging stopped: write errors (full or failing card), or
                        // no log file could be created
    SD_STATE_IDLE,      // enabled but not writing: CONFIG mode, or no inputs routed
    SD_STATE_LOGGING,   // writing rows
};

struct SdStatusSnapshot {
    bool        supported;      // false: no SD logging in this build (zero the rest)
    bool        mounted;        // isSDInitialized()
    bool        enabled;        // outputEnabled[OUTPUT_SD]
    bool        configMode;     // the logger pauses in CONFIG (#268)
    bool        cardRemoved;    // sdLogCardRemoved()
    bool        faulted;        // sdLogIsFaulted()
    bool        fileOpen;       // sdLogIsActive()
    const char* fileName;       // nullptr when no file has been opened
    uint32_t    rows;
    uint16_t    errors;
    bool        capacityKnown;  // false: no card, or no capacity call on this platform
    uint64_t    freeBytes;      // at mount, minus what the logger wrote since
    uint64_t    totalBytes;
};

// Most specific first: a card that is gone or never mounted says so whatever
// the output's setting, and an error outranks the CONFIG pause, because the
// logger stays stopped after RUN resumes.
inline SdStatusState sdStatusState(const SdStatusSnapshot& s) {
    if (!s.supported)               return SD_STATE_NONE;
    if (!s.mounted)                 return SD_STATE_NOCARD;
    if (s.cardRemoved)              return SD_STATE_REMOVED;
    if (!s.enabled)                 return SD_STATE_OFF;
    if (s.faulted)                  return SD_STATE_ERROR;
    if (s.configMode || !s.fileOpen) return SD_STATE_IDLE;
    return SD_STATE_LOGGING;
}

const char* sdStatusStateName(SdStatusState state);

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)
#include <ArduinoJson.h>
// Fills out["sd"]. Sizes are decimal megabytes (10^6), as printed on the card,
// and null when unknown or when there is no card to measure.
void sdStatusToJson(const SdStatusSnapshot& s, JsonObject out);
#endif

#endif // SD_STATUS_JSON_H
