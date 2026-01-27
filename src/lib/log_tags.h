/*
 * log_tags.h - Tag definitions for structured logging
 *
 * Defines tag IDs and string constants for categorizing log messages.
 * Tags allow runtime filtering of log output by subsystem.
 *
 * Usage:
 *   LOG_INFO(TAG_SD, "Card initialized");
 *   msg.debug.error(TAG_BME280, "Sensor not found");
 */

#ifndef LOG_TAGS_H
#define LOG_TAGS_H

#include <Arduino.h>

// Tag ID enumeration (used for bitmap filtering)
// Use subsystem-level tags, not device-specific tags
// e.g., TAG_SENSOR (not TAG_BME280), TAG_DISPLAY (not TAG_LCD)
enum LogTag {
    TAG_ID_SD = 0,
    TAG_ID_BT = 1,
    TAG_ID_CAN = 2,
    TAG_ID_ADC = 3,
    TAG_ID_SENSOR = 4,
    TAG_ID_CONFIG = 5,
    TAG_ID_ALARM = 6,
    TAG_ID_DISPLAY = 7,
    TAG_ID_ROUTER = 8,
    TAG_ID_SERIAL = 9,
    TAG_ID_I2C = 10,
    TAG_ID_SPI = 11,
    TAG_ID_JSON = 12,
    TAG_ID_RELAY = 13,
    TAG_ID_SYSTEM = 14,
    // Add more tags as needed (max 32 total for uint32_t bitmap)

    NUM_LOG_TAGS  // Must be last - count of tags
};

// Tag string constants (for code usage)
#define TAG_SD        "SD"
#define TAG_BT        "BT"
#define TAG_CAN       "CAN"
#define TAG_ADC       "ADC"
#define TAG_SENSOR    "SENSOR"
#define TAG_CONFIG    "CONFIG"
#define TAG_ALARM     "ALARM"
#define TAG_DISPLAY   "DISPLAY"
#define TAG_ROUTER    "ROUTER"
#define TAG_SERIAL    "SERIAL"
#define TAG_I2C       "I2C"
#define TAG_SPI       "SPI"
#define TAG_JSON      "JSON"
#define TAG_RELAY     "RELAY"
#define TAG_SYSTEM    "SYSTEM"

// Tag name array (stored in PROGMEM to save RAM)
const char TAG_NAME_SD[]      PROGMEM = "SD";
const char TAG_NAME_BT[]      PROGMEM = "BT";
const char TAG_NAME_CAN[]     PROGMEM = "CAN";
const char TAG_NAME_ADC[]     PROGMEM = "ADC";
const char TAG_NAME_SENSOR[]  PROGMEM = "SENSOR";
const char TAG_NAME_CONFIG[]  PROGMEM = "CONFIG";
const char TAG_NAME_ALARM[]   PROGMEM = "ALARM";
const char TAG_NAME_DISPLAY[] PROGMEM = "DISPLAY";
const char TAG_NAME_ROUTER[]  PROGMEM = "ROUTER";
const char TAG_NAME_SERIAL[]  PROGMEM = "SERIAL";
const char TAG_NAME_I2C[]     PROGMEM = "I2C";
const char TAG_NAME_SPI[]     PROGMEM = "SPI";
const char TAG_NAME_JSON[]    PROGMEM = "JSON";
const char TAG_NAME_RELAY[]   PROGMEM = "RELAY";
const char TAG_NAME_SYSTEM[]  PROGMEM = "SYSTEM";

// Array of tag name pointers (in PROGMEM)
const char* const LOG_TAG_NAMES[] PROGMEM = {
    TAG_NAME_SD,
    TAG_NAME_BT,
    TAG_NAME_CAN,
    TAG_NAME_ADC,
    TAG_NAME_SENSOR,
    TAG_NAME_CONFIG,
    TAG_NAME_ALARM,
    TAG_NAME_DISPLAY,
    TAG_NAME_ROUTER,
    TAG_NAME_SERIAL,
    TAG_NAME_I2C,
    TAG_NAME_SPI,
    TAG_NAME_JSON,
    TAG_NAME_RELAY,
    TAG_NAME_SYSTEM
};

// Helper function to get tag ID from string name
// Returns NUM_LOG_TAGS if tag not found
uint8_t getTagID(const char* tagName);

// Helper function to get tag name from ID
// Returns nullptr if ID out of range
const char* getTagName(uint8_t tagId);

#endif // LOG_TAGS_H
