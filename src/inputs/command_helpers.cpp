/*
 * command_helpers.cpp - Helper functions and help system
 * Extracted from serial_config_old.cpp
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#include "../config.h"

#ifndef USE_STATIC_CONFIG

#include "command_helpers.h"
#include "../config.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include "../lib/system_config.h"
#include "../lib/platform.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include <string.h>
#include <ctype.h>

// Helper: trim whitespace in-place
void trim(char* str) {
    if (!str || *str == '\0') return;

    // Trim leading
    char* start = str;
    while (*start && isspace(*start)) start++;

    // Trim trailing
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';

    // Move trimmed string to start
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Helper: convert to uppercase in-place
void toUpper(char* str) {
    while (*str) {
        *str = toupper(*str);
        str++;
    }
}

// Helper: case-insensitive string compare
bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(*a) != toupper(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Helper: Parse message plane name
MessagePlane parsePlane(const char* str, bool* isValid) {
    if (streq(str, "CONTROL")) {
        if (isValid) *isValid = true;
        return PLANE_CONTROL;
    }
    if (streq(str, "DATA")) {
        if (isValid) *isValid = true;
        return PLANE_DATA;
    }
    if (streq(str, "DEBUG")) {
        if (isValid) *isValid = true;
        return PLANE_DEBUG;
    }

    if (isValid) *isValid = false;
    return PLANE_CONTROL;  // Safe default (but isValid=false indicates error)
}

// Helper: Parse transport ID
TransportID parseTransport(const char* str, bool* isValid) {
    if (streq(str, "USB_SERIAL") || streq(str, "USB") || streq(str, "SERIAL")) {
        if (isValid) *isValid = true;
        return TRANSPORT_USB_SERIAL;
    }
    if (streq(str, "SERIAL1")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL1;
    }
    if (streq(str, "SERIAL2")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL2;
    }
    if (streq(str, "SERIAL3")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL3;
    }
    if (streq(str, "SERIAL4")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL4;
    }
    if (streq(str, "SERIAL5")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL5;
    }
    if (streq(str, "SERIAL6")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL6;
    }
    if (streq(str, "SERIAL7")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL7;
    }
    if (streq(str, "SERIAL8")) {
        if (isValid) *isValid = true;
        return TRANSPORT_SERIAL8;
    }
    if (streq(str, "ESP32_BT") || streq(str, "ESP32") || streq(str, "ESP32BT")) {
        if (isValid) *isValid = true;
        return TRANSPORT_ESP32_BT;
    }
    if (streq(str, "NONE")) {
        if (isValid) *isValid = true;
        return TRANSPORT_NONE;
    }

    if (isValid) *isValid = false;
    return TRANSPORT_NONE;  // Safe default (but isValid=false indicates error)
}

// Helper: Parse file path with destination prefix
FilePathComponents parseFilePath(const char* pathStr) {
    FilePathComponents result;
    result.isValid = false;
    result.destination[0] = '\0';
    result.filename[0] = '\0';

    // Validate input
    if (!pathStr || pathStr[0] == '\0') {
        return result;
    }

    // Look for colon separator
    const char* colon = strchr(pathStr, ':');

    if (colon != nullptr) {
        // Format: "destination:filename"
        size_t destLen = colon - pathStr;

        // Validate destination length
        if (destLen == 0 || destLen >= sizeof(result.destination)) {
            msg.control.println(F("ERROR: Invalid destination prefix"));
            return result;
        }

        // Extract destination (convert to uppercase)
        strncpy(result.destination, pathStr, destLen);
        result.destination[destLen] = '\0';
        toUpper(result.destination);

        // Extract filename (skip colon)
        strncpy(result.filename, colon + 1, sizeof(result.filename) - 1);
        result.filename[sizeof(result.filename) - 1] = '\0';

        // Validate filename not empty
        if (result.filename[0] == '\0') {
            msg.control.println(F("ERROR: Filename required after ':' separator"));
            return result;
        }
    } else {
        // No colon - default to SD destination
        strcpy(result.destination, "SD");
        strncpy(result.filename, pathStr, sizeof(result.filename) - 1);
        result.filename[sizeof(result.filename) - 1] = '\0';
    }

    // Validate filename
    if (strlen(result.filename) == 0) {
        msg.control.println(F("ERROR: Empty filename"));
        return result;
    }

    result.isValid = true;
    return result;
}

// ===== HELP SYSTEM =====
// Category-based help system for hierarchical command reference

// Function pointer type for help printer functions
typedef void (*HelpPrinter)();

// Help category structure
struct HelpCategory {
    const char* name;           // Category name (in PROGMEM)
    const char* description;    // Short description (in PROGMEM)
    HelpPrinter printer;        // Function to print detailed help
};

// PROGMEM string literals for category names and descriptions
static const char PSTR_HELP_LIST[] PROGMEM = "LIST";
static const char PSTR_HELP_LIST_DESC[] PROGMEM = "Discovery - Show available inputs, applications, and sensors";

static const char PSTR_HELP_SET[] PROGMEM = "SET";
static const char PSTR_HELP_SET_DESC[] PROGMEM = "Configuration - Configure input pins (application, sensor, names, units, alarms)";

static const char PSTR_HELP_CALIBRATION[] PROGMEM = "CALIBRATION";
static const char PSTR_HELP_CALIBRATION_DESC[] PROGMEM = "Advanced SET options - Custom sensor calibration (RPM, speed, pressure, temperature)";

static const char PSTR_HELP_CONTROL[] PROGMEM = "CONTROL";
static const char PSTR_HELP_CONTROL_DESC[] PROGMEM = "Input commands - ENABLE, DISABLE, CLEAR, INFO";

static const char PSTR_HELP_OUTPUT[] PROGMEM = "OUTPUT";
static const char PSTR_HELP_OUTPUT_DESC[] PROGMEM = "Output Modules - Configure CAN, RealDash, Serial, and SD logging";

static const char PSTR_HELP_BUS[] PROGMEM = "BUS";
static const char PSTR_HELP_BUS_DESC[] PROGMEM = "Bus Config - Configure I2C, SPI, and CAN buses";

#ifdef ENABLE_RELAY_OUTPUT
static const char PSTR_HELP_RELAY[] PROGMEM = "RELAY";
static const char PSTR_HELP_RELAY_DESC[] PROGMEM = "Relay Control - Threshold-based relay outputs for cooling fans, alarms, etc.";
#endif

#ifdef ENABLE_TEST_MODE
static const char PSTR_HELP_TEST[] PROGMEM = "TEST";
static const char PSTR_HELP_TEST_DESC[] PROGMEM = "Test Mode - Simulate sensor inputs with predefined scenarios";
#endif

static const char PSTR_HELP_DISPLAY[] PROGMEM = "DISPLAY";
static const char PSTR_HELP_DISPLAY_DESC[] PROGMEM = "Display Config - LCD/OLED settings and unit preferences";

static const char PSTR_HELP_TRANSPORT[] PROGMEM = "TRANSPORT";
static const char PSTR_HELP_TRANSPORT_DESC[] PROGMEM = "Message Routing - Route control, data, and debug messages";

static const char PSTR_HELP_SYSTEM[] PROGMEM = "SYSTEM";
static const char PSTR_HELP_SYSTEM_DESC[] PROGMEM = "System Config - Sea level pressure, read intervals (advanced)";

static const char PSTR_HELP_CONFIG[] PROGMEM = "CONFIG";
static const char PSTR_HELP_CONFIG_DESC[] PROGMEM = "Persistence & Modes - Save, load, reset, and system control";

static const char PSTR_HELP_EXAMPLES[] PROGMEM = "EXAMPLES";
static const char PSTR_HELP_EXAMPLES_DESC[] PROGMEM = "Usage Examples - Common configuration workflows and patterns";

// Forward declarations for help printing functions
void printHelpList();
void printHelpSet();
void printHelpCalibration();
void printHelpControl();
void printHelpOutput();
void printHelpBus();
#ifdef ENABLE_RELAY_OUTPUT
void printHelpRelay();
#endif
#ifdef ENABLE_TEST_MODE
void printHelpTest();
#endif
void printHelpDisplay();
void printHelpTransport();
void printHelpSystem();
void printHelpConfig();
void printHelpExamples();
void printHelpOverview();
void printHelpQuick();

// Help category registry (in flash memory)
static const HelpCategory HELP_CATEGORIES[] PROGMEM = {
    {PSTR_HELP_LIST, PSTR_HELP_LIST_DESC, printHelpList},
    {PSTR_HELP_SET, PSTR_HELP_SET_DESC, printHelpSet},
    {PSTR_HELP_CALIBRATION, PSTR_HELP_CALIBRATION_DESC, printHelpCalibration},
    {PSTR_HELP_CONTROL, PSTR_HELP_CONTROL_DESC, printHelpControl},
    {PSTR_HELP_OUTPUT, PSTR_HELP_OUTPUT_DESC, printHelpOutput},
    {PSTR_HELP_BUS, PSTR_HELP_BUS_DESC, printHelpBus},
#ifdef ENABLE_RELAY_OUTPUT
    {PSTR_HELP_RELAY, PSTR_HELP_RELAY_DESC, printHelpRelay},
#endif
#ifdef ENABLE_TEST_MODE
    {PSTR_HELP_TEST, PSTR_HELP_TEST_DESC, printHelpTest},
#endif
    {PSTR_HELP_DISPLAY, PSTR_HELP_DISPLAY_DESC, printHelpDisplay},
    {PSTR_HELP_TRANSPORT, PSTR_HELP_TRANSPORT_DESC, printHelpTransport},
    {PSTR_HELP_SYSTEM, PSTR_HELP_SYSTEM_DESC, printHelpSystem},
    {PSTR_HELP_CONFIG, PSTR_HELP_CONFIG_DESC, printHelpConfig},
    {PSTR_HELP_EXAMPLES, PSTR_HELP_EXAMPLES_DESC, printHelpExamples}
};
static const uint8_t NUM_HELP_CATEGORIES = sizeof(HELP_CATEGORIES) / sizeof(HelpCategory);

// Helper function to print system configuration details
void printSystemConfig() {
    msg.control.println(F("=== System Configuration ==="));
    extern uint8_t numActiveInputs;
    msg.control.print(F("Active Inputs: "));
    msg.control.println(numActiveInputs);
    msg.control.print(F("System Voltage: "));
    msg.control.print(SYSTEM_VOLTAGE);
    msg.control.println(F("V"));
    msg.control.print(F("ADC Reference: "));
    msg.control.print(AREF_VOLTAGE);
    msg.control.println(F("V"));
    msg.control.print(F("ADC Resolution: "));
    msg.control.print(ADC_RESOLUTION);
    msg.control.println(F(" bits"));
    msg.control.print(F("ADC Max Value: "));
    msg.control.println(ADC_MAX_VALUE);
    msg.control.print(F("Sea Level Pressure: "));
    msg.control.print(systemConfig.seaLevelPressure);
    msg.control.println(F(" hPa"));
    msg.control.print(F("Intervals: Sensor="));
    msg.control.print(systemConfig.sensorReadInterval);
    msg.control.print(F("ms, Alarm="));
    msg.control.print(systemConfig.alarmCheckInterval);
    msg.control.print(F("ms, LCD="));
    msg.control.print(systemConfig.lcdUpdateInterval);
    msg.control.println(F("ms"));

    // Log Filter Configuration
    msg.control.println();
    msg.control.println(F("=== Log Filter Configuration ==="));
    const char* levelNames[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG"};
    msg.control.print(F("Control Level: "));
    msg.control.println(systemConfig.logFilter.control_level <= 4 ? levelNames[systemConfig.logFilter.control_level] : "UNKNOWN");
    msg.control.print(F("Data Level: "));
    msg.control.println(systemConfig.logFilter.data_level <= 4 ? levelNames[systemConfig.logFilter.data_level] : "UNKNOWN");
    msg.control.print(F("Debug Level: "));
    msg.control.println(systemConfig.logFilter.debug_level <= 4 ? levelNames[systemConfig.logFilter.debug_level] : "UNKNOWN");
    msg.control.print(F("Enabled Tags: 0x"));
    msg.control.println(systemConfig.logFilter.enabledTags, HEX);
}

void printDisplayConfig() {
    msg.control.println(F("=== Display Configuration ==="));

    // Display enabled status
    msg.control.print(F("Status: "));
    msg.control.println(systemConfig.displayEnabled ? F("Enabled") : F("Disabled"));

    // Display type
    msg.control.print(F("Type: "));
    switch (systemConfig.displayType) {
        case DISPLAY_NONE: msg.control.println(F("None")); break;
        case DISPLAY_LCD: msg.control.println(F("LCD")); break;
        case DISPLAY_OLED: msg.control.println(F("OLED")); break;
        default: msg.control.println(F("Unknown")); break;
    }

    // I2C address (for LCD)
    if (systemConfig.displayType == DISPLAY_LCD) {
        msg.control.print(F("LCD I2C Address: 0x"));
        if (systemConfig.lcdI2CAddress < 0x10) msg.control.print(F("0"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
    }

    // Update interval
    msg.control.print(F("Update Interval: "));
    msg.control.print(systemConfig.lcdUpdateInterval);
    msg.control.println(F(" ms"));

    // Default units
    msg.control.print(F("Default Units - Temp: "));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
    msg.control.print(F(", Press: "));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
    msg.control.print(F(", Elev: "));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
    msg.control.print(F(", Speed: "));
    msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultSpeedUnits));
}

// ===== HELP PRINTER FUNCTIONS =====

void printHelpList() {
    msg.control.println();
    msg.control.println(F("=== LIST Commands ==="));
    msg.control.println(F("Discovery commands to explore available options"));
    msg.control.println();
    msg.control.println(F("  LIST INPUTS         - Show all configured inputs"));
    msg.control.println(F("  LIST APPLICATIONS   - Show available Type presets"));
    msg.control.println(F("  LIST SENSORS        - Show available Sensor Types"));
    msg.control.println(F("  LIST OUTPUTS        - Show available output modules"));
    msg.control.println(F("  LIST TRANSPORTS     - Show available transports"));
    msg.control.println();
}

void printHelpSet() {
    msg.control.println();
    msg.control.println(F("=== SET Commands ==="));
    msg.control.println(F("Configure input pins (application, sensor, names, units, alarms)"));
    msg.control.println();
    msg.control.println(F("Basic Configuration:"));
    msg.control.println(F("  SET <pin> <app> <sensor>  - Combined config (e.g., SET 6 CHT MAX6675)"));
    msg.control.println(F("  SET <pin> APPLICATION <application>  - Set measurement type"));
    msg.control.println(F("  SET <pin> SENSOR <sensor>  - Set hardware sensor"));
    msg.control.println();
    msg.control.println(F("Naming:"));
    msg.control.println(F("  SET <pin> NAME <name>  - Set abbreviated name (8 chars)"));
    msg.control.println(F("  SET <pin> DISPLAY_NAME <name>  - Set full name (32 chars)"));
    msg.control.println(F("  SET <pin> UNITS <units>  - Override display units"));
    msg.control.println();
    msg.control.println(F("Alarms:"));
    msg.control.println(F("  SET <pin> ALARM <min> <max>  - Set alarm thresholds"));
    msg.control.println(F("  SET <pin> ALARM ENABLE  - Enable alarm for input"));
    msg.control.println(F("  SET <pin> ALARM DISABLE  - Disable alarm for input"));
    msg.control.println(F("  SET <pin> ALARM WARMUP <ms>  - Alarm warmup time (0-300000ms)"));
    msg.control.println(F("  SET <pin> ALARM PERSIST <ms>  - Alarm persistence time (0-60000ms)"));
    msg.control.println();
    msg.control.println(F("See also: HELP CALIBRATION for advanced sensor calibration"));
    msg.control.println();
}

void printHelpCalibration() {
    msg.control.println();
    msg.control.println(F("=== CALIBRATION Commands ==="));
    msg.control.println(F("Advanced sensor calibration (RPM, speed, pressure, temperature)"));
    msg.control.println();
    msg.control.println(F("  SET <pin> CALIBRATION PRESET  - Clear custom, use preset"));
    msg.control.println(F("  SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>"));
    msg.control.println(F("  SET <pin> SPEED <ppr> <tire_circ> <ratio> [<mult>] <timeout> <max>"));
    msg.control.println(F("  SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
    msg.control.println(F("  SET <pin> BIAS <resistor>  - Set bias resistor (Ohms)"));
    msg.control.println(F("  SET <pin> STEINHART <bias> <a> <b> <c>  - Steinhart-Hart"));
    msg.control.println(F("  SET <pin> BETA <bias> <beta> <r0> <t0>  - Beta equation"));
    msg.control.println(F("  SET <pin> PRESSURE_POLY <bias> <a> <b> <c>  - VDO polynomial"));
    msg.control.println(F("  INFO <pin> CALIBRATION  - Show calibration details"));
    msg.control.println();
}

void printHelpControl() {
    msg.control.println();
    msg.control.println(F("=== CONTROL Commands ==="));
    msg.control.println(F("Enable, disable, clear, and query input status"));
    msg.control.println();
    msg.control.println(F("  ENABLE <pin>  - Enable input reading"));
    msg.control.println(F("  DISABLE <pin>  - Disable input reading"));
    msg.control.println(F("  CLEAR <pin>  - Reset input to unconfigured"));
    msg.control.println(F("  INFO <pin>  - Show detailed pin info"));
    msg.control.println(F("  INFO <pin> ALARM  - Show alarm status and configuration"));
    msg.control.println(F("  INFO <pin> CALIBRATION  - Show calibration details"));
    msg.control.println();
}

void printHelpOutput() {
    msg.control.println();
    msg.control.println(F("=== OUTPUT Commands ==="));
    msg.control.println(F("Configure CAN, RealDash, Serial, and SD logging"));
    msg.control.println();
    msg.control.println(F("  OUTPUT STATUS  - Show current output states"));
    msg.control.println(F("  OUTPUT <name> ENABLE  - Enable output (CAN, RealDash, Serial, SD_Log)"));
    msg.control.println(F("  OUTPUT <name> DISABLE  - Disable output"));
    msg.control.println(F("  OUTPUT <name> INTERVAL <ms>  - Set output interval"));
    msg.control.println();
}

void printHelpBus() {
    msg.control.println();
    msg.control.println(F("=== BUS Commands ==="));
    msg.control.println(F("Configure I2C, SPI, CAN buses and Serial ports"));
    msg.control.println();
    msg.control.println(F("Display Bus Configuration:"));
    msg.control.println(F("  BUS I2C                   - Show all I2C bus status"));
    msg.control.println(F("  BUS SPI                   - Show all SPI bus status"));
    msg.control.println(F("  BUS CAN                   - Show all CAN bus status"));
    msg.control.println(F("  BUS SERIAL                - Show all serial port status"));
    msg.control.println();
    msg.control.println(F("I2C Bus Commands:"));
    msg.control.println(F("  BUS I2C [0|1|2]           - Select I2C bus (Wire/Wire1/Wire2)"));
    msg.control.println(F("  BUS I2C CLOCK <kHz>       - Set I2C clock (100, 400, 1000)"));
    msg.control.println();
    msg.control.println(F("SPI Bus Commands:"));
    msg.control.println(F("  BUS SPI [0|1|2]           - Select SPI bus (SPI/SPI1/SPI2)"));
    msg.control.println(F("  BUS SPI CLOCK <Hz>        - Set SPI clock (e.g., 4000000)"));
    msg.control.println();
    msg.control.println(F("CAN Bus Commands:"));
    msg.control.println(F("  BUS CAN [0|1|2]           - Select CAN bus (CAN1/CAN2/CAN3)"));
    msg.control.println(F("  BUS CAN BAUDRATE <bps>    - Set CAN baudrate"));
    msg.control.println(F("    Valid baudrates: 125000, 250000, 500000, 1000000"));
    msg.control.println();
    msg.control.println(F("Serial Port Commands:"));
    msg.control.println(F("  BUS SERIAL <1-8>          - Show specific port status"));
    msg.control.println(F("  BUS SERIAL <1-8> ENABLE [baud] - Enable port"));
    msg.control.println(F("  BUS SERIAL <1-8> DISABLE  - Disable port"));
    msg.control.println(F("  BUS SERIAL <1-8> BAUDRATE <rate> - Set baud rate"));
    msg.control.println(F("    Valid rates: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600"));
    msg.control.println();
    msg.control.println(F("Examples:"));
    msg.control.println(F("  BUS I2C 1                 # Select Wire1"));
    msg.control.println(F("  BUS CAN 0 BAUDRATE 250000 # Set CAN1 to 250kbps"));
    msg.control.println(F("  BUS SERIAL 5 ENABLE 115200 # Enable Serial5 at 115200"));
    msg.control.println();
}

#ifdef ENABLE_RELAY_OUTPUT
void printHelpRelay() {
    msg.control.println();
    msg.control.println(F("=== RELAY Commands ==="));
    msg.control.println(F("Threshold-based relay outputs for cooling fans, alarms, etc."));
    msg.control.println();
    msg.control.println(F("  RELAY LIST  - Show all relay status"));
    msg.control.println(F("  RELAY <0-1> STATUS  - Show specific relay"));
    msg.control.println(F("  RELAY <0-1> PIN <pin>  - Set relay output pin"));
    msg.control.println(F("  RELAY <0-1> INPUT <pin>  - Link relay to sensor input"));
    msg.control.println(F("  RELAY <0-1> THRESHOLD <on> <off>  - Set activation thresholds"));
    msg.control.println(F("  RELAY <0-1> MODE <DISABLED|AUTO_HIGH|AUTO_LOW|MANUAL_ON|MANUAL_OFF>"));
    msg.control.println();
}
#endif

#ifdef ENABLE_TEST_MODE
void printHelpTest() {
    msg.control.println();
    msg.control.println(F("=== TEST Commands ==="));
    msg.control.println(F("Simulate sensor inputs with predefined test scenarios"));
    msg.control.println();
    msg.control.println(F("  TEST LIST  - Show available test scenarios"));
    msg.control.println(F("  TEST <0-4>  - Start a specific test scenario"));
    msg.control.println(F("  TEST STOP  - Stop current test scenario"));
    msg.control.println(F("  TEST STATUS  - Show current test status"));
    msg.control.println();
    msg.control.println(F("Note: Test mode replaces real sensor readings with simulated values"));
    msg.control.println(F("      All outputs (LCD, CAN, logging) continue to function normally"));
    msg.control.println();
}
#endif

void printHelpDisplay() {
    msg.control.println();
    msg.control.println(F("=== DISPLAY Commands ==="));
    msg.control.println(F("Configure display hardware"));
    msg.control.println();
    msg.control.println(F("  DISPLAY STATUS           - Show display hardware status"));
    msg.control.println(F("  DISPLAY ENABLE           - Enable display"));
    msg.control.println(F("  DISPLAY DISABLE          - Disable display"));
    msg.control.println(F("  DISPLAY TYPE <LCD|OLED|NONE>"));
    msg.control.println(F("  DISPLAY ADDRESS <hex>    - I2C address (LCD only)"));
    msg.control.println(F("  DISPLAY INTERVAL <ms>    - Display refresh rate"));
    msg.control.println();
    msg.control.println(F("Note: Unit preferences moved to SYSTEM UNITS"));
    msg.control.println();
}

void printHelpTransport() {
    msg.control.println();
    msg.control.println(F("=== TRANSPORT Commands ==="));
    msg.control.println(F("Route control, data, and debug messages"));
    msg.control.println();
    msg.control.println(F("  TRANSPORT STATUS  - Show current transport routing"));
    msg.control.println(F("  TRANSPORT CONTROL <transport>  - Route control messages"));
    msg.control.println(F("  TRANSPORT DATA <transport>  - Route sensor data output"));
    msg.control.println(F("  TRANSPORT DEBUG <transport>  - Route debug messages"));
    msg.control.println();
    msg.control.println(F("  (Use LIST TRANSPORTS to see available transports)"));
    msg.control.println();
}

void printHelpSystem() {
    msg.control.println();
    msg.control.println(F("=== SYSTEM Commands ==="));
    msg.control.println(F("Global configuration affecting all subsystems"));
    msg.control.println();

    msg.control.println(F("Query:"));
    msg.control.println(F("  SYSTEM STATUS           - Show all global configuration"));
    msg.control.println(F("  SYSTEM DUMP             - Show complete system dump"));
    msg.control.println(F("  SYSTEM DUMP JSON        - Export configuration as JSON"));
    msg.control.println();

    msg.control.println(F("Global Defaults:"));
    msg.control.println(F("  SYSTEM UNITS TEMP <C|F>"));
    msg.control.println(F("  SYSTEM UNITS PRESSURE <BAR|PSI|KPA|INHG>"));
    msg.control.println(F("  SYSTEM UNITS ELEVATION <M|FT>"));
    msg.control.println(F("  SYSTEM UNITS SPEED <KPH|MPH>"));
    msg.control.println();

    msg.control.println(F("Calibration & Timing:"));
    msg.control.println(F("  SYSTEM SEA_LEVEL <hPa>  - For altitude calculations"));
    msg.control.println(F("  SYSTEM INTERVAL <type> <ms> - Global timing intervals"));
    msg.control.println(F("    Types: SENSOR, ALARM"));
    msg.control.println();

    msg.control.println(F("System Control:"));
    msg.control.println(F("  SYSTEM REBOOT           - Restart the device"));
    msg.control.println(F("  SYSTEM RESET CONFIRM    - Factory reset (erase config + reboot)"));
    msg.control.println();
}

void printHelpConfig() {
    msg.control.println();
    msg.control.println(F("=== CONFIG Commands ==="));
    msg.control.println(F("Persistence, modes, and system control"));
    msg.control.println();
    msg.control.println(F("EEPROM Persistence:"));
    msg.control.println(F("  SAVE                    - Save config to EEPROM"));
    msg.control.println(F("  SAVE EEPROM             - Save config to EEPROM (explicit)"));
    msg.control.println(F("  LOAD                    - Load config from EEPROM"));
    msg.control.println(F("  LOAD EEPROM             - Load config from EEPROM (explicit)"));
    msg.control.println();
    msg.control.println(F("File Storage (SD Card, USB, etc.):"));
    msg.control.println(F("  SAVE [dest:]file        - Save config to file"));
    msg.control.println(F("    Examples:"));
    msg.control.println(F("      SAVE config.json                # Default to SD"));
    msg.control.println(F("      SAVE SD:mycar.json              # Explicit SD"));
    msg.control.println(F("      SAVE USB:backup.json            # USB (if available)"));
    msg.control.println();
    msg.control.println(F("  LOAD [dest:]file        - Load config from file"));
    msg.control.println(F("    Examples:"));
    msg.control.println(F("      LOAD racing.json                # Load from SD"));
    msg.control.println(F("      LOAD SD:backup.json             # Explicit SD"));
    msg.control.println(F("      LOAD USB:restore.json           # USB (if available)"));
    msg.control.println();
    msg.control.println(F("Modes:"));
    msg.control.println(F("  CONFIG                  - Enter configuration mode"));
    msg.control.println(F("  RUN                     - Enter run mode"));
    msg.control.println(F("  SYSTEM REBOOT           - Restart the device"));
    msg.control.println(F("  SYSTEM RESET CONFIRM    - Factory reset (erase config + reboot)"));
    msg.control.println();
    msg.control.println(F("Information:"));
    msg.control.println(F("  VERSION                 - Firmware and EEPROM version"));
    msg.control.println();
}

void printHelpExamples() {
    msg.control.println();
    msg.control.println(F("=== Usage Examples ==="));
    msg.control.println(F("Common configuration workflows and patterns"));
    msg.control.println();
    msg.control.println(F("Basic sensor configuration:"));
    msg.control.println(F("  SET 6 CHT MAX6675  (combined syntax)"));
    msg.control.println(F("  SET A2 APPLICATION COOLANT_TEMP"));
    msg.control.println(F("  SET A2 SENSOR VDO_120C_STEINHART"));
    msg.control.println();
    msg.control.println(F("I2C sensor configuration:"));
    msg.control.println(F("  SET I2C AMBIENT_TEMP BME280_TEMP  (new I2C sensor)"));
    msg.control.println(F("  SET I2C:0 ALARM 10 50  (modify existing I2C sensor)"));
    msg.control.println(F("  INFO I2C:1  (query I2C sensor)"));
    msg.control.println();
    msg.control.println(F("Advanced sensor setup:"));
    msg.control.println(F("  SET 2 SPEED 100 2008 3.73 2000 300  (Hall sensor speed)"));
    msg.control.println(F("  SET 3 ENGINE_RPM W_PHASE_RPM  (alternator RPM)"));
    msg.control.println(F("  SET A1 PRESSURE_LINEAR 0.5 4.5 0 7  (custom pressure)"));
    msg.control.println(F("  SET A0 BIAS 4700  (change bias resistor)"));
    msg.control.println(F("  SET A2 ALARM WARMUP 30000  (30 second warmup)"));
    msg.control.println(F("  SET A2 ALARM PERSIST 2000  (2 second persistence)"));
    msg.control.println();
    msg.control.println(F("Alarm configuration:"));
    msg.control.println(F("  SET A2 ALARM 50 120  (set alarm thresholds)"));
    msg.control.println(F("  SET A2 ALARM ENABLE  (enable alarm)"));
    msg.control.println(F("  INFO A2 ALARM  (show alarm status)"));
    msg.control.println();
    msg.control.println(F("Output and control:"));
    msg.control.println(F("  ENABLE A2"));
    msg.control.println(F("  OUTPUT CAN ENABLE"));
    msg.control.println(F("  OUTPUT CAN INTERVAL 100"));
    msg.control.println(F("  SAVE"));
    msg.control.println();
}

void printHelpOverview() {
    msg.control.println();
    msg.control.println(F("=== openEMS Command Reference ==="));
    msg.control.println();
    msg.control.println(F("Available help categories (use HELP <category>):"));
    msg.control.println();

    // Iterate through categories and print formatted list
    for (uint8_t i = 0; i < NUM_HELP_CATEGORIES; i++) {
        msg.control.print(F("  "));

        #ifdef __AVR__
            // AVR: Read from PROGMEM
            const char* name = (const char*)pgm_read_ptr(&HELP_CATEGORIES[i].name);
            const char* desc = (const char*)pgm_read_ptr(&HELP_CATEGORIES[i].description);

            msg.control.print((__FlashStringHelper*)name);
            // Pad to align descriptions
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 14; j++) {
                msg.control.write(' ');
            }
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)desc);
        #else
            // ESP32/Teensy/other: Read from PROGMEM
            msg.control.print((__FlashStringHelper*)HELP_CATEGORIES[i].name);
            // Pad to align descriptions (use strlen_P for PROGMEM strings)
            uint8_t nameLen = strlen_P(HELP_CATEGORIES[i].name);
            for (uint8_t j = nameLen; j < 14; j++) {
                msg.control.write(' ');
            }
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)HELP_CATEGORIES[i].description);
        #endif
    }

    msg.control.println();
    msg.control.println(F("Quick commands:"));
    msg.control.println(F("  HELP QUICK      - Compact command list"));
    msg.control.println(F("  ?               - Alias for HELP"));
    msg.control.println(F("  VERSION         - Firmware version"));
    msg.control.println(F("  SYSTEM DUMP     - Show full configuration"));
    msg.control.println();
    msg.control.println(F("Examples:"));
    msg.control.println(F("  HELP SET        - Show all SET commands"));
    msg.control.println(F("  HELP CALIBRATION - Show calibration commands"));
    msg.control.println();
}

void printHelpQuick() {
    msg.control.println();
    msg.control.println(F("=== Quick Command Reference ==="));
    msg.control.println();
    msg.control.println(F("Notation: <required> <option1|option2> [optional]"));
    msg.control.println();
    msg.control.println(F("Discovery:"));
    msg.control.println(F("  LIST INPUTS|APPLICATIONS|SENSORS|OUTPUTS|TRANSPORTS"));
    msg.control.println();
    msg.control.println(F("Input Control:"));
    msg.control.println(F("  ENABLE <pin>"));
    msg.control.println(F("  DISABLE <pin>"));
    msg.control.println(F("  CLEAR <pin>"));
    msg.control.println(F("  INFO [<pin>] [ALARM|CALIBRATION]"));
    msg.control.println();
    msg.control.println(F("Input Configuration:"));
    msg.control.println(F("  SET <pin> <app> <sensor>"));
    msg.control.println(F("  SET <pin> APPLICATION <app>"));
    msg.control.println(F("  SET <pin> SENSOR <sensor>"));
    msg.control.println(F("  SET <pin> NAME <name>"));
    msg.control.println(F("  SET <pin> DISPLAY_NAME <name>"));
    msg.control.println(F("  SET <pin> UNITS <units>"));
    msg.control.println(F("  SET <pin> ALARM <min> <max>"));
    msg.control.println(F("  SET <pin> ALARM ENABLE|DISABLE"));
    msg.control.println(F("  SET <pin> ALARM WARMUP|PERSIST <ms>"));
    msg.control.println(F("  SET <pin> CALIBRATION PRESET"));
    msg.control.println(F("  SET <pin> RPM|SPEED|PRESSURE_LINEAR|STEINHART|BETA|BIAS|PRESSURE_POLY ..."));
    msg.control.println();
    msg.control.println(F("Outputs:"));
    msg.control.println(F("  OUTPUT STATUS"));
    msg.control.println(F("  OUTPUT <module> ENABLE|DISABLE"));
    msg.control.println(F("  OUTPUT <module> INTERVAL <ms>"));
    msg.control.println();
    msg.control.println(F("Bus Configuration:"));
    msg.control.println(F("  BUS I2C|SPI|CAN"));
    msg.control.println(F("  BUS <type> <0-2> ENABLE|DISABLE"));
    msg.control.println(F("  BUS I2C <0-2> CLOCK <100|400|1000>"));
    msg.control.println(F("  BUS SPI <0-2> CLOCK <Hz>"));
    msg.control.println(F("  BUS CAN <0-2> BAUDRATE <125000|250000|500000|1000000>"));
#ifdef ENABLE_RELAY_OUTPUT
    msg.control.println();
    msg.control.println(F("Relays:"));
    msg.control.println(F("  RELAY LIST"));
    msg.control.println(F("  RELAY <0-1> STATUS"));
    msg.control.println(F("  RELAY <0-1> PIN|INPUT <pin>"));
    msg.control.println(F("  RELAY <0-1> THRESHOLD <on> <off>"));
    msg.control.println(F("  RELAY <0-1> MODE <DISABLED|AUTO_HIGH|AUTO_LOW|MANUAL_ON|MANUAL_OFF>"));
#endif
#ifdef ENABLE_TEST_MODE
    msg.control.println();
    msg.control.println(F("Test Mode:"));
    msg.control.println(F("  TEST LIST|STATUS|STOP"));
    msg.control.println(F("  TEST <0-4>"));
#endif
    msg.control.println();
    msg.control.println(F("Display:"));
    msg.control.println(F("  DISPLAY STATUS"));
    msg.control.println(F("  DISPLAY ENABLE|DISABLE"));
    msg.control.println(F("  DISPLAY TYPE <LCD|OLED|NONE>"));
    msg.control.println(F("  DISPLAY ADDRESS <hex>"));
    msg.control.println(F("  DISPLAY INTERVAL <ms>"));
    msg.control.println();
    msg.control.println(F("Transport:"));
    msg.control.println(F("  TRANSPORT STATUS|LIST"));
    msg.control.println(F("  TRANSPORT CONTROL|DATA|DEBUG <transport>"));
    msg.control.println();
    msg.control.println(F("System:"));
    msg.control.println(F("  SYSTEM STATUS"));
    msg.control.println(F("  SYSTEM DUMP [JSON]"));
    msg.control.println(F("  SYSTEM UNITS <TEMP|PRESSURE|ELEVATION|SPEED> <unit>"));
    msg.control.println(F("  SYSTEM SEA_LEVEL <hPa>"));
    msg.control.println(F("  SYSTEM INTERVAL <SENSOR|ALARM> <ms>"));
    msg.control.println(F("  SYSTEM REBOOT"));
    msg.control.println(F("  SYSTEM RESET CONFIRM"));
    msg.control.println();
    msg.control.println(F("Configuration:"));
    msg.control.println(F("  CONFIG|RUN|RELOAD"));
    msg.control.println(F("  SAVE [EEPROM|[dest:]file]"));
    msg.control.println(F("  LOAD [EEPROM|[dest:]file]"));
    msg.control.println(F("  RESET"));
    msg.control.println(F("  VERSION"));
    msg.control.println();
    msg.control.println(F("For detailed help: HELP <category>"));
    msg.control.println();
}

/**
 * Parse a pin string into a pin number.
 * Accepts "A0"-"A15" for analog pins, numeric strings for digital pins,
 * or "I2C" for I2C sensors (BME280, etc).
 */
uint8_t parsePin(const char* pinStr, bool* isValid) {
    if (!pinStr) {
        if (isValid) *isValid = false;
        return 0;
    }

    if (isValid) *isValid = true;

    // Handle "I2C" keyword for I2C sensors (BME280, etc)
    // Use a virtual pin counter to allow multiple I2C sensors
    // Virtual pins start at 0xF0 (240) - well above any real pin number
    if (streq(pinStr, "I2C")) {
        static uint8_t i2cVirtualPinCounter = 0xF0;

        // Check if we've exceeded the virtual pin range
        if (i2cVirtualPinCounter >= 0xFE) {
            msg.control.println(F("ERROR: Too many I2C sensors configured (max 14)"));
            if (isValid) *isValid = false;
            return 0;
        }

        return i2cVirtualPinCounter++;
    }

    // Handle "I2C:n" format for referencing existing I2C sensors (e.g., "I2C:0", "I2C:1")
    if (strncmp(pinStr, "I2C:", 4) == 0 || strncmp(pinStr, "i2c:", 4) == 0) {
        const char* numStr = pinStr + 4;
        int i2cIndex = atoi(numStr);

        // Validate I2C index range (0-13 for 14 total I2C sensors)
        if (i2cIndex < 0 || i2cIndex > 13) {
            msg.control.print(F("ERROR: I2C index "));
            msg.control.print(i2cIndex);
            msg.control.println(F(" out of range (valid: 0-13)"));
            if (isValid) *isValid = false;
            return 0;
        }

        // Convert I2C index to virtual pin number
        uint8_t virtualPin = 0xF0 + i2cIndex;
        return virtualPin;
    }

    // Analog pins
    if (toupper(pinStr[0]) == 'A') {
        int analogNum = atoi(pinStr + 1);
        uint8_t pin = A0 + analogNum;

        // Validate against platform capabilities
        #if defined(__IMXRT1062__)  // Teensy 4.x
            #if defined(ARDUINO_TEENSY41)
                if (analogNum < 0 || analogNum > 17) {
                    if (isValid) *isValid = false;
                    msg.control.print(F("ERROR: Pin A"));
                    msg.control.print(analogNum);
                    msg.control.println(F(" not available (Teensy 4.1 supports A0-A17)"));
                    return 0;
                }
            #else  // Teensy 4.0
                if (analogNum < 0 || analogNum > 13) {
                    if (isValid) *isValid = false;
                    msg.control.print(F("ERROR: Pin A"));
                    msg.control.print(analogNum);
                    msg.control.println(F(" not available (Teensy 4.0 supports A0-A13)"));
                    return 0;
                }
            #endif
        #elif defined(__AVR_ATmega2560__)  // Arduino Mega
            if (analogNum < 0 || analogNum > 15) {
                if (isValid) *isValid = false;
                msg.control.print(F("ERROR: Pin A"));
                msg.control.print(analogNum);
                msg.control.println(F(" not available (Arduino Mega supports A0-A15)"));
                return 0;
            }
        #elif defined(__AVR_ATmega328P__)  // Arduino Uno
            if (analogNum < 0 || analogNum > 5) {
                if (isValid) *isValid = false;
                msg.control.print(F("ERROR: Pin A"));
                msg.control.print(analogNum);
                msg.control.println(F(" not available (Arduino Uno supports A0-A5)"));
                return 0;
            }
        #else
            // Generic validation using MAX_INPUTS from platform.h
            if (analogNum < 0 || analogNum >= MAX_INPUTS) {
                if (isValid) *isValid = false;
                msg.control.print(F("ERROR: Pin A"));
                msg.control.print(analogNum);
                msg.control.println(F(" not available on this platform"));
                return 0;
            }
        #endif

        return pin;
    }

    // Digital pins
    uint8_t pin = atoi(pinStr);

    // Validate digital pin range
    #if defined(__IMXRT1062__)  // Teensy 4.x
        #if defined(ARDUINO_TEENSY41)
            if (pin > 54) {  // Teensy 4.1: pins 0-54
                if (isValid) *isValid = false;
                msg.control.print(F("ERROR: Digital pin "));
                msg.control.print(pin);
                msg.control.println(F(" not available (Teensy 4.1 supports 0-54)"));
                return 0;
            }
        #else  // Teensy 4.0
            if (pin > 39) {  // Teensy 4.0: pins 0-39
                if (isValid) *isValid = false;
                msg.control.print(F("ERROR: Digital pin "));
                msg.control.print(pin);
                msg.control.println(F(" not available (Teensy 4.0 supports 0-39)"));
                return 0;
            }
        #endif
    #elif defined(__AVR_ATmega2560__)  // Mega has 54 digital pins
        if (pin > 53) {
            if (isValid) *isValid = false;
            msg.control.print(F("ERROR: Digital pin "));
            msg.control.print(pin);
            msg.control.println(F(" not available (Arduino Mega supports 0-53)"));
            return 0;
        }
    #elif defined(__AVR_ATmega328P__)  // Uno has 14 digital pins
        if (pin > 13) {
            if (isValid) *isValid = false;
            msg.control.print(F("ERROR: Digital pin "));
            msg.control.print(pin);
            msg.control.println(F(" not available (Arduino Uno supports 0-13)"));
            return 0;
        }
    #else
        // For unknown platforms, allow any pin (no validation)
        // This prevents breaking support for new platforms
    #endif

    return pin;
}

// Helper function to print help for a specific category
void printHelpCategory(const char* category) {
    // Look up category in table
    bool found = false;
    for (uint8_t i = 0; i < NUM_HELP_CATEGORIES; i++) {
        #ifdef __AVR__
            // AVR: Read from PROGMEM
            char nameBuf[16];
            strcpy_P(nameBuf, (char*)pgm_read_ptr(&HELP_CATEGORIES[i].name));
            if (streq(category, nameBuf)) {
                HelpPrinter printer = (HelpPrinter)pgm_read_ptr(&HELP_CATEGORIES[i].printer);
                printer();
                found = true;
                break;
            }
        #else
            // ESP32/other: Read from PROGMEM
            const char* name = (const char*)HELP_CATEGORIES[i].name;
            if (strcasecmp_P(category, name) == 0) {
                HELP_CATEGORIES[i].printer();
                found = true;
                break;
            }
        #endif
    }

    if (!found) {
        // Unknown category - show error and list valid categories
        msg.control.println();
        msg.control.print(F("ERROR: Unknown help category '"));
        msg.control.print(category);
        msg.control.println(F("'"));
        msg.control.println();
        msg.control.println(F("Available categories:"));
        for (uint8_t i = 0; i < NUM_HELP_CATEGORIES; i++) {
            msg.control.print(F("  "));
            #ifdef __AVR__
                char nameBuf[16];
                strcpy_P(nameBuf, (char*)pgm_read_ptr(&HELP_CATEGORIES[i].name));
                msg.control.println(nameBuf);
            #else
                msg.control.println(HELP_CATEGORIES[i].name);
            #endif
        }
        msg.control.println(F("  QUICK"));
        msg.control.println();
    }
}

#endif // USE_STATIC_CONFIG
