/*
 * registry_display.cpp - CLI listers over the sensor and application registries.
 *
 * Moved verbatim from inputs/input_manager.cpp (#402 step 1). Zero
 * behaviour change intended.
 */

#include <string.h>

#include "registry_display.h"
#include "../comms/message_router.h"  // For msg.control
#include "../core/message_api.h"      // For msg
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"

// These are COLD paths: they run only when a CLI command asks for them, never
// in the sensor loop. On Teensy 4.x every function lands in ITCM (RAM1) by
// default, and ITCM is allocated in 32 KB blocks -- crossing a block boundary
// costs 32 KB of RAM1 that local variables can no longer use, not the few
// hundred bytes the code actually occupies. FLASHMEM keeps this module in
// flash where it belongs. Same macro json_config.cpp / json_registry.cpp /
// pin_label.cpp already use; a no-op off Teensy.
#ifndef FLASHMEM
#define FLASHMEM
#endif

FLASHMEM void listApplicationPresets() {
    msg.control.println(F("Available Application Presets:"));

    // Group by measurement type
    msg.control.println(F("Temperature:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_TEMPERATURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            // Pad to 20 chars for alignment
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Pressure:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_PRESSURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Electrical:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Other:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type != MEASURE_TEMPERATURE && type != MEASURE_PRESSURE && type != MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }
}

/**
 * List sensors - supports three modes:
 *   1. No filter: Show category summary with sensor counts
 *   2. Category filter: Show sensors in that category (e.g., "NTC_THERMISTOR", "NTC")
 *   3. Measurement filter: Show all sensors of that type (e.g., "TEMPERATURE", "PRESSURE")
 */
FLASHMEM void listSensors(const char* filter) {
    // Helper lambda to print a single sensor
    auto printSensor = [](const SensorInfo* sensor) {
        const char* name = READ_SENSOR_NAME(sensor);
        const char* label = (const char*)pgm_read_ptr(&sensor->label);
        msg.control.print(F("  "));
        msg.control.print((__FlashStringHelper*)name);
        uint8_t nameLen = strlen_P(name);
        for (uint8_t j = nameLen; j < 24; j++) msg.control.write(' ');
        msg.control.print(F("- "));
        if (label) msg.control.println((__FlashStringHelper*)label);
        else msg.control.println();
    };

    // No filter: Show category summary
    if (filter == nullptr) {
        msg.control.println(F("Sensor Categories:"));
        msg.control.println();

        for (uint8_t cat = 0; cat < CAT_COUNT; cat++) {
            uint8_t count = countSensorsInCategory((SensorCategory)cat);
            if (count > 0) {
                const SensorCategoryInfo* catInfo = getCategoryInfo((SensorCategory)cat);
                const char* catName = READ_CATEGORY_NAME(catInfo);
                const char* catLabel = READ_CATEGORY_LABEL(catInfo);

                msg.control.print(F("  "));
                msg.control.print((__FlashStringHelper*)catName);
                uint8_t nameLen = strlen_P(catName);
                for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
                msg.control.print(F("- "));
                msg.control.print((__FlashStringHelper*)catLabel);
                msg.control.print(F(" ("));
                msg.control.print(count);
                msg.control.println(F(")"));
            }
        }

        msg.control.println();
        msg.control.println(F("Measurement Type Filters:"));
        msg.control.print(F("  TEMPERATURE           - All temperature sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_TEMPERATURE));
        msg.control.println(F(")"));
        msg.control.print(F("  PRESSURE              - All pressure sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_PRESSURE));
        msg.control.println(F(")"));

        msg.control.println();
        msg.control.println(F("Usage: LIST SENSORS <category>    - Show sensors in category"));
        msg.control.println(F("       LIST SENSORS TEMPERATURE   - Show all temperature sensors"));
        msg.control.println(F("       SET <pin> SENSOR <category> <preset>"));
        msg.control.println();
        msg.control.println(F("Aliases: NTC -> THERMISTOR"));
        msg.control.println(F("         TC -> THERMOCOUPLE"));
        msg.control.println(F("         RPM, SPEED -> FREQUENCY"));
        return;
    }

    // Check if filter is a measurement type (TEMPERATURE, PRESSURE)
    int8_t measFilter = getMeasurementTypeFilter(filter);
    if (measFilter >= 0) {
        MeasurementType measType = (MeasurementType)measFilter;
        const char* typeName = (measType == MEASURE_TEMPERATURE) ? "Temperature" : "Pressure";

        msg.control.print(F("All "));
        msg.control.print(typeName);
        msg.control.println(F(" Sensors:"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
            if (sensorMeasType == measType) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        return;
    }

    // Check if filter is a category name or alias
    SensorCategory cat = getCategoryByName(filter);
    if (cat < CAT_COUNT) {
        const SensorCategoryInfo* catInfo = getCategoryInfo(cat);
        const char* catLabel = READ_CATEGORY_LABEL(catInfo);

        msg.control.print((__FlashStringHelper*)catLabel);
        msg.control.println(F(":"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            if (getSensorCategory(i) == cat) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("Usage: SET <pin> SENSOR <category> <preset>"));
        if (cat == CAT_ENVIRONMENTAL) {
            msg.control.println(F("Note: Use 'I2C' for pin, e.g., SET I2C AMBIENT_TEMP BME280_TEMP"));
        } else if (cat == CAT_PRESSURE || cat == CAT_THERMISTOR) {
            msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        }
        return;
    }

    // Unknown filter
    msg.control.print(F("ERROR: Unknown category or filter '"));
    msg.control.print(filter);
    msg.control.println(F("'"));
    msg.control.println(F("Use: LIST SENSORS to see available categories"));
}
