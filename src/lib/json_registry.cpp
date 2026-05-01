/*
 * json_registry.cpp - JSON export of firmware static catalogs
 *
 * Only compiled on platforms with SUPPORTS_JSON_EXPORT (Teensy 3.x/4.x, ESP32).
 */

#include "../config.h"

#include "json_registry.h"
#include "platform.h"

#if SUPPORTS_JSON_EXPORT

#include <ArduinoJson.h>
#include "watchdog.h"
#include "sensor_library.h"
#include "application_presets.h"
#include "units_registry.h"
#include "sensor_library/sensor_categories.h"
#include "sensor_library/sensor_helpers.h"
#include "can_sensor_library/standard_pids.h"
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
    {MEASURE_LEVEL,       "LEVEL"},
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

static const char* measurementTypeName(MeasurementType t) {
    for (uint8_t i = 0; i < MEAS_TYPE_COUNT; i++) {
        if (MEAS_TYPE_TABLE[i].id == t) return MEAS_TYPE_TABLE[i].name;
    }
    return "UNKNOWN";
}

static const char* calibrationTypeName(CalibrationType t) {
    for (uint8_t i = 0; i < CAL_TYPE_COUNT; i++) {
        if (CAL_TYPE_TABLE[i].id == t) return CAL_TYPE_TABLE[i].name;
    }
    return "UNKNOWN";
}

static const char* pinTypeName(PinTypeRequirement p) {
    switch (p) {
        case PIN_ANALOG:  return "ANALOG";
        case PIN_DIGITAL: return "DIGITAL";
        case PIN_I2C:     return "I2C";
        default:          return "UNKNOWN";
    }
}

static const char* getPlatformStr() {
#if defined(TEENSY_41)
    return "TEENSY41";
#elif defined(TEENSY_40)
    return "TEENSY40";
#elif defined(TEENSY_36)
    return "TEENSY36";
#elif defined(TEENSY_35)
    return "TEENSY35";
#elif defined(TEENSY_32)
    return "TEENSY32";
#elif defined(TEENSY_31)
    return "TEENSY31";
#elif defined(ESP32)
    return "ESP32";
#else
    return "UNKNOWN";
#endif
}

// ===== ENUM REGISTRY EXPORTS =====

void writeMeasurementTypesJson(Print& out) {
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

void writeCalibrationTypesJson(Print& out) {
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

void writeUnitsJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        UnitsInfo u;
        memcpy_P(&u, &UNITS_REGISTRY[i], sizeof(u));
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]            = (const __FlashStringHelper*)u.name;
        obj["alias"]           = (const __FlashStringHelper*)u.alias;
        obj["symbol"]          = (const __FlashStringHelper*)u.symbol;
        obj["measurementType"] = measurementTypeName(u.measurementType);
        obj["factor"]          = u.conversionFactor;
        obj["offset"]          = u.conversionOffset;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

void writeCategoriesJson(Print& out) {
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
void writeSensorsJson(Print& out, const char* filter) {
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
        obj["measurementType"] = measurementTypeName(s.measurementType);
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

void writeApplicationsJson(Print& out) {
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
        obj["obd2pid"]         = p.obd2pid;
        obj["obd2length"]      = p.obd2length;
        obj["measurementType"] = measurementTypeName(p.expectedMeasurementType);
        obj["warmupMs"]        = p.warmupTime_ms;
        obj["persistMs"]       = p.persistTime_ms;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

void writeOutputsJson(Print& out) {
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

void writePidsJson(Print& out) {
    const uint8_t count = sizeof(STANDARD_PID_TABLE) / sizeof(STANDARD_PID_TABLE[0]);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        StandardPIDInfo pid;
        memcpy_P(&pid, &STANDARD_PID_TABLE[i], sizeof(pid));
        JsonObject obj = arr.add<JsonObject>();
        obj["pid"]             = pid.pid;
        obj["name"]            = (const __FlashStringHelper*)pid.name;
        obj["abbr"]            = (const __FlashStringHelper*)pid.abbr;
        obj["dataLength"]      = pid.data_length;
        obj["measurementType"] = measurementTypeName(pid.measurementType);
        obj["scale"]           = pid.scale_factor;
        obj["offset"]          = pid.offset;
        obj["units"]           = (const __FlashStringHelper*)pid.units;
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

// ===== BUNDLED EXPORT =====

static void writeFirmwareJson(Print& out) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["version"]    = firmwareVersionString();
    obj["major"]      = FW_MAJOR;
    obj["minor"]      = FW_MINOR;
    obj["patch"]      = FW_PATCH;
    obj["prerelease"] = FW_PRERELEASE;
    obj["gitHash"]    = FW_GIT_HASH;
    obj["platform"]   = getPlatformStr();
    JsonArray caps = obj["capabilities"].to<JsonArray>();
#if SUPPORTS_JSON_IMPORT_STREAM
    caps.add("json_import");
#endif
    caps.add("json_export");
#if ENABLE_CAN
    caps.add("can");
#endif
#if ENABLE_SD_LOGGING
    caps.add("sd");
#endif
#if ENABLE_LCD
    caps.add("lcd");
#endif
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}

void dumpRegistryToJson(Print& out) {
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
