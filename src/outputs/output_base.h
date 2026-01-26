/*
 * output_base.h - Base interface for output modules
 */

#ifndef OUTPUT_BASE_H
#define OUTPUT_BASE_H

#include "../inputs/input.h"
#include "../lib/sensor_types.h"
#ifdef USE_STATIC_CONFIG
#include "../lib/generated/sensor_library_static.h"
#else
#include "../lib/sensor_library.h"
#endif
#include "../lib/system_config.h"  // For OutputID enum

// Output module structure
typedef struct {
    const char* name;
    bool enabled;
    void (*init)(void);
    void (*send)(Input*);
    void (*update)(void);  // Called each loop iteration
    uint16_t sendInterval;  // Milliseconds between sends
} OutputModule;

// Output module functions
void initOutputModules();
void sendToOutputs(uint32_t now);  // Send data to all outputs (time-sliced)
void updateOutputs();              // Housekeeping (drain buffers, etc.)

// Runtime configuration API
bool setOutputEnabled(const char* name, bool enabled);
bool setOutputInterval(const char* name, uint16_t interval);
OutputModule* getOutputByName(const char* name);
void listOutputs();          // Show output status (enabled/disabled + intervals)
void listOutputModules();    // Show available output module names

// ===== OBDII FRAME BUILDING =====

// Build standard OBDII Mode 01 frame data (ISO 15765-4 compliant)
// Fixes: 1) Correct length byte calculation, 2) Big-endian byte order
// Parameters:
//   frameData - 8-byte buffer to fill
//   ptr - Input with obd2pid, obd2length, and obdConvert()
// Returns: true if successful, false if data size invalid
inline bool buildOBD2Frame(byte* frameData, Input* ptr) {
    byte mode = 0x41;  // Mode 01: Show current data
    byte dataBytes = ptr->obd2length;

    // Validate data size (max 5 bytes to fit in frameData[3..7])
    if (dataBytes == 0 || dataBytes > 5) {
        return false;
    }

    // Clear frame
    for (int i = 0; i < 8; i++) {
        frameData[i] = 0;
    }

    // Byte 0: Length = mode + PID + data (ISO 15765-4 single-frame format)
    frameData[0] = 2 + dataBytes;  // FIX: Was just dataBytes, now 2 + dataBytes
    frameData[1] = mode;
    frameData[2] = ptr->obd2pid;

    // Convert value to OBDII format using sensor's conversion function
    float obdValue = getObdConvertFunc(ptr->measurementType)(ptr->value);

    // Encode data based on size (big-endian / MSB first)
    if (dataBytes == 1) {
        // 1-byte data (most temperatures, percentages)
        frameData[3] = (byte)obdValue;
    } else if (dataBytes == 2) {
        // 2-byte data (RPM, high-precision temps, pressures)
        uint16_t value = (uint16_t)obdValue;
        frameData[3] = (value >> 8) & 0xFF;  // MSB first (FIX: Was LSB first)
        frameData[4] = value & 0xFF;         // LSB second
    } else {
        // 3-5 byte data (rare, but supported)
        uint32_t value = (uint32_t)obdValue;
        for (byte i = 0; i < dataBytes; i++) {
            frameData[3 + i] = (value >> ((dataBytes - 1 - i) * 8)) & 0xFF;
        }
    }

    return true;
}

#endif
