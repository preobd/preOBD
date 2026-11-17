/*
 * output_table.cpp - The outputs you add, as a view over systemConfig
 * (#612 phase 1). See output_table.h for why ids are fixed per format.
 *
 * Dependency-light on purpose (system_config.h only): the native tests
 * build it on its own.
 */

#include "output_table.h"
#include <string.h>
#ifndef __AVR__
#include <strings.h>   // strcasecmp (AVR has it in string.h)
#endif

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// Per id: format, transport kind, legacy slot. Indexed by id.
struct OutputIdInfo {
    OutputFormat format;
    OutputTransportKind kind;
    uint8_t legacySlot;
};
static const OutputIdInfo ID_INFO[NUM_OUTPUT_IDS] = {
    { FMT_OBD,      OTK_CAN,    OUTPUT_CAN      },
    { FMT_REALDASH, OTK_STREAM, OUTPUT_REALDASH },
    { FMT_CSV,      OTK_STREAM, OUTPUT_SERIAL   },
    { FMT_CSV,      OTK_SD,     OUTPUT_SD       },
    { FMT_ELM327,   OTK_SERIAL, OUTPUT_ELM327   },
};

OutputFormat outputFormatOf(uint8_t id) {
    return id < NUM_OUTPUT_IDS ? ID_INFO[id].format : FMT_NONE;
}

OutputTransportKind outputKindOf(uint8_t id) {
    return id < NUM_OUTPUT_IDS ? ID_INFO[id].kind : OTK_STREAM;
}

uint8_t outputLegacySlot(uint8_t id) {
    return id < NUM_OUTPUT_IDS ? ID_INFO[id].legacySlot : 0xFF;
}

bool outputIdBuilt(uint8_t id) {
    switch (id) {
#if INCLUDE_CAN
    case OUT_ID_OBD:      return true;
#endif
#if INCLUDE_REALDASH
    case OUT_ID_REALDASH: return true;
#endif
#if INCLUDE_SERIAL_OUTPUT
    case OUT_ID_CSV:      return true;
#endif
#if INCLUDE_SD_LOGGING
    case OUT_ID_SDLOG:    return true;
#endif
#if INCLUDE_ELM327
    case OUT_ID_ELM327:   return true;
#endif
    default:              return false;
    }
}

bool outputTakesIntervalId(uint8_t id) {
    return id < NUM_OUTPUT_IDS && outputTakesInterval(ID_INFO[id].legacySlot);
}

uint16_t outputIntervalMinId(uint8_t id) {
    return id < NUM_OUTPUT_IDS ? outputIntervalMinMs(ID_INFO[id].legacySlot) : 0;
}

bool outputExists(uint8_t id) {
    return outputIdBuilt(id) && !(systemConfig.router.outputsAbsent & (1u << id));
}

bool outputIsEnabled(uint8_t id) {
    return outputExists(id) && systemConfig.outputEnabled[ID_INFO[id].legacySlot] != 0;
}

uint16_t outputIntervalOf(uint8_t id) {
    return id < NUM_OUTPUT_IDS ? systemConfig.outputInterval[ID_INFO[id].legacySlot] : 0;
}

uint8_t outputCount() {
    uint8_t n = 0;
    for (uint8_t id = 0; id < NUM_OUTPUT_IDS; id++)
        if (outputExists(id)) n++;
    return n;
}

RouteMask outputsExistingMask() {
    RouteMask m = 0;
    for (uint8_t id = 0; id < NUM_OUTPUT_IDS; id++)
        if (outputExists(id)) m |= (RouteMask)(1u << id);
    return m;
}

uint8_t outputIdFor(OutputFormat format, OutputTransportKind kind) {
    for (uint8_t id = 0; id < NUM_OUTPUT_IDS; id++) {
        if (ID_INFO[id].format != format) continue;
        // CSV is the one format with two ids; the others match on format alone
        if (format == FMT_CSV && ID_INFO[id].kind != kind) continue;
        return id;
    }
    return OUT_ID_NONE;
}

void outputMarkAbsent(uint8_t id, bool absent) {
    if (id >= NUM_OUTPUT_IDS) return;
    if (absent) systemConfig.router.outputsAbsent |= (uint8_t)(1u << id);
    else        systemConfig.router.outputsAbsent &= (uint8_t)~(1u << id);
}

void outputsReconcileAbsent() {
    for (uint8_t id = 0; id < NUM_OUTPUT_IDS; id++) {
        if ((systemConfig.router.outputsAbsent & (1u << id)) &&
            systemConfig.outputEnabled[ID_INFO[id].legacySlot]) {
            outputMarkAbsent(id, false);
        }
    }
}

bool outputTransportKindOk(uint8_t id, const char* tok) {
    if (tok == nullptr || id >= NUM_OUTPUT_IDS) return false;
    const bool none = strcasecmp(tok, "NONE") == 0;
    const bool can  = strncasecmp(tok, "CAN", 3) == 0 && tok[3] >= '1' && tok[3] <= '3' && tok[4] == '\0';
    const bool sd   = strcasecmp(tok, "SD") == 0;
    const bool ser  = strncasecmp(tok, "SERIAL", 6) == 0 && tok[6] >= '1' && tok[6] <= '8' && tok[7] == '\0';
    switch (ID_INFO[id].kind) {
    case OTK_CAN:    return can || none;
    case OTK_SERIAL: return ser || none;
    case OTK_SD:     return sd;
    case OTK_STREAM: return tok[0] != '\0' && !can && !sd && !none;
    }
    return false;
}

const char* outputFormatName(OutputFormat format) {
    switch (format) {
    case FMT_CSV:      return "CSV";
    case FMT_REALDASH: return "REALDASH";
    case FMT_OBD:      return "OBD";
    case FMT_ELM327:   return "ELM327";
    default:           return "NONE";
    }
}

OutputFormat parseOutputFormat(const char* s) {
    if (s == nullptr) return FMT_NONE;
    if (strcasecmp(s, "CSV") == 0)      return FMT_CSV;
    if (strcasecmp(s, "REALDASH") == 0) return FMT_REALDASH;
    if (strcasecmp(s, "OBD") == 0)      return FMT_OBD;
    if (strcasecmp(s, "ELM327") == 0)   return FMT_ELM327;
    return FMT_NONE;
}

RouteMask routeMaskFromLegacy(uint8_t legacyMask) {
    RouteMask m = (RouteMask)(legacyMask & 0x0F);
    if (legacyMask & (1u << OUT_ID_OBD)) m |= (RouteMask)(1u << OUT_ID_ELM327);
    return m;
}
