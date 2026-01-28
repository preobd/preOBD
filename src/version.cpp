/*
 * version.cpp - Firmware version helper functions
 */

#include "version.h"
#include <stdio.h>
#include <string.h>

uint32_t firmwareVersion() {
    return FW_BUILD_NUMBER;
}

const char* firmwareVersionString() {
    static char buf[48];

    // Format: "0.6.3-beta (b147 @a1b2c3d)" or "0.6.3 (b147 @a1b2c3d)"
    snprintf(buf, sizeof(buf), "%d.%d.%d%s%s (b%d @%s)",
        FW_MAJOR,
        FW_MINOR,
        FW_PATCH,
        (strlen(FW_PRERELEASE) > 0) ? "-" : "",
        FW_PRERELEASE,
        FW_BUILD_NUMBER,
        FW_GIT_HASH
    );

    return buf;
}
