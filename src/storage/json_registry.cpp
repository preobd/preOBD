/*
 * json_registry.cpp - JSON export of firmware static catalogs
 *
 * Only compiled on platforms with SUPPORTS_JSON_EXPORT (Teensy 3.x/4.x, ESP32).
 */

#include "../config.h"

#include "json_registry.h"
#include "../pinctrl/board_pins.h"   // boardName(): firmware.platform (#570)
#include "../core/platform.h"

#if SUPPORTS_JSON_EXPORT

#include <ArduinoJson.h>
#include "../core/watchdog.h"

// Serial-driven exports, never a hot path. Teensy 4.x places code in fast
// ITCM (RAM1, scarce) by default; keep these writers in FLASH instead.
// FLASHMEM is a Teensy core macro — no-op elsewhere.
#ifndef FLASHMEM
#define FLASHMEM
#endif
#include "../sensors/sensor_library.h"
#include "../sensors/application_presets.h"
#include "../sensors/units_registry.h"
#include "../sensors/sensor_library/sensor_categories.h"
#include "../sensors/sensor_library/sensor_helpers.h"
#include "../sensors/can_sensor_library/standard_pids.h"
#include "../outputs/output_base.h"
#include "system_config.h"
#include "../version.h"

// ===== ENUM STRING TABLES =====
// Single source of truth — used both for name→string lookup and for JSON array export.

struct MeasTypeEntry  { MeasurementType id; const char* name; };
struct CalTypeEntry   { CalibrationType id; const char* name; };

static const MeasTypeEntry MEAS_TYPE_TABLE[] = {
    {MEASURE_TEMPERATURE, "TEMPERATURE"},
    {MEASURE_PRESSURE,    "PRESSURE"},
    {MEASURE_VOLTAGE,     "VOLTAGE"},
    {MEASURE_RPM,         "RPM"},
    {MEASURE_HUMIDITY,    "HUMIDITY"},
    {MEASURE_ELEVATION,   "ELEVATION"},
    {MEASURE_DIGITAL,     "DIGITAL"},
    {MEASURE_SPEED,       "SPEED"},
    {MEASURE_PERCENT,     "PERCENT"},
    {MEASURE_DISTANCE,    "DISTANCE"},
    {MEASURE_TIME,        "TIME"},
    {MEASURE_MASS_FLOW,   "MASS_FLOW"},
    {MEASURE_FUEL_RATE,   "FUEL_RATE"},
};
static const uint8_t MEAS_TYPE_COUNT = sizeof(MEAS_TYPE_TABLE) / sizeof(MEAS_TYPE_TABLE[0]);

static const CalTypeEntry CAL_TYPE_TABLE[] = {
    {CAL_NONE,                 "CAL_NONE"},
    {CAL_THERMISTOR_STEINHART, "CAL_THERMISTOR_STEINHART"},
    {CAL_THERMISTOR_TABLE,     "CAL_THERMISTOR_TABLE"},
    {CAL_THERMISTOR_BETA,      "CAL_THERMISTOR_BETA"},
    {CAL_PRESSURE_POLYNOMIAL,  "CAL_PRESSURE_POLYNOMIAL"},
    {CAL_PRESSURE_TABLE,       "CAL_PRESSURE_TABLE"},
    {CAL_LINEAR,               "CAL_LINEAR"},
    {CAL_VOLTAGE_DIVIDER,      "CAL_VOLTAGE_DIVIDER"},
    {CAL_RPM,                  "CAL_RPM"},
    {CAL_SPEED,                "CAL_SPEED"},
    {CAL_CAN_IMPORT,           "CAL_CAN_IMPORT"},
    {CAL_LEVEL_TABLE,          "CAL_LEVEL_TABLE"},
};
static const uint8_t CAL_TYPE_COUNT = sizeof(CAL_TYPE_TABLE) / sizeof(CAL_TYPE_TABLE[0]);

FLASHMEM const char* jsonMeasurementTypeName(MeasurementType t) {
    for (uint8_t i = 0; i < MEAS_TYPE_COUNT; i++) {
        if (MEAS_TYPE_TABLE[i].id == t) return MEAS_TYPE_TABLE[i].name;
    }
    return "UNKNOWN";
}

static FLASHMEM const char* calibrationTypeName(CalibrationType t) {
    for (uint8_t i = 0; i < CAL_TYPE_COUNT; i++) {
        if (CAL_TYPE_TABLE[i].id == t) return CAL_TYPE_TABLE[i].name;
    }
    return "UNKNOWN";
}

static FLASHMEM const char* pinTypeName(PinTypeRequirement p) {
    switch (p) {
        case PIN_ANALOG:  return "ANALOG";
        case PIN_DIGITAL: return "DIGITAL";
        case PIN_I2C:     return "I2C";
        default:          return "UNKNOWN";
    }
}

// ===== ENUM REGISTRY EXPORTS =====

FLASHMEM void writeMeasurementTypesJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < MEAS_TYPE_COUNT; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]   = (int)MEAS_TYPE_TABLE[i].id;
        obj["name"] = MEAS_TYPE_TABLE[i].name;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

FLASHMEM void writeCalibrationTypesJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < CAL_TYPE_COUNT; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]   = (int)CAL_TYPE_TABLE[i].id;
        obj["name"] = CAL_TYPE_TABLE[i].name;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

// ===== REGISTRY EXPORTS =====

FLASHMEM void writeUnitsJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        UnitsInfo u;
        memcpy_P(&u, &UNITS_REGISTRY[i], sizeof(u));
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]            = (const __FlashStringHelper*)u.name;
        obj["alias"]           = (const __FlashStringHelper*)u.alias;
        obj["symbol"]          = (const __FlashStringHelper*)u.symbol;
        obj["measurementType"] = jsonMeasurementTypeName(u.measurementType);
        obj["factor"]          = u.conversionFactor;
        obj["offset"]          = u.conversionOffset;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

FLASHMEM void writeCategoriesJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < CAT_COUNT; i++) {
        SensorCategoryInfo c;
        memcpy_P(&c, &SENSOR_CATEGORIES[i], sizeof(c));
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]  = (const __FlashStringHelper*)c.name;
        obj["label"] = (const __FlashStringHelper*)c.label;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

// filter: optional category name string (e.g. "THERMOCOUPLE"); nullptr = all sensors
FLASHMEM void writeSensorsJson(Print& out, const char* filter) {
    // Resolve filter to a category enum up-front; CAT_COUNT means "no filter"
    SensorCategory filterCat = CAT_COUNT;
    if (filter) {
        SensorCategory c = getCategoryByName(filter);
        if (c < CAT_COUNT) filterCat = c;
        // Unknown filter name: return empty array rather than silently dumping all
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {  // skip index 0 (SENSOR_NONE)
        SensorInfo s;
        memcpy_P(&s, &SENSOR_LIBRARY[i], sizeof(s));
        if (s.label == nullptr) continue;  // skip unimplemented slots
        if (filterCat < CAT_COUNT && getSensorCategory(i) != filterCat) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]     = (const __FlashStringHelper*)s.name;
        obj["label"]    = (const __FlashStringHelper*)s.label;
        obj["nameHash"] = s.nameHash;
        if (s.description) {
            obj["description"] = (const __FlashStringHelper*)s.description;
        } else {
            obj["description"] = nullptr;
        }
        obj["measurementType"] = jsonMeasurementTypeName(s.measurementType);
        obj["calibrationType"] = calibrationTypeName(s.calibrationType);
        obj["minReadInterval"] = s.minReadInterval;
        obj["minValue"]        = s.minValue;
        obj["maxValue"]        = s.maxValue;
        obj["pinType"]         = pinTypeName(s.pinTypeRequirement);
        SensorCategory cat = getSensorCategory(i);
        const SensorCategoryInfo* catInfo = getCategoryInfo(cat);
        obj["category"] = (const __FlashStringHelper*)READ_CATEGORY_NAME(catInfo);
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

// Car-area group names for the "group" export field. Clients bucket unknown
// names under OTHER, so growing AppGroup only requires extending this switch.
static FLASHMEM const char* appGroupName(AppGroup g) {
    switch (g) {
        case APP_GROUP_ENGINE:      return "ENGINE";
        case APP_GROUP_DRIVETRAIN:  return "DRIVETRAIN";
        case APP_GROUP_INDUCTION:   return "INDUCTION";
        case APP_GROUP_EXHAUST:     return "EXHAUST";
        case APP_GROUP_ELECTRICAL:  return "ELECTRICAL";
        case APP_GROUP_ENVIRONMENT: return "ENVIRONMENT";
        default:                    return "OTHER";
    }
}

FLASHMEM void writeApplicationsJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {  // skip index 0 (APP_NONE)
        ApplicationPreset p;
        memcpy_P(&p, &APPLICATION_PRESETS[i], sizeof(p));
        if (p.label == nullptr) continue;  // skip unimplemented slots
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]         = (const __FlashStringHelper*)p.name;
        obj["nameHash"]     = p.nameHash;
        obj["abbreviation"] = (const __FlashStringHelper*)p.abbreviation;
        obj["label"]        = (const __FlashStringHelper*)p.label;
        obj["group"]        = appGroupName(p.group);
        if (p.description) {
            obj["description"] = (const __FlashStringHelper*)p.description;
        } else {
            obj["description"] = nullptr;
        }
        // Cross-references by name string (not index), matching hash-based registry design
        const SensorInfo* sensor = getSensorInfo(p.defaultSensor);
        if (sensor) {
            obj["defaultSensor"] = (const __FlashStringHelper*)READ_SENSOR_NAME(sensor);
        } else {
            obj["defaultSensor"] = nullptr;
        }
        if (p.defaultUnits < NUM_UNITS) {
            UnitsInfo u;
            memcpy_P(&u, &UNITS_REGISTRY[p.defaultUnits], sizeof(u));
            obj["defaultUnits"] = (const __FlashStringHelper*)u.name;
        } else {
            obj["defaultUnits"] = nullptr;
        }
        obj["defaultMinValue"] = p.defaultMinValue;
        obj["defaultMaxValue"] = p.defaultMaxValue;
        // Gates whether SET APPLICATION auto-seeds an alarm Rule (rules_manager
        // buildRuleConfigFromApplication). The editor mirrors this to pre-fill
        // the alarm band only when the app actually defaults to an alarm (#295).
        obj["defaultAlarmEnabled"] = p.defaultAlarmEnabled;
        obj["obd2pid"]         = p.obd2pid;
        obj["obd2length"]      = p.obd2length;
        obj["measurementType"] = jsonMeasurementTypeName(p.expectedMeasurementType);
        obj["warmupMs"]        = p.warmupTime_ms;
        obj["persistMs"]       = p.persistTime_ms;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

FLASHMEM void writeOutputsJson(Print& out) {
    extern OutputModule outputModules[];
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (outputModules[i].name == nullptr) continue;  // placeholder/disabled slot
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]       = outputModules[i].name;
        obj["enabled"]    = outputModules[i].enabled;
        obj["intervalMs"] = outputModules[i].sendInterval;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

FLASHMEM void writePidsJson(Print& out) {
    const uint8_t count = NUM_STANDARD_PIDS;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        StandardPIDInfo pid;
        memcpy_P(&pid, &STANDARD_PID_TABLE[i], sizeof(pid));
        JsonObject obj = arr.add<JsonObject>();
        obj["pid"]             = pid.pid;
        obj["name"]            = (const __FlashStringHelper*)pid.name;
        obj["abbr"]            = (const __FlashStringHelper*)pid.abbr;
        obj["dataOffset"]      = pid.data_offset;   // first value byte from A (#509)
        obj["dataLength"]      = pid.data_length;
        obj["measurementType"] = jsonMeasurementTypeName(pid.measurementType);
        obj["scale"]           = pid.scale_factor;
        obj["offset"]          = pid.offset;
        obj["units"]           = (const __FlashStringHelper*)pid.units;
        // Multi-sensor PIDs (#535): the value's bit in byte A, and sensors
        // 2+ (SET CAN <pid> SENSOR <n>)
        if (pid.support_bit != NO_SUPPORT_BIT) obj["supportBit"] = pid.support_bit;
        JsonArray sensors;
        for (uint8_t j = 0; j < NUM_STANDARD_PID_SENSORS; j++) {
            StandardPIDSensor s;
            memcpy_P(&s, &STANDARD_PID_SENSORS[j], sizeof(s));
            if (s.pid != pid.pid) continue;
            if (sensors.isNull()) sensors = obj["sensors"].to<JsonArray>();
            JsonObject so = sensors.add<JsonObject>();
            so["sensor"]     = s.sensor;
            so["name"]       = (const __FlashStringHelper*)s.name;
            so["abbr"]       = (const __FlashStringHelper*)s.abbr;
            so["dataOffset"] = s.data_offset;
            so["supportBit"] = s.support_bit;
        }
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

// ===== BUNDLED EXPORT =====

// This function's capability list is mirrored in json_config.cpp's
// dumpConfigToJSON() (SYSTEM DUMP JSON's own "firmware" object) — duplicated
// rather than shared since that file is SUPPORTS_JSON_CONFIG-gated and this
// one is SUPPORTS_JSON_EXPORT-gated. Keep both lists in sync.
static FLASHMEM void writeFirmwareJson(Print& out) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["version"]    = firmwareVersionString();
    obj["major"]      = FW_MAJOR;
    obj["minor"]      = FW_MINOR;
    obj["patch"]      = FW_PATCH;
    obj["prerelease"] = FW_PRERELEASE;
    obj["gitHash"]    = FW_GIT_HASH;
    obj["platform"]   = boardName();
    JsonArray caps = obj["capabilities"].to<JsonArray>();
#if SUPPORTS_JSON_IMPORT_STREAM
    caps.add("json_import");
#endif
    caps.add("json_export");
#if INCLUDE_CAN
    caps.add("can");
#endif
#if INCLUDE_SD_LOGGING
    caps.add("sd");
#endif
#if INCLUDE_LCD
    caps.add("lcd");
#endif
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

FLASHMEM void dumpRegistryToJson(Print& out) {
    out.print(F("{\"schemaVersion\":1,\"kind\":\"registry\","));
    out.print(F("\"firmware\":"));          writeFirmwareJson(out);
    out.print(F(",\"measurementTypes\":")); writeMeasurementTypesJson(out);
    out.print(F(",\"calibrationTypes\":")); writeCalibrationTypesJson(out);
    out.print(F(",\"units\":"));            writeUnitsJson(out);
    out.print(F(",\"categories\":"));       writeCategoriesJson(out);
    out.print(F(",\"sensors\":"));          writeSensorsJson(out);
    out.print(F(",\"applications\":"));     writeApplicationsJson(out);
    out.print(F(",\"outputs\":"));          writeOutputsJson(out);
    out.print(F(",\"standardPids\":"));     writePidsJson(out);
    out.println('}');
}

#endif // SUPPORTS_JSON_EXPORT
