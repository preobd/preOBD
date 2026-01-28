/*
 * output_sdlog.cpp - SD card data logging module
 * Example of how to add a new output module
 */

#include "output_base.h"
#include "../config.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"

#ifdef ENABLE_SD_LOGGING

// Use Arduino SD library for consistency across platforms
#include <SD.h>
#include "../lib/sd_manager.h"

// SD object is provided globally by SD.h

File logFile;
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL = 1000;  // Log every 1 second

void initSDLog() {
    // SD card is already initialized by initSD() in main setup()
    // Just check if it's available and create log file

    if (!isSDInitialized()) {
        msg.debug.warn(TAG_SD, "SD logging failed - SD card not initialized");
        return;
    }

    msg.debug.info(TAG_SD, "SD card ready for logging");

    // Create or open log file with timestamp
    char filename[20];
    snprintf(filename, sizeof(filename), "log_%lu.csv", millis());

    logFile = SD.open(filename, FILE_WRITE);

    if (logFile) {
        // Write CSV header
        logFile.println("Time,Sensor,Value,Units");
        logFile.flush();
        msg.debug.info(TAG_SD, "Logging to: %s", filename);
    } else {
        msg.debug.error(TAG_SD, "Failed to create log file");
    }
}

void sendSDLog(Input *ptr) {
    if (!logFile) {
        return;  // File not open
    }
    
    // Throttle logging to avoid SD wear
    if (millis() - lastLogTime < LOG_INTERVAL) {
        return;
    }
    
    if (isnan(ptr->value)) {
        return;  // Don't log invalid data
    }

    // Convert to display units for logging
    float displayValue = convertFromBaseUnits(ptr->value, ptr->unitsIndex);

    // Write CSV line: timestamp, sensor name, value, units
    logFile.print(millis());
    logFile.print(",");
    logFile.print(ptr->abbrName);
    logFile.print(",");
    logFile.print(displayValue, 2);
    logFile.print(",");
    logFile.print(getUnitStringByIndex(ptr->unitsIndex));
    logFile.println();
}

void updateSDLog() {
    static unsigned long lastFlush = 0;
    
    // Flush to SD card every 5 seconds to ensure data is written
    if (millis() - lastFlush > 5000) {
        if (logFile) {
            logFile.flush();
        }
        lastFlush = millis();
        lastLogTime = millis();
    }
}

void closeSDLog() {
    if (logFile) {
        logFile.close();
        msg.debug.info(TAG_SD, "Log file closed");
    }
}

#else

// Dummy functions if SD logging is disabled
void initSDLog() {}
void sendSDLog(Input *ptr) {}
void updateSDLog() {}
void closeSDLog() {}

#endif
