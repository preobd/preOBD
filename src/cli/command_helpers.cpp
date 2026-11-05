/*
 * command_helpers.cpp - Helper functions and help system
 * Extracted from serial_config_old.cpp
 */

#include "../config.h"

#include "command_helpers.h"
#include "command_table.h"  // for Subcommand
#include "command_handlers.h"  // cmd_relay (HELP RELAY reuses its pointer)
#include "../config.h"
#include "../core/message_router.h"
#include "../core/message_api.h"
#include "../storage/system_config.h"
#include "../core/platform.h"
#include "../pinctrl/board_pins.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../sensors/units_registry.h"
#include "../drivers/bus_manager.h"
#include "../hal/bus_defaults.h"
#include "../pinctrl/pin_registry.h"
#include "../pinctrl/pin_owner.h"
#include "../inputs/input.h"  // For Input struct definition
#include "../inputs/input_manager.h"  // For inputs[] array access
#include <string.h>
#include <ctype.h>


// Reject putting a client on a bus whose pins another function already holds.
//
// These are the CLI's two entry points into the one predicate that also guards
// every bus bring-up at boot (pin_owner.cpp) — `DISPLAY TYPE LCD` / `BUS I2C <n>`
// and `BUS SPI <n>` cannot drift from it, nor from each other.
//
// This is about the PHYSICAL pins only. Other I2C devices are virtual pins
// (0xF0+), so sharing a bus — an LCD daisy-chained alongside a BME280 — is the
// normal multi-drop case and must NOT be rejected; neither is the bus's own
// PIN_RESERVED entry a conflict when the bus is already up. What is rejected is
// a competing electrical function on the same copper: an analog input on
// SDA/SCL, or a relay Action driving one.
//
// @return true if a conflict was found (message already printed)
bool i2cBusPinsConflict(uint8_t bus_id) {
    if (bus_id >= NUM_I2C_BUSES) return false;   // caller reports the bad bus id

    const uint8_t pins[2] = { getDefaultI2CSDA(bus_id), getDefaultI2CSCL(bus_id) };
    return pinsConflict(pins, 2, PIN_RANK_BUS, getI2CBusName(bus_id));
}

bool spiBusPinsConflict(uint8_t bus_id) {
    if (bus_id >= NUM_SPI_BUSES) return false;   // caller reports the bad bus id

    const uint8_t pins[3] = { getDefaultSPIMOSI(bus_id),
                              getDefaultSPIMISO(bus_id),
                              getDefaultSPISCK(bus_id) };
    return pinsConflict(pins, 3, PIN_RANK_BUS, getSPIBusName(bus_id));
}

#if NUM_CAN_BUSES > 0
// The CAN twin of the two above (#563). Re-selecting a bus that is already up,
// or that the other CAN direction has already selected, is not a conflict:
// pinsConflict() skips the bus's own PIN_RESERVED entry and storedBusHoldsPin()
// skips its own name, so input and output can share one bus.
// SPI-attached controllers have no TX/RX of their own (0xFF), so nothing to check.
bool canBusPinsConflict(uint8_t bus_id) {
    if (bus_id >= NUM_CAN_BUSES) return false;   // caller reports the bad bus id

    const uint8_t pins[2] = { getDefaultCANTX(bus_id), getDefaultCANRX(bus_id) };
    return pinsConflict(pins, 2, PIN_RANK_BUS, getCANBusName(bus_id));
}
#endif

void printSaveReminder() {
    msg.control.println(F("  (use SAVE to persist)"));
}

void printSubcommandTable(const Subcommand* table, uint8_t n) {
    // Each entry's help string is a self-contained usage block: it bakes in
    // the parent prefix and may span multiple lines so leaves with sub-forms
    // (BUS CAN, BUS SERIAL, SYSTEM DUMP, SYSTEM UNITS, ...) can document
    // every supported shape. We print it verbatim and let the help author
    // own the formatting.
    for (uint8_t i = 0; i < n; i++) {
        const char* help = (const char*)pgm_read_ptr(&table[i].help);
        if (!help) continue;
        msg.control.println((const __FlashStringHelper*)help);
    }
}

static void printInputFieldPrefix(const char* pinArg,
                                  const __FlashStringHelper* field) {
    msg.control.print(F("Input "));
    msg.control.print(pinArg);
    msg.control.print(' ');
    msg.control.print(field);
    msg.control.print(F(" set to "));
}

void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        const char* value) {
    printInputFieldPrefix(pinArg, field);
    msg.control.println(value);
    printSaveReminder();
}

void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        const __FlashStringHelper* value) {
    printInputFieldPrefix(pinArg, field);
    msg.control.println(value);
    printSaveReminder();
}

void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        float value, uint8_t decimals) {
    printInputFieldPrefix(pinArg, field);
    msg.control.println(value, decimals);
    printSaveReminder();
}

void printInputFlag(const char* pinArg, const __FlashStringHelper* facet,
                    bool enabled) {
    msg.control.print(F("Input "));
    msg.control.print(pinArg);
    msg.control.print(' ');
    msg.control.print(facet);
    msg.control.println(enabled ? F(" enabled") : F(" disabled"));
    printSaveReminder();
}

int validateDispatchEntry(const __FlashStringHelper* group, uint8_t index,
                          const char* token, const void* handler,
                          bool tokenInProgmem) {
    auto fail = [&](const __FlashStringHelper* reason) {
        msg.control.print(F("  FAIL "));
        msg.control.print(group);
        msg.control.print('[');
        msg.control.print(index);
        msg.control.print(F("]: "));
        msg.control.println(reason);
        return 1;
    };

    if (!token)   return fail(F("null token"));
    if (!handler) return fail(F("null handler"));

    size_t len = tokenInProgmem ? strlen_P(token) : strlen(token);
    if (len < 1 || len > 24) return fail(F("bad token length"));

    for (size_t k = 0; k < len; k++) {
        char c = tokenInProgmem ? (char)pgm_read_byte(token + k) : token[k];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '?';
        if (!ok) return fail(F("non-uppercase token (PROGMEM access broken?)"));
    }
    return 0;
}

bool streq_P(const char* a, const char* b_P) {
    if (!a || !b_P) return false;
    while (*a) {
        char bc = (char)pgm_read_byte(b_P);
        if (bc == '\0') return false;
        if (toupper(*a) != toupper(bc)) return false;
        a++;
        b_P++;
    }
    return pgm_read_byte(b_P) == '\0';
}

const __FlashStringHelper* measurementTypeName(MeasurementType type) {
    switch (type) {
        case MEASURE_TEMPERATURE: return F("TEMPERATURE");
        case MEASURE_PRESSURE:    return F("PRESSURE");
        case MEASURE_VOLTAGE:     return F("VOLTAGE");
        case MEASURE_RPM:         return F("RPM");
        case MEASURE_SPEED:       return F("SPEED");
        case MEASURE_HUMIDITY:    return F("HUMIDITY");
        case MEASURE_ELEVATION:   return F("ELEVATION");
        case MEASURE_DIGITAL:     return F("DIGITAL");
        case MEASURE_PERCENT:     return F("PERCENT");
        case MEASURE_DISTANCE:    return F("DISTANCE");
        case MEASURE_TIME:        return F("TIME");
        case MEASURE_MASS_FLOW:   return F("MASS_FLOW");
        case MEASURE_FUEL_RATE:   return F("FUEL_RATE");
    }
    return nullptr;
}

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

static const char PSTR_HELP_CONTROL[] PROGMEM = "CONTROL";
static const char PSTR_HELP_CONTROL_DESC[] PROGMEM = "Input commands - CLEAR, INFO (use SET <pin> INPUT for ENABLE/DISABLE)";

static const char PSTR_HELP_OUTPUT[] PROGMEM = "OUTPUT";
static const char PSTR_HELP_OUTPUT_DESC[] PROGMEM = "Output Modules - Configure CAN, RealDash, Serial, and SD logging";

static const char PSTR_HELP_BUS[] PROGMEM = "BUS";
static const char PSTR_HELP_BUS_DESC[] PROGMEM = "Bus Config - Configure I2C, SPI, and CAN buses";

#if INCLUDE_RELAY_OUTPUT
static const char PSTR_HELP_RELAY[] PROGMEM = "RELAY";
static const char PSTR_HELP_RELAY_DESC[] PROGMEM = "Deprecated - relays are now RULE / ACTION";
#endif

#if INCLUDE_TEST_MODE
static const char PSTR_HELP_TEST[] PROGMEM = "TEST";
static const char PSTR_HELP_TEST_DESC[] PROGMEM = "Test Mode - Simulate sensor inputs with predefined scenarios";
#endif

static const char PSTR_HELP_DISPLAY[] PROGMEM = "DISPLAY";
static const char PSTR_HELP_DISPLAY_DESC[] PROGMEM = "Display Config - LCD settings";

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
void printHelpControl();
void printHelpOutput();
void printHelpBus();
#if INCLUDE_RELAY_OUTPUT
void printHelpRelay();
#endif
#if INCLUDE_TEST_MODE
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
    {PSTR_HELP_CONTROL, PSTR_HELP_CONTROL_DESC, printHelpControl},
    {PSTR_HELP_OUTPUT, PSTR_HELP_OUTPUT_DESC, printHelpOutput},
    {PSTR_HELP_BUS, PSTR_HELP_BUS_DESC, printHelpBus},
#if INCLUDE_RELAY_OUTPUT
    {PSTR_HELP_RELAY, PSTR_HELP_RELAY_DESC, printHelpRelay},
#endif
#if INCLUDE_TEST_MODE
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

#if INCLUDE_OLED
extern const __FlashStringHelper* oledPanelState();
#endif

void printDisplayPanel() {
    if (systemConfig.displayType == DISPLAY_OLED) {
        // The OLED's address is fixed (the FeatherWing can't change it).
        msg.control.println(F("OLED I2C Address: 0x3C"));
        msg.control.print(F("OLED Panel: "));
#if INCLUDE_OLED
        msg.control.println(oledPanelState());
#else
        msg.control.println(F("not supported by this firmware"));
#endif
    } else if (systemConfig.displayType == DISPLAY_LCD) {
        msg.control.print(F("LCD I2C Address: 0x"));
        if (systemConfig.lcdI2CAddress < 0x10) msg.control.print(F("0"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
    }
}

bool displayI2CPinsConflict() {
    // The display acquires an I2C bus at the next boot, which reserves
    // SDA/SCL. Refuse now if another function holds one of those pins, so the
    // clash surfaces as an error instead of a silently dropped input at the
    // next reboot.
    uint8_t bus = (systemConfig.buses.active_i2c != 0xFF)
                      ? systemConfig.buses.active_i2c
                      : (uint8_t)DEFAULT_I2C_BUS;
    return i2cBusPinsConflict(bus);
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

    printDisplayPanel();

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

// printHelpSet() — defined in cmd_set.cpp where SET_FIELDS lives.

void printHelpControl() {
    msg.control.println();
    msg.control.println(F("=== CONTROL Commands ==="));
    msg.control.println(F("Inspect, clear, and query input status"));
    msg.control.println();
    msg.control.println(F("  SET <pin> INPUT ENABLE   - Enable input reading"));
    msg.control.println(F("  SET <pin> INPUT DISABLE  - Disable input reading"));
    msg.control.println(F("  CLEAR <pin>              - Reset input to unconfigured"));
    msg.control.println(F("  CLEAR <pin> CALIBRATION  - Clear custom calibration only"));
    msg.control.println(F("  CLEAR <pin> [CALIBRATION] SAVE - Clear, then SAVE (whole config)"));
    msg.control.println(F("  INFO <pin>               - Show detailed pin info"));
    msg.control.println(F("  INFO <pin> ALARM         - Show alarm status and configuration"));
    msg.control.println(F("  INFO <pin> CALIBRATION   - Show calibration details"));
#if INCLUDE_RULES_ENGINE
    msg.control.println();
    msg.control.println(F("  ALARM STATUS             - Severity, acknowledged alarms, silence policy"));
    msg.control.println(F("  ALARM SILENCE            - Acknowledge sounding alarm(s) (= mode button)"));
    msg.control.println(F("  ALARM DURATION <s|CLEAR> - Re-arm after 5..630 s, or hold until it clears"));
    msg.control.println(F("  ALARM SCOPE <BUZZER|ALL> - ALL: acknowledged alarm also stops LED blink"));
#endif
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

// printHelpBus() — defined in cmd_bus.cpp where BUS_SUBCOMMANDS lives.

#if INCLUDE_RELAY_OUTPUT
void printHelpRelay() {
    msg.control.println();
    cmd_relay(0, nullptr);  // same migration pointer the deprecated command prints
    msg.control.println();
}
#endif

#if INCLUDE_TEST_MODE
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
#if INCLUDE_OLED
    msg.control.println(F("  DISPLAY TYPE <LCD|OLED|NONE>"));
#else
    msg.control.println(F("  DISPLAY TYPE <LCD|NONE>"));
#endif
    msg.control.println(F("  DISPLAY ADDRESS <hex>    - LCD I2C address, 0x20-0x27 or 0x38-0x3F (at next boot)"));
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
    msg.control.println(F("  TRANSPORT <plane> <transport>  - Set primary transport"));
    msg.control.println(F("  TRANSPORT <plane> <transport> SECONDARY  - Set secondary"));
    msg.control.println();
    msg.control.println(F("  Planes: CONTROL, DATA, DEBUG"));
    msg.control.println(F("  (Use LIST TRANSPORTS to see available transports)"));
    msg.control.println();
    msg.control.println(F("  Both primary and secondary are polled for input and"));
    msg.control.println(F("  receive all output, enabling simultaneous control from"));
    msg.control.println(F("  two ports (e.g. USB + Bluetooth HM-10)."));
    msg.control.println();
    msg.control.println(F("  Example:"));
    msg.control.println(F("    TRANSPORT CONTROL SERIAL7"));
    msg.control.println(F("    TRANSPORT CONTROL USB_SERIAL SECONDARY"));
    msg.control.println(F("    SAVE"));
    msg.control.println();
    msg.control.println(F("  Clear secondary: TRANSPORT CONTROL NONE SECONDARY"));
    msg.control.println();
}

// printHelpSystem() — defined in cmd_system.cpp where SYSTEM_SUBCOMMANDS lives.

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
    msg.control.println(F("CAN sensor import (OBD-II, custom):"));
    msg.control.println(F("  SET CAN 0x0C  (import Engine RPM from CAN bus)"));
    msg.control.println(F("  SET CAN 0x0D  (import Vehicle Speed)"));
    msg.control.println(F("  SET CAN 0x05 0x7E9  (from a second ECU; SCAN PIDS lists them)"));
    msg.control.println(F("  SET CAN 0x78 SENSOR 3  (EGT bank 1 sensor 3)"));
    msg.control.println(F("  SET CAN:0 ALARM 500 6000  (modify CAN sensor)"));
    msg.control.println();
    msg.control.println(F("Advanced sensor setup:"));
    msg.control.println(F("  SET 2 CAL SPEED 100 2008 3.73 2000 300  (Hall sensor speed)"));
    msg.control.println(F("  SET 3 ENGINE_RPM W_PHASE_RPM  (alternator RPM)"));
    msg.control.println(F("  SET A1 CAL PRESSURE_LINEAR 0.5 4.5 0 7  (custom pressure)"));
    msg.control.println(F("  SET A0 CAL BIAS 4700  (change bias resistor)"));
    msg.control.println();
    msg.control.println(F("Alarm configuration:"));
    msg.control.println(F("  SET A2 ALARM 50 120  (set alarm thresholds)"));
    msg.control.println(F("  SET A2 ALARM MIN 1  (one-sided: alarm below 1)"));
    msg.control.println(F("  INFO A2 ALARM  (show alarm status)"));
    msg.control.println();
    msg.control.println(F("Output and control:"));
    msg.control.println(F("  SET A2 INPUT ENABLE"));
    msg.control.println(F("  OUTPUT CAN ENABLE"));
    msg.control.println(F("  OUTPUT CAN INTERVAL 100"));
    msg.control.println(F("  SAVE"));
    msg.control.println();
}

void printHelpOverview() {
    msg.control.println();
    msg.control.println(F("=== preOBD Command Reference ==="));
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
    msg.control.println(F("  HELP SET        - Show all SET commands (including CAL profiles)"));
    msg.control.println(F("  HELP CONTROL    - Show input enable/disable/clear commands"));
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
    msg.control.println(F("  SET <pin> INPUT ENABLE|DISABLE"));
    msg.control.println(F("  CLEAR <pin> [SAVE]"));
    msg.control.println(F("  CLEAR <pin> CALIBRATION [SAVE]"));
    msg.control.println(F("  INFO [<pin>] [ALARM|CALIBRATION]"));
    msg.control.println();
    msg.control.println(F("Input Configuration:"));
    msg.control.println(F("  SET <pin> <app> <sensor>"));
    msg.control.println(F("  SET <pin> APPLICATION <app>"));
    msg.control.println(F("  SET <pin> SENSOR <sensor>"));
    msg.control.println(F("  SET <pin> NAME <full_label>"));
    msg.control.println(F("  SET <pin> ABBR <abbreviation>"));
    msg.control.println(F("  SET <pin> UNITS <units>"));
    msg.control.println(F("  SET <pin> ALARM <min> <max>"));
    msg.control.println(F("  SET <pin> ALARM MIN|MAX <v>"));
    msg.control.println(F("  SET <pin> ALARM WARN <lo> [<hi>]"));
    msg.control.println(F("  SET <pin> ALARM DISABLE"));
    msg.control.println(F("  SET <pin> CAL PRESET"));
    msg.control.println(F("  SET <pin> CAL <STEINHART|BETA|RPM|SPEED|PRESSURE_LINEAR|PRESSURE_POLY|VOLTAGE_DIVIDER|BIAS> ..."));
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
#if INCLUDE_RULES_ENGINE
    msg.control.println();
    msg.control.println(F("Rules & Actions (relays):"));
    msg.control.println(F("  RULE LIST|<id>"));
    msg.control.println(F("  RULE ADD <pin> ABOVE|BELOW <limit> [HYST <w>]"));
    msg.control.println(F("  ACTION LIST|<id>"));
    msg.control.println(F("  ACTION ADD RELAY <pin> [FAILSAFE ON|OFF] [INVERT]"));
    msg.control.println(F("  ACTION <id> RULE <ruleId>"));
    msg.control.println(F("  ACTION <id> ON|OFF|AUTO"));
    msg.control.println(F("  ALARM STATUS|SILENCE"));
#endif
#if INCLUDE_TEST_MODE
    msg.control.println();
    msg.control.println(F("Test Mode:"));
    msg.control.println(F("  TEST LIST|STATUS|STOP"));
    msg.control.println(F("  TEST <0-4>"));
#endif
    msg.control.println();
    msg.control.println(F("Display:"));
    msg.control.println(F("  DISPLAY STATUS"));
    msg.control.println(F("  DISPLAY ENABLE|DISABLE"));
#if INCLUDE_OLED
    msg.control.println(F("  DISPLAY TYPE <LCD|OLED|NONE>"));
#else
    msg.control.println(F("  DISPLAY TYPE <LCD|NONE>"));
#endif
    msg.control.println(F("  DISPLAY ADDRESS <hex>"));
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
    msg.control.println(F("  SYSTEM REBOOT"));
    msg.control.println(F("  SYSTEM RESET CONFIRM"));
    msg.control.println();
    msg.control.println(F("Configuration:"));
    msg.control.println(F("  CONFIG|RUN"));
    msg.control.println(F("  SAVE [EEPROM|[dest:]file]"));
    msg.control.println(F("  LOAD [EEPROM|[dest:]file]"));
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

    // Handle "I2C" keyword for I2C sensors (BME280, etc).
    // Virtual pins are PIN_I2C_BASE..PIN_I2C_MAX (13 slots, well above real
    // pin numbers) — see the PIN ALLOCATION block in input.h, the single
    // source of truth for this range (#368).
    //
    // Source of truth is the inputs[] array, NOT a static counter. A static
    // counter advances on every parsePin() call regardless of whether the
    // command went on to allocate a slot — so a typo like
    // `SET I2C SENSOR DOESNOTEXIST` would burn a virtual pin number, and
    // 13 such typos would falsely report "too many I2C sensors" until reboot
    // (#196).
    if (streq(pinStr, "I2C")) {
        uint8_t highest = PIN_I2C_BASE - 1;  // start before I2C range
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].pin >= PIN_I2C_BASE && inputs[i].pin <= PIN_I2C_MAX) {
                if (inputs[i].pin > highest) highest = inputs[i].pin;
            }
        }
        uint8_t next = highest + 1;
        if (next > PIN_I2C_MAX) {
            msg.control.println(F("ERROR: Too many I2C sensors configured (max 13)"));
            if (isValid) *isValid = false;
            return 0;
        }
        return next;
    }

    // Handle "I2C:n" format for referencing existing I2C sensors (e.g., "I2C:0", "I2C:1")
    if (strncmp(pinStr, "I2C:", 4) == 0 || strncmp(pinStr, "i2c:", 4) == 0) {
        const char* numStr = pinStr + 4;
        int i2cIndex = atoi(numStr);

        // Validate I2C index range (0-12 for 13 total I2C sensors). MUST agree
        // with the "I2C" allocator branch above (PIN_I2C_BASE..PIN_I2C_MAX) —
        // a prior mismatch let this branch accept index 13 (pin 0xFD) even
        // though the allocator could never hand that pin out (#368).
        if (i2cIndex < 0 || i2cIndex > (PIN_I2C_MAX - PIN_I2C_BASE)) {
            msg.control.print(F("ERROR: I2C index "));
            msg.control.print(i2cIndex);
            msg.control.println(F(" out of range (valid: 0-12)"));
            if (isValid) *isValid = false;
            return 0;
        }

        // Convert I2C index to virtual pin number
        uint8_t virtualPin = PIN_I2C_BASE + i2cIndex;
        return virtualPin;
    }

    // Handle "CAN" keyword for CAN-imported sensors (OBD-II, J1939, custom).
    // Virtual pins are 0xC0..0xDF (32 slots).
    //
    // Same approach as the I2C branch above: scan inputs[] for the highest
    // allocated CAN pin and return the next slot. The previous implementation
    // kept a static counter that advanced on every parse, which leaked slots
    // on failed commands (#196) — the rebase loop only fired when at least
    // one CAN slot was already allocated, so a fresh boot followed by typos
    // would still burn through the range.
    if (streq(pinStr, "CAN")) {
        uint8_t highest = 0xBF;  // start before CAN range
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].pin >= 0xC0 && inputs[i].pin < 0xE0) {
                if (inputs[i].pin > highest) highest = inputs[i].pin;
            }
        }
        uint8_t next = highest + 1;
        if (next >= 0xE0) {
            msg.control.println(F("ERROR: Too many CAN sensors configured (max 32)"));
            if (isValid) *isValid = false;
            return 0;
        }
        return next;
    }

    // Handle "CAN:n" format for referencing existing CAN sensors (e.g., "CAN:0", "CAN:1")
    if (strncmp(pinStr, "CAN:", 4) == 0 || strncmp(pinStr, "can:", 4) == 0) {
        const char* numStr = pinStr + 4;
        int canIndex = atoi(numStr);

        // Validate CAN index range (0-31 for 32 total CAN sensors)
        if (canIndex < 0 || canIndex > 31) {
            msg.control.print(F("ERROR: CAN index "));
            msg.control.print(canIndex);
            msg.control.println(F(" out of range (valid: 0-31)"));
            if (isValid) *isValid = false;
            return 0;
        }

        // Convert CAN index to virtual pin number
        uint8_t virtualPin = 0xC0 + canIndex;
        return virtualPin;
    }

    // Analog pins. A<n> -> physical pin via the shared analog map, which
    // resolves through the core A* constants so non-contiguous Teensy A-names
    // (4.1 A14..A17 = pins 38..41, not A0+14; 3.x worse) land on the right pin
    // instead of `A0 + n` (#302).
    if (toupper(pinStr[0]) == 'A') {
        int analogNum = atoi(pinStr + 1);
        if (analogNum < 0 || analogNum >= (int)analogPinCount()) {
            if (isValid) *isValid = false;
            msg.control.print(F("ERROR: Pin A"));
            msg.control.print(analogNum);
            msg.control.print(F(" not available (this board supports A0-A"));
            msg.control.print(analogPinCount() - 1);
            msg.control.println(F(")"));
            return 0;
        }
        return analogPinForIndex((uint8_t)analogNum);
    }

    // Digital pins: any pin the core says this board has (board_pins.h, #570).
    // Read as int first, so "300" isn't narrowed to pin 44.
    const int num = atoi(pinStr);
    if (num < 0 || num > 0xFF || !boardHasPin((uint8_t)num)) {
        if (isValid) *isValid = false;
        msg.control.print(F("ERROR: Pin "));
        msg.control.print(num);
        msg.control.println(F(" does not exist on this board"));
        return 0;
    }
    const uint8_t pin = (uint8_t)num;

    return pin;
}

/**
 * Reset virtual pin allocation counters.
 *
 * No-op since the static counters were removed in the #196 fix — parsePin()
 * for CAN/I2C now always scans inputs[] for the highest allocated slot, so
 * there is no internal state to reset. Kept as an exported symbol in case
 * callers exist elsewhere; safe to remove if no callers materialize.
 */
void resetVirtualPinCounters() {
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
