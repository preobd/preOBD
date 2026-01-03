/*
 * serial_config.cpp - Serial Command Interface Implementation
 * NO String class - uses char* and fixed buffers for minimal RAM usage
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#include "../config.h"
#include "../version.h"

#ifndef USE_STATIC_CONFIG

#include "serial_config.h"
#include "input_manager.h"
#include "input.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/platform.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include "../lib/json_config.h"
#include "../outputs/output_base.h"
#ifdef ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif
#ifdef ENABLE_TEST_MODE
#include "../test/test_mode.h"
#endif
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include <string.h>
#include <ctype.h>

// AVR watchdog for system reset
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    #include <avr/wdt.h>
#endif

// Display control functions
extern void enableLCD();
extern void disableLCD();

// Display manager functions
#include "../lib/display_manager.h"

// Command buffer - fixed size, no String class
#define CMD_BUFFER_SIZE 80
static char commandBuffer[CMD_BUFFER_SIZE];
static uint8_t cmdIndex = 0;

// Helper: trim whitespace in-place
static void trim(char* str) {
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
static void toUpper(char* str) {
    while (*str) {
        *str = toupper(*str);
        str++;
    }
}

// Helper: case-insensitive string compare
static bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(*a) != toupper(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Helper: Parse message plane name
static MessagePlane parsePlane(const char* str) {
    if (streq(str, "CONTROL")) return PLANE_CONTROL;
    if (streq(str, "DATA")) return PLANE_DATA;
    if (streq(str, "DEBUG")) return PLANE_DEBUG;
    return NUM_PLANES;  // Invalid
}

// Helper: Parse transport ID
static TransportID parseTransport(const char* str) {
    if (streq(str, "USB_SERIAL") || streq(str, "USB") || streq(str, "SERIAL")) return TRANSPORT_USB_SERIAL;
    if (streq(str, "SERIAL1")) return TRANSPORT_SERIAL1;
    if (streq(str, "SERIAL2")) return TRANSPORT_SERIAL2;
    if (streq(str, "SERIAL3")) return TRANSPORT_SERIAL3;
    if (streq(str, "ESP32_BT") || streq(str, "ESP32") || streq(str, "ESP32BT")) return TRANSPORT_ESP32_BT;
    if (streq(str, "NONE")) return TRANSPORT_NONE;
    return NUM_TRANSPORTS;  // Invalid
}

// Helper: Get plane name
static const char* getPlaneName(MessagePlane plane) {
    switch (plane) {
        case PLANE_CONTROL: return "CONTROL";
        case PLANE_DATA: return "DATA";
        case PLANE_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

// Helper: Get transport name
static const char* getTransportName(TransportID id) {
    switch (id) {
        case TRANSPORT_NONE: return "NONE";
        case TRANSPORT_USB_SERIAL: return "USB_SERIAL";
        case TRANSPORT_SERIAL1: return "SERIAL1";
        case TRANSPORT_SERIAL2: return "SERIAL2";
        case TRANSPORT_SERIAL3: return "SERIAL3";
        case TRANSPORT_ESP32_BT: return "ESP32_BT";
        default: return "UNKNOWN";
    }
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
static const char PSTR_HELP_CALIBRATION_DESC[] PROGMEM = "Advanced - Custom sensor calibration (RPM, speed, pressure, temperature)";

static const char PSTR_HELP_CONTROL[] PROGMEM = "CONTROL";
static const char PSTR_HELP_CONTROL_DESC[] PROGMEM = "Input Control - Enable, disable, clear, and query input status";

static const char PSTR_HELP_OUTPUT[] PROGMEM = "OUTPUT";
static const char PSTR_HELP_OUTPUT_DESC[] PROGMEM = "Output Modules - Configure CAN, RealDash, Serial, and SD logging";

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
static void printHelpList();
static void printHelpSet();
static void printHelpCalibration();
static void printHelpControl();
static void printHelpOutput();
#ifdef ENABLE_RELAY_OUTPUT
static void printHelpRelay();
#endif
#ifdef ENABLE_TEST_MODE
static void printHelpTest();
#endif
static void printHelpDisplay();
static void printHelpTransport();
static void printHelpSystem();
static void printHelpConfig();
static void printHelpExamples();
static void printHelpOverview();
static void printHelpQuick();

// Help category registry (in flash memory)
static const HelpCategory HELP_CATEGORIES[] PROGMEM = {
    {PSTR_HELP_LIST, PSTR_HELP_LIST_DESC, printHelpList},
    {PSTR_HELP_SET, PSTR_HELP_SET_DESC, printHelpSet},
    {PSTR_HELP_CALIBRATION, PSTR_HELP_CALIBRATION_DESC, printHelpCalibration},
    {PSTR_HELP_CONTROL, PSTR_HELP_CONTROL_DESC, printHelpControl},
    {PSTR_HELP_OUTPUT, PSTR_HELP_OUTPUT_DESC, printHelpOutput},
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
static void printSystemConfig() {
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
}

// ===== HELP PRINTER FUNCTIONS =====

static void printHelpList() {
    msg.control.println();
    msg.control.println(F("=== LIST Commands ==="));
    msg.control.println(F("Discovery commands to explore available options"));
    msg.control.println();
    msg.control.println(F("  LIST INPUTS         - Show all configured inputs"));
    msg.control.println(F("  LIST APPLICATIONS   - Show available Type presets"));
    msg.control.println(F("  LIST SENSORS        - Show available Sensor Types"));
    msg.control.println();
}

static void printHelpSet() {
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
    msg.control.println(F("  SET <pin> WARMUP <ms>  - Override alarm warmup time"));
    msg.control.println(F("  SET <pin> PERSIST <ms>  - Override alarm persistence time"));
    msg.control.println();
    msg.control.println(F("See also: HELP CALIBRATION for advanced sensor calibration"));
    msg.control.println();
}

static void printHelpCalibration() {
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

static void printHelpControl() {
    msg.control.println();
    msg.control.println(F("=== CONTROL Commands ==="));
    msg.control.println(F("Enable, disable, clear, and query input status"));
    msg.control.println();
    msg.control.println(F("  ENABLE <pin>  - Enable input reading"));
    msg.control.println(F("  DISABLE <pin>  - Disable input reading"));
    msg.control.println(F("  CLEAR <pin>  - Reset input to unconfigured"));
    msg.control.println(F("  INFO <pin>  - Show detailed pin info"));
    msg.control.println(F("  INFO <pin> ALARM  - Show alarm status and configuration"));
    msg.control.println();
}

static void printHelpOutput() {
    msg.control.println();
    msg.control.println(F("=== OUTPUT Commands ==="));
    msg.control.println(F("Configure CAN, RealDash, Serial, and SD logging"));
    msg.control.println();
    msg.control.println(F("  OUTPUT LIST  - Show all output modules"));
    msg.control.println(F("  OUTPUT <name> ENABLE  - Enable output (CAN, RealDash, Serial, SD_Log)"));
    msg.control.println(F("  OUTPUT <name> DISABLE  - Disable output"));
    msg.control.println(F("  OUTPUT <name> INTERVAL <ms>  - Set output interval"));
    msg.control.println();
}

#ifdef ENABLE_RELAY_OUTPUT
static void printHelpRelay() {
    msg.control.println();
    msg.control.println(F("=== RELAY Commands ==="));
    msg.control.println(F("Threshold-based relay outputs for cooling fans, alarms, etc."));
    msg.control.println();
    msg.control.println(F("  RELAY LIST  - Show all relay status"));
    msg.control.println(F("  RELAY <0-1> STATUS  - Show specific relay"));
    msg.control.println(F("  RELAY <0-1> PIN <pin>  - Set relay output pin"));
    msg.control.println(F("  RELAY <0-1> INPUT <pin>  - Link relay to sensor input"));
    msg.control.println(F("  RELAY <0-1> THRESHOLD <on> <off>  - Set activation thresholds"));
    msg.control.println(F("  RELAY <0-1> MODE <AUTO_HIGH|AUTO_LOW|ON|OFF>  - Set relay mode"));
    msg.control.println(F("  RELAY <0-1> DISABLE  - Disable relay"));
    msg.control.println();
}
#endif

#ifdef ENABLE_TEST_MODE
static void printHelpTest() {
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

static void printHelpDisplay() {
    msg.control.println();
    msg.control.println(F("=== DISPLAY Commands ==="));
    msg.control.println(F("LCD/OLED settings and unit preferences"));
    msg.control.println();
    msg.control.println(F("  DISPLAY STATUS  - Show display configuration"));
    msg.control.println(F("  DISPLAY ENABLE  - Enable display"));
    msg.control.println(F("  DISPLAY DISABLE  - Disable display"));
    msg.control.println(F("  DISPLAY TYPE <LCD|OLED|NONE>  - Set display type"));
    msg.control.println(F("  DISPLAY LCD ADDRESS <hex>  - Set I2C address (e.g., 0x27)"));
    msg.control.println(F("  DISPLAY UNITS TEMP <C|F>  - Default temperature units"));
    msg.control.println(F("  DISPLAY UNITS PRESSURE <BAR|PSI|KPA>  - Default pressure units"));
    msg.control.println(F("  DISPLAY UNITS ELEVATION <M|FT>  - Default elevation units"));
    msg.control.println(F("  DISPLAY UNITS SPEED <KPH|MPH>  - Default speed units"));
    msg.control.println();
}

static void printHelpTransport() {
    msg.control.println();
    msg.control.println(F("=== TRANSPORT Commands ==="));
    msg.control.println(F("Route control, data, and debug messages"));
    msg.control.println();
    msg.control.println(F("  TRANSPORT LIST  - Show current transport assignments"));
    msg.control.println(F("  TRANSPORT CONTROL <transport>  - Route control messages"));
    msg.control.println(F("  TRANSPORT DATA <transport>  - Route sensor data output"));
    msg.control.println(F("  TRANSPORT DEBUG <transport>  - Route debug messages"));
    msg.control.println(F("    Transports: USB_SERIAL, SERIAL1, SERIAL2, SERIAL3, ESP32_BT"));
    msg.control.println();
}

static void printHelpSystem() {
    msg.control.println();
    msg.control.println(F("=== SYSTEM Commands ==="));
    msg.control.println(F("System configuration (advanced settings)"));
    msg.control.println();
    msg.control.println(F("  SYSTEM STATUS  - Show system configuration"));
    msg.control.println(F("  SYSTEM SEA_LEVEL <hPa>  - Sea level pressure"));
    msg.control.println(F("  SYSTEM INTERVAL SENSOR <ms>  - Sensor read interval"));
    msg.control.println(F("  SYSTEM INTERVAL ALARM <ms>  - Alarm check interval"));
    msg.control.println(F("  SYSTEM INTERVAL LCD <ms>  - LCD update interval"));
    msg.control.println();
}

static void printHelpConfig() {
    msg.control.println();
    msg.control.println(F("=== CONFIG Commands ==="));
    msg.control.println(F("Persistence, modes, and system control"));
    msg.control.println();
    msg.control.println(F("Persistence:"));
    msg.control.println(F("  SAVE  - Save config to EEPROM"));
    msg.control.println(F("  LOAD  - Load config from EEPROM"));
    msg.control.println(F("  RESET  - Clear all configuration"));
    msg.control.println();
    msg.control.println(F("Modes:"));
    msg.control.println(F("  CONFIG  - Enter configuration mode (unlock config)"));
    msg.control.println(F("  RUN  - Enter run mode (lock config, resume sensors)"));
    msg.control.println(F("  RELOAD  - Trigger watchdog reset (system reboot)"));
    msg.control.println();
    msg.control.println(F("Information:"));
    msg.control.println(F("  VERSION  - Display firmware and EEPROM version"));
    msg.control.println(F("  DUMP  - Show full configuration (human-readable)"));
    msg.control.println(F("  DUMP JSON  - Export configuration as JSON"));
    msg.control.println(F("  CONFIG SAVE [filename]  - Save configuration to SD card"));
    msg.control.println(F("  CONFIG LOAD <filename>  - Load configuration from SD card"));
    msg.control.println();
}

static void printHelpExamples() {
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
    msg.control.println(F("  SET A1 PRESSURE_LINEAR 0.5 4.5 0 7  (custom pressure)"));
    msg.control.println(F("  SET A0 BIAS 4700  (change bias resistor)"));
    msg.control.println(F("  SET A2 WARMUP 30000  (30 second warmup)"));
    msg.control.println(F("  SET A2 PERSIST 2000  (2 second persistence)"));
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

static void printHelpOverview() {
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
            // ESP32/other: Read from PROGMEM
            msg.control.print(HELP_CATEGORIES[i].name);
            // Pad to align descriptions (use strlen_P for PROGMEM strings)
            uint8_t nameLen = strlen_P(HELP_CATEGORIES[i].name);
            for (uint8_t j = nameLen; j < 14; j++) {
                msg.control.print(' ');
            }
            msg.control.print(F("- "));
            msg.control.println(HELP_CATEGORIES[i].description);
        #endif
    }

    msg.control.println();
    msg.control.println(F("Quick commands:"));
    msg.control.println(F("  HELP QUICK      - Compact command list"));
    msg.control.println(F("  ?               - Alias for HELP"));
    msg.control.println(F("  VERSION         - Firmware version"));
    msg.control.println(F("  DUMP            - Show full configuration"));
    msg.control.println();
    msg.control.println(F("Examples:"));
    msg.control.println(F("  HELP SET        - Show all SET commands"));
    msg.control.println(F("  HELP CALIBRATION - Show calibration commands"));
    msg.control.println();
}

static void printHelpQuick() {
    msg.control.println();
    msg.control.println(F("=== Quick Command Reference ==="));
    msg.control.println();
    msg.control.println(F("LIST: INPUTS | APPLICATIONS | SENSORS"));
    msg.control.println(F("SET: <pin> <app> <sensor> | APPLICATION | SENSOR | NAME | DISPLAY_NAME"));
    msg.control.println(F("     UNITS | ALARM [ENABLE|DISABLE|<min> <max>] | WARMUP | PERSIST"));
    msg.control.println(F("CALIBRATION: PRESET | RPM | SPEED | PRESSURE_LINEAR | STEINHART | BETA"));
    msg.control.println(F("             PRESSURE_POLY | BIAS"));
    msg.control.println(F("CONTROL: ENABLE | DISABLE | CLEAR | INFO [<pin>] [ALARM|CALIBRATION]"));
    msg.control.println(F("OUTPUT: LIST | <name> ENABLE/DISABLE/INTERVAL"));
#ifdef ENABLE_RELAY_OUTPUT
    msg.control.println(F("RELAY: LIST | <0-1> STATUS/PIN/INPUT/THRESHOLD/MODE/DISABLE"));
#endif
#ifdef ENABLE_TEST_MODE
    msg.control.println(F("TEST: LIST | <0-4> | STOP | STATUS"));
#endif
    msg.control.println(F("DISPLAY: STATUS | ENABLE/DISABLE | TYPE | LCD ADDRESS | UNITS"));
    msg.control.println(F("TRANSPORT: LIST | CONTROL/DATA/DEBUG <transport>"));
    msg.control.println(F("SYSTEM: STATUS | SEA_LEVEL | INTERVAL SENSOR/ALARM/LCD"));
    msg.control.println(F("CONFIG: SAVE | LOAD | RESET | CONFIG [SAVE|LOAD] | RUN | VERSION | DUMP"));
    msg.control.println(F("        RELOAD"));
    msg.control.println();
    msg.control.println(F("For detailed help: HELP <category>"));
    msg.control.println();
}

void initSerialConfig() {
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  openEMS Serial Configuration"));
    msg.control.println(F("  Type 'HELP' for commands"));
    msg.control.println(F("========================================"));
    msg.control.println();
    cmdIndex = 0;
    commandBuffer[0] = '\0';
}

/**
 * Handle incoming character input (called by MessageRouter)
 * This function is called character-by-character from router.update()
 */
void handleCommandInput(char c) {
    if (c == '\n' || c == '\r') {
        if (cmdIndex > 0) {
            commandBuffer[cmdIndex] = '\0';
            trim(commandBuffer);
            if (commandBuffer[0] != '\0') {
                handleSerialCommand(commandBuffer);
            }
            cmdIndex = 0;
            commandBuffer[0] = '\0';
            msg.control.print(F("\n> "));  // Prompt goes to active control transport
        }
    } else if (cmdIndex < CMD_BUFFER_SIZE - 1) {
        commandBuffer[cmdIndex++] = c;
    }
}

/**
 * Legacy function for backward compatibility
 * Now just a no-op since router.update() handles command polling
 */
void processSerialCommands() {
    // This function is deprecated - command input is now handled by
    // router.update() calling handleCommandInput()
    // Kept for backward compatibility, but does nothing
}

/**
 * Parse a pin string into a pin number.
 * Accepts "A0"-"A15" for analog pins, numeric strings for digital pins,
 * or "I2C" for I2C sensors (BME280, etc).
 */
static uint8_t parsePin(const char* pinStr, bool* isValid) {
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

// Parse Application index from string (registry-based)
static uint8_t parseApplication(const char* appStr) {
    return getApplicationIndexByName(appStr);
}

// Parse Sensor index from string (registry-based)
static uint8_t parseSensor(const char* sensorStr) {
    return getSensorIndexByName(sensorStr);
}

// Parse Units index from string (registry-based)
static uint8_t parseUnits(const char* unitsStr) {
    return getUnitsIndexByName(unitsStr);
}

void handleSerialCommand(char* cmd) {
    if (!cmd) return;

    trim(cmd);
    toUpper(cmd);

    // Parse command for multi-word commands (find first space)
    char* firstSpace = strchr(cmd, ' ');

    // ===== MODE COMMANDS (always available to prevent deadlock) =====
    if (streq(cmd, "CONFIG") || strncmp(cmd, "CONFIG ", 7) == 0) {
        // Check for CONFIG SAVE or CONFIG LOAD subcommands
        if (firstSpace) {
            char* subCmd = firstSpace + 1;
            toUpper(subCmd);

            if (strncmp(subCmd, "SAVE", 4) == 0) {
                // CONFIG SAVE [filename]
                char* filename = nullptr;
                char* filenameStart = subCmd + 4;
                while (*filenameStart == ' ') filenameStart++;  // Skip spaces
                if (*filenameStart != '\0') {
                    filename = filenameStart;
                }

                msg.control.println();
                if (saveConfigToSD(filename)) {
                    msg.control.println(F("Configuration saved successfully"));
                } else {
                    msg.control.println(F("ERROR: Failed to save configuration"));
                }
                msg.control.println();
                return;
            }

            if (strncmp(subCmd, "LOAD", 4) == 0) {
                // CONFIG LOAD <filename>
                char* filename = subCmd + 4;
                while (*filename == ' ') filename++;  // Skip spaces

                if (*filename == '\0') {
                    msg.control.println(F("ERROR: Filename required for CONFIG LOAD"));
                    return;
                }

                msg.control.println();
                if (loadConfigFromSD(filename)) {
                    msg.control.println(F("Configuration loaded successfully"));
                    msg.control.println(F("Type SAVE to persist to EEPROM"));
                } else {
                    msg.control.println(F("ERROR: Failed to load configuration"));
                }
                msg.control.println();
                return;
            }
        }

        // No subcommand or unknown subcommand - enter CONFIG mode
        setMode(MODE_CONFIG);
        return;
    }

    if (streq(cmd, "RUN")) {
        setMode(MODE_RUN);
        return;
    }

    // ===== COMMAND GATING (based on current mode) =====
    // In RUN mode, only allow read-only commands
    if (isInRunMode()) {
        // Read-only commands allowed in RUN mode
        bool isReadOnly =
            strncmp(cmd, "HELP", 4) == 0 ||  // HELP, HELP CONFIG, etc.
            streq(cmd, "?") ||
            streq(cmd, "VERSION") ||
            streq(cmd, "DUMP") ||
            streq(cmd, "INFO") ||
            strncmp(cmd, "LIST", 4) == 0 ||  // LIST, LIST INPUTS, etc.
#ifdef ENABLE_TEST_MODE
            strncmp(cmd, "TEST", 4) == 0 ||  // TEST, TEST LIST, etc.
#endif
            false;  // Sentinel

        if (!isReadOnly) {
            msg.control.println();
            msg.control.println(F("========================================"));
            msg.control.println(F("  ERROR: Configuration locked in RUN mode"));
            msg.control.println(F("  Type CONFIG to enter configuration mode"));
            msg.control.println(F("========================================"));
            msg.control.println();
            return;
        }
    }

    // ===== HELP & INFO COMMANDS =====
    if (strncmp(cmd, "HELP", 4) == 0 || streq(cmd, "?")) {
        // Check for category argument
        if (firstSpace && cmd[4] == ' ') {  // Ensure it's "HELP " not "HELPABC"
            char* category = firstSpace + 1;
            trim(category);

            // Special case: HELP QUICK
            if (streq(category, "QUICK")) {
                printHelpQuick();
                return;
            }

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
        } else {
            // No argument - show overview
            printHelpOverview();
        }
        return;
    }

    // ===== LIST COMMANDS =====
    // Query available presets and show configured inputs
    if (streq(cmd, "LIST INPUTS")) {
        listAllInputs();
        return;
    }

    if (streq(cmd, "LIST APPLICATIONS")) {
        listApplicationPresets();
        return;
    }

    if (streq(cmd, "LIST SENSORS")) {
        listSensors();
        return;
    }

    // ===== SET COMMANDS =====
    // Modify input configuration - Syntax: SET <pin> <field> <value>
    if (strncmp(cmd, "SET ", 4) == 0) {
        char* rest = cmd + 4;
        trim(rest);

        // Find first space to separate pin from field
        char* firstSpace = strchr(rest, ' ');
        if (!firstSpace) {
            msg.control.println(F("ERROR: Invalid SET syntax"));
            msg.control.println(F("  Usage: SET <pin> <field> <value>"));
            msg.control.println(F("  Example: SET A0 APPLICATION CHT"));
            return;
        }

        // Extract pin string
        *firstSpace = '\0';
        char* pinStr = rest;
        char* fieldAndValue = firstSpace + 1;
        trim(fieldAndValue);

        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;

        // Try combined syntax: SET <pin> <application> <sensor>
        // Example: SET 6 CHT MAX6675
        char* firstToken = fieldAndValue;
        char* secondSpace = strchr(firstToken, ' ');
        if (secondSpace) {
            *secondSpace = '\0';
            char* secondToken = secondSpace + 1;
            trim(secondToken);

            uint8_t appIndex = parseApplication(firstToken);
            uint8_t sensorIndex = parseSensor(secondToken);

            if (appIndex != 0 && sensorIndex != 0) {  // 0 = NONE
                // Valid combined command
                // Check sensor/application compatibility
                MeasurementType sensorMeasType = getSensorMeasurementType(sensorIndex);
                MeasurementType appMeasType = getApplicationExpectedMeasurementType(appIndex);

                if (sensorMeasType != appMeasType) {
                    msg.control.print(F("ERROR: Sensor/application type mismatch - "));
                    msg.control.print(secondToken);
                    msg.control.print(F(" measures "));
                    // Print measurement type names
                    switch(sensorMeasType) {
                        case MEASURE_TEMPERATURE: msg.control.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: msg.control.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: msg.control.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: msg.control.print(F("RPM")); break;
                        case MEASURE_SPEED: msg.control.print(F("SPEED")); break;
                        case MEASURE_HUMIDITY: msg.control.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: msg.control.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: msg.control.print(F("DIGITAL")); break;
                    }
                    msg.control.print(F(" but "));
                    msg.control.print(firstToken);
                    msg.control.print(F(" expects "));
                    switch(appMeasType) {
                        case MEASURE_TEMPERATURE: msg.control.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: msg.control.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: msg.control.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: msg.control.print(F("RPM")); break;
                        case MEASURE_SPEED: msg.control.print(F("SPEED")); break;
                        case MEASURE_HUMIDITY: msg.control.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: msg.control.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: msg.control.print(F("DIGITAL")); break;
                    }
                    msg.control.println();
                    return;  // Reject the configuration
                }

                // First set application (which also calls setInputSensor with preset sensor)
                if (setInputApplication(pin, appIndex)) {
                    // Then override sensor if different from preset
                    Input* input = getInputByPin(pin);
                    if (input && input->sensorIndex != sensorIndex) {
                        setInputSensor(pin, sensorIndex);
                    }

                    msg.control.print(F("Input "));
                    msg.control.print(pinStr);
                    msg.control.print(F(" configured as "));
                    msg.control.print(firstToken);
                    msg.control.print(F(" with "));
                    msg.control.println(secondToken);
                }
                return;
            }

            // Restore space for fallthrough to existing handlers
            *secondSpace = ' ';
        }

        // SET <pin> APPLICATION <application>
        if (strncmp(fieldAndValue, "APPLICATION ", 12) == 0) {
            char* appStr = fieldAndValue + 12;
            trim(appStr);
            uint8_t appIndex = parseApplication(appStr);
            if (appIndex == 0) {  // 0 = APP_NONE
                msg.control.print(F("ERROR: Unknown application '"));
                msg.control.print(appStr);
                msg.control.println(F("'"));
                msg.control.println(F("  Hint: Use 'LIST APPLICATIONS' to see valid options"));
                return;
            }
            if (setInputApplication(pin, appIndex)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" configured as "));
                msg.control.println(appStr);
            }
            return;
        }

        // SET <pin> SENSOR <sensor>
        if (strncmp(fieldAndValue, "SENSOR ", 7) == 0) {
            char* sensorStr = fieldAndValue + 7;
            trim(sensorStr);
            uint8_t sensorIndex = parseSensor(sensorStr);
            if (sensorIndex == 0) {  // 0 = SENSOR_NONE
                msg.control.print(F("ERROR: Unknown sensor '"));
                msg.control.print(sensorStr);
                msg.control.println(F("'"));
                msg.control.println(F("  Hint: Use 'LIST SENSORS' to see valid options"));
                return;
            }
            if (setInputSensor(pin, sensorIndex)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" sensor set to "));
                msg.control.println(sensorStr);
            }
            return;
        }

        // SET <pin> NAME <name>
        if (strncmp(fieldAndValue, "NAME ", 5) == 0) {
            char* name = fieldAndValue + 5;
            trim(name);
            if (setInputName(pin, name)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" name set to "));
                msg.control.println(name);
            }
            return;
        }

        // SET <pin> DISPLAY_NAME <name>
        if (strncmp(fieldAndValue, "DISPLAY_NAME ", 13) == 0) {
            char* name = fieldAndValue + 13;
            trim(name);
            if (setInputDisplayName(pin, name)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" display name set to "));
                msg.control.println(name);
            }
            return;
        }

        // SET <pin> UNITS <units>
        if (strncmp(fieldAndValue, "UNITS ", 6) == 0) {
            char* unitsStr = fieldAndValue + 6;
            trim(unitsStr);
            uint8_t unitsIndex = parseUnits(unitsStr);
            if (setInputUnits(pin, unitsIndex)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" units set to "));
                msg.control.println(unitsStr);
            }
            return;
        }

        // SET <pin> ALARM ENABLE
        if (strncmp(fieldAndValue, "ALARM ENABLE", 12) == 0) {
            if (enableInputAlarm(pin, true)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.println(F(" alarm enabled"));
            }
            return;
        }

        // SET <pin> ALARM DISABLE
        if (strncmp(fieldAndValue, "ALARM DISABLE", 13) == 0) {
            if (enableInputAlarm(pin, false)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.println(F(" alarm disabled"));
            }
            return;
        }

        // SET <pin> ALARM <min> <max>
        if (strncmp(fieldAndValue, "ALARM ", 6) == 0) {
            char* values = fieldAndValue + 6;
            trim(values);
            char* spacePos = strchr(values, ' ');
            if (!spacePos) {
                msg.control.println(F("ERROR: ALARM requires min and max values"));
                return;
            }
            *spacePos = '\0';
            float minVal = atof(values);
            float maxVal = atof(spacePos + 1);
            if (setInputAlarmRange(pin, minVal, maxVal)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" alarm range set to "));
                msg.control.print(minVal);
                msg.control.print(F(" - "));
                msg.control.println(maxVal);
            }
            return;
        }

        // SET <pin> CALIBRATION PRESET
        // Clears custom calibration and reverts to sensor library preset
        if (strncmp(fieldAndValue, "CALIBRATION PRESET", 18) == 0) {
            Input* input = getInputByPin(pin);
            if (!input) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Clear custom calibration
            input->flags.useCustomCalibration = false;
            memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

            msg.control.print(F("Cleared custom calibration for pin "));
            msg.control.println(pinStr);
            msg.control.println(F("Using preset calibration from sensor library"));
            return;
        }

        // SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
        // Supports 5 parameters (mult defaults to 1.0) or 6 parameters (custom mult)
        if (strncmp(fieldAndValue, "RPM ", 4) == 0) {
            char* params = fieldAndValue + 4;
            trim(params);

            // Count tokens to determine if calibration_mult is provided
            char paramsCopy[80];
            strncpy(paramsCopy, params, sizeof(paramsCopy) - 1);
            paramsCopy[sizeof(paramsCopy) - 1] = '\0';

            int tokenCount = 0;
            char* token = strtok(paramsCopy, " ");
            while (token != nullptr && tokenCount < 7) {
                tokenCount++;
                token = strtok(nullptr, " ");
            }

            if (tokenCount != 5 && tokenCount != 6) {
                msg.control.println(F("ERROR: RPM requires 5 or 6 parameters"));
                msg.control.println(F("  Usage: SET <pin> RPM <poles> <ratio> <timeout> <min> <max>"));
                msg.control.println(F("     or: SET <pin> RPM <poles> <ratio> <mult> <timeout> <min> <max>"));
                msg.control.println(F("  Example: SET 5 RPM 12 3.0 2000 100 8000"));
                msg.control.println(F("       or: SET 5 RPM 12 3.0 1.02 2000 100 8000"));
                return;
            }

            // Parse parameters
            byte poles;
            float pulley_ratio;
            float calibration_mult = 1.0;  // Default
            uint16_t timeout_ms;
            uint16_t min_rpm;
            uint16_t max_rpm;

            token = strtok(params, " ");
            if (!token) { msg.control.println(F("ERROR: Missing poles")); return; }
            poles = (byte)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing pulley_ratio")); return; }
            pulley_ratio = atof(token);

            if (tokenCount == 6) {
                // 6 parameters: custom calibration_mult provided
                token = strtok(nullptr, " ");
                if (!token) { msg.control.println(F("ERROR: Missing calibration_mult")); return; }
                calibration_mult = atof(token);
            }

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing timeout_ms")); return; }
            timeout_ms = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing min_rpm")); return; }
            min_rpm = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing max_rpm")); return; }
            max_rpm = (uint16_t)atoi(token);

            // Validate parameters
            if (poles < 2 || poles > 32) {
                msg.control.println(F("ERROR: Poles must be between 2 and 32"));
                return;
            }
            if (pulley_ratio < 0.5 || pulley_ratio > 10.0) {
                msg.control.println(F("ERROR: Pulley ratio must be between 0.5 and 10.0"));
                return;
            }
            if (calibration_mult < 0.5 || calibration_mult > 2.0) {
                msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
                return;
            }
            if (timeout_ms < 100 || timeout_ms > 10000) {
                msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
                return;
            }
            if (min_rpm >= max_rpm) {
                msg.control.println(F("ERROR: min_rpm must be less than max_rpm"));
                return;
            }

            // Apply custom calibration
            Input* input = getInputByPin(pin);
            if (!input) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Set custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.rpm.poles = poles;
            input->customCalibration.rpm.pulley_ratio = pulley_ratio;
            input->customCalibration.rpm.calibration_mult = calibration_mult;
            input->customCalibration.rpm.timeout_ms = timeout_ms;
            input->customCalibration.rpm.min_rpm = min_rpm;
            input->customCalibration.rpm.max_rpm = max_rpm;

            msg.control.print(F("RPM calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Poles: "));
            msg.control.println(poles);
            msg.control.print(F("  Pulley Ratio: "));
            msg.control.print(pulley_ratio, 2);
            msg.control.println(F(":1"));
            msg.control.print(F("  Calibration Mult: "));
            msg.control.println(calibration_mult, 4);
            msg.control.print(F("  Timeout: "));
            msg.control.print(timeout_ms);
            msg.control.println(F(" ms"));
            msg.control.print(F("  Valid Range: "));
            msg.control.print(min_rpm);
            msg.control.print(F("-"));
            msg.control.print(max_rpm);
            msg.control.println(F(" RPM"));
            float effective_ppr = (poles / 2.0) * pulley_ratio * calibration_mult;
            msg.control.print(F("  Effective: "));
            msg.control.print(effective_ppr, 2);
            msg.control.println(F(" pulses/engine-rev"));
            return;
        }

        // SET <pin> SPEED <pulses_per_rev> <tire_circ_mm> <drive_ratio> [<mult>] <timeout> <max_speed>
        // Supports 5 parameters (mult defaults to 1.0) or 6 parameters (custom mult)
        if (strncmp(fieldAndValue, "SPEED ", 6) == 0) {
            char* params = fieldAndValue + 6;
            trim(params);

            // Count tokens to determine if calibration_mult is provided
            char paramsCopy[80];
            strncpy(paramsCopy, params, sizeof(paramsCopy) - 1);
            paramsCopy[sizeof(paramsCopy) - 1] = '\0';

            int tokenCount = 0;
            char* token = strtok(paramsCopy, " ");
            while (token != nullptr && tokenCount < 7) {
                tokenCount++;
                token = strtok(nullptr, " ");
            }

            if (tokenCount != 5 && tokenCount != 6) {
                msg.control.println(F("ERROR: SPEED requires 5 or 6 parameters"));
                msg.control.println(F("  Usage: SET <pin> SPEED <ppr> <tire_circ> <ratio> <timeout> <max_speed>"));
                msg.control.println(F("     or: SET <pin> SPEED <ppr> <tire_circ> <ratio> <mult> <timeout> <max_speed>"));
                msg.control.println(F("  Example: SET 2 SPEED 100 2008 3.73 2000 300"));
                msg.control.println(F("       or: SET 2 SPEED 100 2008 3.73 1.05 2000 300"));
                return;
            }

            // Parse parameters
            uint8_t pulses_per_rev;
            uint16_t tire_circumference_mm;
            float final_drive_ratio;
            float calibration_mult = 1.0;  // Default
            uint16_t timeout_ms;
            uint16_t max_speed_kph;

            token = strtok(params, " ");
            if (!token) { msg.control.println(F("ERROR: Missing pulses_per_rev")); return; }
            pulses_per_rev = (uint8_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing tire_circumference_mm")); return; }
            tire_circumference_mm = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing final_drive_ratio")); return; }
            final_drive_ratio = atof(token);

            if (tokenCount == 6) {
                // 6 parameters: custom calibration_mult provided
                token = strtok(nullptr, " ");
                if (!token) { msg.control.println(F("ERROR: Missing calibration_mult")); return; }
                calibration_mult = atof(token);
            }

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing timeout_ms")); return; }
            timeout_ms = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { msg.control.println(F("ERROR: Missing max_speed_kph")); return; }
            max_speed_kph = (uint16_t)atoi(token);

            // Validate parameters
            if (pulses_per_rev < 1 || pulses_per_rev > 250) {
                msg.control.println(F("ERROR: Pulses per rev must be between 1 and 250"));
                return;
            }
            if (tire_circumference_mm < 500 || tire_circumference_mm > 5000) {
                msg.control.println(F("ERROR: Tire circumference must be between 500 and 5000 mm"));
                return;
            }
            if (final_drive_ratio < 0.5 || final_drive_ratio > 10.0) {
                msg.control.println(F("ERROR: Drive ratio must be between 0.5 and 10.0"));
                return;
            }
            if (calibration_mult < 0.5 || calibration_mult > 2.0) {
                msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
                return;
            }
            if (timeout_ms < 100 || timeout_ms > 10000) {
                msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
                return;
            }
            if (max_speed_kph < 50 || max_speed_kph > 500) {
                msg.control.println(F("ERROR: Max speed must be between 50 and 500 km/h"));
                return;
            }

            // Apply custom calibration
            Input* input = getInputByPin(pin);
            if (!input) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Set custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.speed.pulses_per_rev = pulses_per_rev;
            input->customCalibration.speed.tire_circumference_mm = tire_circumference_mm;
            input->customCalibration.speed.final_drive_ratio = final_drive_ratio;
            input->customCalibration.speed.calibration_mult = calibration_mult;
            input->customCalibration.speed.timeout_ms = timeout_ms;
            input->customCalibration.speed.max_speed_kph = max_speed_kph;

            msg.control.print(F("Speed calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Pulses/Rev: "));
            msg.control.println(pulses_per_rev);
            msg.control.print(F("  Tire Circumference: "));
            msg.control.print(tire_circumference_mm);
            msg.control.println(F(" mm"));
            msg.control.print(F("  Drive Ratio: "));
            msg.control.print(final_drive_ratio, 2);
            msg.control.println(F(":1"));
            msg.control.print(F("  Calibration Mult: "));
            msg.control.println(calibration_mult, 4);
            msg.control.print(F("  Timeout: "));
            msg.control.print(timeout_ms);
            msg.control.println(F(" ms"));
            msg.control.print(F("  Max Speed: "));
            msg.control.print(max_speed_kph);
            msg.control.println(F(" km/h"));
            return;
        }

        // SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
        if (strncmp(fieldAndValue, "PRESSURE_LINEAR ", 16) == 0) {
            char* params = fieldAndValue + 16;
            trim(params);

            // Parse 4 float parameters
            float vmin, vmax, pmin, pmax;
            char* token = strtok(params, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                msg.control.println(F("  Usage: SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
                msg.control.println(F("  Example: SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 7.0"));
                return;
            }
            vmin = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            vmax = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            pmin = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            pmax = atof(token);

            // Validate parameters
            if (vmin >= vmax) {
                msg.control.println(F("ERROR: vmin must be less than vmax"));
                return;
            }
            if (vmin < 0.0 || vmax > SYSTEM_VOLTAGE) {
                msg.control.print(F("ERROR: Voltage range must be 0.0-"));
                msg.control.print(SYSTEM_VOLTAGE);
                msg.control.println(F("V for this platform"));
                return;
            }
            if (pmin >= pmax) {
                msg.control.println(F("ERROR: pmin must be less than pmax"));
                return;
            }
            if (pmin < 0.0) {
                msg.control.println(F("ERROR: pmin must be >= 0.0"));
                return;
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.pressureLinear.voltage_min = vmin;
            input->customCalibration.pressureLinear.voltage_max = vmax;
            input->customCalibration.pressureLinear.output_min = pmin;
            input->customCalibration.pressureLinear.output_max = pmax;

            msg.control.print(F("Pressure Linear calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Voltage Range: "));
            msg.control.print(vmin, 2);
            msg.control.print(F("-"));
            msg.control.print(vmax, 2);
            msg.control.println(F(" V"));
            msg.control.print(F("  Pressure Range: "));
            msg.control.print(pmin, 2);
            msg.control.print(F("-"));
            msg.control.print(pmax, 2);
            msg.control.println(F(" bar"));
            return;
        }

        // SET <pin> BIAS <resistor>
        // Generic bias resistor command - works with Steinhart-Hart, Lookup, and Pressure Polynomial
        if (strncmp(fieldAndValue, "BIAS ", 5) == 0) {
            char* biasStr = fieldAndValue + 5;
            trim(biasStr);
            float bias = atof(biasStr);

            // Get input
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Validate calibration type supports bias resistor
            if (input->calibrationType != CAL_THERMISTOR_STEINHART &&
                input->calibrationType != CAL_THERMISTOR_LOOKUP &&
                input->calibrationType != CAL_THERMISTOR_BETA &&
                input->calibrationType != CAL_PRESSURE_POLYNOMIAL) {
                msg.control.print(F("ERROR: Calibration type "));
                msg.control.print(input->calibrationType);
                msg.control.println(F(" does not use bias resistor"));
                msg.control.println(F("  BIAS works with: Thermistor (Steinhart-Hart), Thermistor (Beta), Thermistor (Lookup), Pressure (Polynomial)"));
                return;
            }

            // Validate value (10Ω to 10MΩ covers all practical thermistors)
            #define BIAS_R_MIN 10.0
            #define BIAS_R_MAX 10000000.0
            if (bias < BIAS_R_MIN || bias > BIAS_R_MAX) {
                msg.control.print(F("ERROR: Bias resistor ("));
                msg.control.print(bias, 1);
                msg.control.println(F("Ω) must be between 10Ω and 10MΩ"));
                return;
            }

            // Apply to appropriate union member based on type
            input->flags.useCustomCalibration = true;

            if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
                input->customCalibration.steinhart.bias_resistor = bias;
            } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
                input->customCalibration.beta.bias_resistor = bias;
            } else if (input->calibrationType == CAL_THERMISTOR_LOOKUP) {
                input->customCalibration.lookup.bias_resistor = bias;
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
                input->customCalibration.pressurePolynomial.bias_resistor = bias;
            }

            msg.control.print(F("Bias resistor set for pin "));
            msg.control.print(pinStr);
            msg.control.print(F(": "));
            msg.control.print(bias, 1);
            msg.control.println(F(" Ω"));
            return;
        }

        // SET <pin> STEINHART <bias_r> <a> <b> <c>
        if (strncmp(fieldAndValue, "STEINHART ", 10) == 0) {
            char* params = fieldAndValue + 10;
            trim(params);

            // Parse 4 float parameters
            float bias_r, a, b, c;
            char* token = strtok(params, " ");
            if (!token) {
                msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
                msg.control.println(F("  Usage: SET <pin> STEINHART <bias_r> <a> <b> <c>"));
                msg.control.println(F("  Example: SET A0 STEINHART 10000 0.001129 0.0002341 0.00000008775"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            a = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            b = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            c = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                msg.control.println(F("ERROR: bias_r must be > 0"));
                return;
            }
            if (a == 0 || b == 0 || c == 0) {
                msg.control.println(F("WARNING: Zero coefficient detected - may indicate error"));
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.steinhart.bias_resistor = bias_r;
            input->customCalibration.steinhart.steinhart_a = a;
            input->customCalibration.steinhart.steinhart_b = b;
            input->customCalibration.steinhart.steinhart_c = c;

            msg.control.print(F("Steinhart-Hart calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Bias Resistor: "));
            msg.control.print(bias_r, 1);
            msg.control.println(F(" Ω"));
            msg.control.print(F("  A: "));
            msg.control.println(a, 10);
            msg.control.print(F("  B: "));
            msg.control.println(b, 10);
            msg.control.print(F("  C: "));
            msg.control.println(c, 10);
            return;
        }

        // SET <pin> BETA <bias_r> <beta> <r0> <t0>
        if (strncmp(fieldAndValue, "BETA ", 5) == 0) {
            char* params = fieldAndValue + 5;
            trim(params);

            // Parse 4 float parameters
            float bias_r, beta, r0, t0;
            char* token = strtok(params, " ");
            if (!token) {
                msg.control.println(F("ERROR: BETA requires 4 parameters"));
                msg.control.println(F("  Usage: SET <pin> BETA <bias_r> <beta> <r0> <t0>"));
                msg.control.println(F("  Example: SET A0 BETA 10000 3950 10000 25"));
                msg.control.println(F("  Where: bias_r=bias resistor (Ω), beta=β coefficient (K),"));
                msg.control.println(F("         r0=ref resistance (Ω), t0=ref temp (°C, typically 25)"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            beta = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            r0 = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            t0 = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                msg.control.println(F("ERROR: bias_r must be > 0"));
                return;
            }
            if (beta < 1000 || beta > 10000) {
                msg.control.println(F("WARNING: Beta typically 2000-6000K. Value may be incorrect."));
            }
            if (r0 <= 0) {
                msg.control.println(F("ERROR: r0 must be > 0"));
                return;
            }
            if (t0 < -40 || t0 > 150) {
                msg.control.println(F("WARNING: t0 typically 25°C. Value may be incorrect."));
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->calibrationType = CAL_THERMISTOR_BETA;
            input->customCalibration.beta.bias_resistor = bias_r;
            input->customCalibration.beta.beta = beta;
            input->customCalibration.beta.r0 = r0;
            input->customCalibration.beta.t0 = t0;

            msg.control.print(F("Beta calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Bias Resistor: "));
            msg.control.print(bias_r, 1);
            msg.control.println(F(" Ω"));
            msg.control.print(F("  Beta: "));
            msg.control.print(beta, 1);
            msg.control.println(F(" K"));
            msg.control.print(F("  R0: "));
            msg.control.print(r0, 1);
            msg.control.println(F(" Ω"));
            msg.control.print(F("  T0: "));
            msg.control.print(t0, 1);
            msg.control.println(F(" °C"));
            return;
        }

        // SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>
        if (strncmp(fieldAndValue, "PRESSURE_POLY ", 14) == 0) {
            char* params = fieldAndValue + 14;
            trim(params);

            // Parse 4 float parameters
            float bias_r, a, b, c;
            char* token = strtok(params, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                msg.control.println(F("  Usage: SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>"));
                msg.control.println(F("  Example: SET A1 PRESSURE_POLY 184 -6.75e-4 2.54e-6 1.87e-9"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            a = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            b = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            c = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                msg.control.println(F("ERROR: bias_r must be > 0"));
                return;
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                msg.control.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.pressurePolynomial.bias_resistor = bias_r;
            input->customCalibration.pressurePolynomial.poly_a = a;
            input->customCalibration.pressurePolynomial.poly_b = b;
            input->customCalibration.pressurePolynomial.poly_c = c;

            msg.control.print(F("Pressure Polynomial calibration set for pin "));
            msg.control.println(pinStr);
            msg.control.print(F("  Bias Resistor: "));
            msg.control.print(bias_r, 1);
            msg.control.println(F(" Ω"));
            msg.control.print(F("  A: "));
            msg.control.println(a, 10);
            msg.control.print(F("  B: "));
            msg.control.println(b, 10);
            msg.control.print(F("  C: "));
            msg.control.println(c, 10);
            return;
        }

        // SET <pin> WARMUP <ms>
        if (strncmp(fieldAndValue, "WARMUP ", 7) == 0) {
            char* valueStr = fieldAndValue + 7;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value > 300000) {  // Max 5 minutes
                msg.control.println(F("ERROR: Warmup time must be 0-300000ms"));
                return;
            }
            if (setInputAlarmWarmup(pin, value)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" alarm warmup set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
            }
            return;
        }

        // SET <pin> PERSIST <ms>
        if (strncmp(fieldAndValue, "PERSIST ", 8) == 0) {
            char* valueStr = fieldAndValue + 8;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value > 60000) {  // Max 60 seconds
                msg.control.println(F("ERROR: Persistence time must be 0-60000ms"));
                return;
            }
            if (setInputAlarmPersist(pin, value)) {
                msg.control.print(F("Input "));
                msg.control.print(pinStr);
                msg.control.print(F(" alarm persistence set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
            }
            return;
        }

        msg.control.println(F("ERROR: Unknown SET field"));
        return;
    }

    // ===== ENABLE/DISABLE COMMANDS =====
    // Control input active state
    if (strncmp(cmd, "ENABLE ", 7) == 0) {
        char* pinStr = cmd + 7;
        trim(pinStr);
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
        if (enableInput(pin, true)) {
            msg.control.print(F("Input "));
            msg.control.print(pinStr);
            msg.control.println(F(" enabled"));
        }
        return;
    }

    if (strncmp(cmd, "DISABLE ", 8) == 0) {
        char* pinStr = cmd + 8;
        trim(pinStr);
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
        if (enableInput(pin, false)) {
            msg.control.print(F("Input "));
            msg.control.print(pinStr);
            msg.control.println(F(" disabled"));
        }
        return;
    }

    // ===== CLEAR COMMAND =====
    if (strncmp(cmd, "CLEAR ", 6) == 0) {
        char* pinStr = cmd + 6;
        trim(pinStr);
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
        if (clearInput(pin)) {
            msg.control.print(F("Input "));
            msg.control.print(pinStr);
            msg.control.println(F(" cleared"));
        }
        return;
    }

    // ===== OUTPUT COMMANDS =====
    if (strncmp(cmd, "OUTPUT ", 7) == 0) {
        char* rest = cmd + 7;
        trim(rest);

        // OUTPUT LIST / OUTPUT STATUS
        if (streq(rest, "LIST") || streq(rest, "STATUS")) {
            listOutputs();
            return;
        }

        // Parse: OUTPUT <name> <action> [value]
        char* firstSpace = strchr(rest, ' ');
        if (!firstSpace) {
            msg.control.println(F("ERROR: Invalid OUTPUT syntax"));
            msg.control.println(F("  Usage: OUTPUT <name> <ENABLE|DISABLE|INTERVAL> [ms]"));
            return;
        }

        *firstSpace = '\0';
        char* outputName = rest;
        char* action = firstSpace + 1;
        trim(action);

        OutputModule* output = getOutputByName(outputName);
        if (!output) {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            msg.control.println(F("  Hint: Use 'OUTPUT LIST' to see available outputs"));
            return;
        }

        // OUTPUT <name> ENABLE
        if (streq(action, "ENABLE")) {
            if (setOutputEnabled(outputName, true)) {
                msg.control.print(output->name);
                msg.control.println(F(" output enabled"));
            }
            return;
        }

        // OUTPUT <name> DISABLE
        if (streq(action, "DISABLE")) {
            if (setOutputEnabled(outputName, false)) {
                msg.control.print(output->name);
                msg.control.println(F(" output disabled"));
            }
            return;
        }

        // OUTPUT <name> INTERVAL <ms>
        if (strncmp(action, "INTERVAL ", 9) == 0) {
            char* intervalStr = action + 9;
            trim(intervalStr);
            uint16_t interval = atoi(intervalStr);

            if (interval < 10 || interval > 60000) {
                msg.control.println(F("ERROR: Interval must be 10-60000ms"));
                return;
            }

            if (setOutputInterval(outputName, interval)) {
                msg.control.print(output->name);
                msg.control.print(F(" interval set to "));
                msg.control.print(interval);
                msg.control.println(F("ms"));
            }
            return;
        }

        msg.control.println(F("ERROR: Unknown OUTPUT action"));
        msg.control.println(F("  Valid actions: ENABLE, DISABLE, INTERVAL <ms>"));
        return;
    }

#ifdef ENABLE_RELAY_OUTPUT
    // ===== RELAY COMMANDS =====
    if (strncmp(cmd, "RELAY ", 6) == 0) {
        char* rest = cmd + 6;
        trim(rest);

        // RELAY LIST
        if (streq(rest, "LIST")) {
            printAllRelayStatus();
            return;
        }

        // Parse: RELAY <index> <action> [params]
        uint8_t relayIndex = atoi(rest);
        if (relayIndex >= MAX_RELAYS) {
            msg.control.print(F("ERROR: Invalid relay index (0-"));
            msg.control.print(MAX_RELAYS - 1);
            msg.control.println(F(")"));
            return;
        }

        // Find next space
        char* action = strchr(rest, ' ');
        if (!action) {
            printRelayStatus(relayIndex);  // Just show status if no action
            return;
        }
        action++;
        trim(action);

        // RELAY <index> STATUS
        if (streq(action, "STATUS")) {
            printRelayStatus(relayIndex);
            return;
        }

        // RELAY <index> PIN <pin>
        if (strncmp(action, "PIN ", 4) == 0) {
            uint8_t pin = atoi(action + 4);
            if (setRelayPin(relayIndex, pin)) {
                msg.control.print(F("Relay "));
                msg.control.print(relayIndex);
                msg.control.print(F(" pin set to "));
                msg.control.println(pin);
            }
            return;
        }

        // RELAY <index> INPUT <pin>
        if (strncmp(action, "INPUT ", 6) == 0) {
            bool isValid = false;
            uint8_t inputPin = parsePin(action + 6, &isValid);
            if (!isValid) {
                msg.control.println(F("ERROR: Invalid pin number"));
                return;
            }
            if (setRelayInput(relayIndex, inputPin)) {
                msg.control.print(F("Relay "));
                msg.control.print(relayIndex);
                msg.control.print(F(" linked to input on pin "));
                msg.control.println(inputPin);
            }
            return;
        }

        // RELAY <index> THRESHOLD <on> <off>
        if (strncmp(action, "THRESHOLD ", 10) == 0) {
            char* params = action + 10;
            float thresholdOn = atof(params);
            char* offStr = strchr(params, ' ');
            if (!offStr) {
                msg.control.println(F("ERROR: THRESHOLD requires two values"));
                msg.control.println(F("  Usage: RELAY <index> THRESHOLD <on> <off>"));
                return;
            }
            float thresholdOff = atof(offStr + 1);

            if (setRelayThresholds(relayIndex, thresholdOn, thresholdOff)) {
                msg.control.print(F("Relay "));
                msg.control.print(relayIndex);
                msg.control.print(F(" thresholds: ON="));
                msg.control.print(thresholdOn);
                msg.control.print(F(", OFF="));
                msg.control.println(thresholdOff);
            }
            return;
        }

        // RELAY <index> MODE <AUTO_HIGH|AUTO_LOW|ON|OFF>
        if (strncmp(action, "MODE ", 5) == 0) {
            char* mode = action + 5;
            trim(mode);

            RelayMode newMode;
            if (streq(mode, "AUTO_HIGH")) {
                newMode = RELAY_AUTO_HIGH;
            } else if (streq(mode, "AUTO_LOW")) {
                newMode = RELAY_AUTO_LOW;
            } else if (streq(mode, "ON")) {
                newMode = RELAY_MANUAL_ON;
            } else if (streq(mode, "OFF")) {
                newMode = RELAY_MANUAL_OFF;
            } else {
                msg.control.println(F("ERROR: Invalid mode"));
                msg.control.println(F("  Valid modes: AUTO_HIGH, AUTO_LOW, ON, OFF"));
                return;
            }

            if (setRelayMode(relayIndex, newMode)) {
                msg.control.print(F("Relay "));
                msg.control.print(relayIndex);
                msg.control.print(F(" mode set to "));
                msg.control.println(mode);
            }
            return;
        }

        // RELAY <index> DISABLE
        if (streq(action, "DISABLE")) {
            if (setRelayMode(relayIndex, RELAY_DISABLED)) {
                msg.control.print(F("Relay "));
                msg.control.print(relayIndex);
                msg.control.println(F(" disabled"));
            }
            return;
        }

        msg.control.println(F("ERROR: Unknown RELAY command"));
        msg.control.println(F("  Usage: RELAY LIST"));
        msg.control.println(F("  Usage: RELAY <0-1> STATUS"));
        msg.control.println(F("  Usage: RELAY <0-1> PIN <pin>"));
        msg.control.println(F("  Usage: RELAY <0-1> INPUT <pin>"));
        msg.control.println(F("  Usage: RELAY <0-1> THRESHOLD <on> <off>"));
        msg.control.println(F("  Usage: RELAY <0-1> MODE <AUTO_HIGH|AUTO_LOW|ON|OFF>"));
        msg.control.println(F("  Usage: RELAY <0-1> DISABLE"));
        return;
    }
#endif

#ifdef ENABLE_TEST_MODE
    // ===== TEST COMMANDS =====
    if (strncmp(cmd, "TEST", 4) == 0) {
        // Handle standalone "TEST" command
        if (streq(cmd, "TEST")) {
            msg.control.println(F("ERROR: TEST requires a subcommand"));
            msg.control.println(F("  Usage: TEST LIST"));
            msg.control.println(F("  Usage: TEST <0-4>"));
            msg.control.println(F("  Usage: TEST STOP"));
            msg.control.println(F("  Usage: TEST STATUS"));
            return;
        }

        char* rest = cmd + 4;
        trim(rest);

        // TEST LIST
        if (streq(rest, "LIST")) {
            listTestScenarios();
            return;
        }

        // TEST STOP
        if (streq(rest, "STOP")) {
            if (!isTestModeActive()) {
                msg.control.println(F("No test scenario is currently running"));
            } else {
                stopTestMode();
                msg.control.println(F("Test mode stopped"));
            }
            return;
        }

        // TEST STATUS
        if (streq(rest, "STATUS")) {
            if (!isTestModeActive()) {
                msg.control.println(F("Test mode: INACTIVE"));
            } else {
                msg.control.println(F("Test mode: ACTIVE"));
                msg.control.println(F("  Use TEST LIST to see all scenarios"));
            }
            return;
        }

        // TEST <scenario_number>
        // Try to parse as a number
        char* endPtr;
        long scenarioNum = strtol(rest, &endPtr, 10);

        // Check if parsing succeeded and entire string was consumed
        if (endPtr != rest && *endPtr == '\0') {
            // Valid number
            if (scenarioNum < 0 || scenarioNum >= getNumTestScenarios()) {
                msg.control.print(F("ERROR: Invalid scenario index (must be 0-"));
                msg.control.print(getNumTestScenarios() - 1);
                msg.control.println(F(")"));
                msg.control.println(F("Use TEST LIST to see available scenarios"));
                return;
            }

            // Start the test scenario
            if (startTestScenario((uint8_t)scenarioNum)) {
                msg.control.println(F("Test scenario started"));
                msg.control.println(F("  Use TEST STATUS to check progress"));
                msg.control.println(F("  Use TEST STOP to end early"));
            } else {
                msg.control.println(F("ERROR: Failed to start test scenario"));
            }
            return;
        }

        // Unknown TEST subcommand
        msg.control.println(F("ERROR: Unknown TEST command"));
        msg.control.println(F("  Usage: TEST LIST"));
        msg.control.println(F("  Usage: TEST <0-4>"));
        msg.control.println(F("  Usage: TEST STOP"));
        msg.control.println(F("  Usage: TEST STATUS"));
        return;
    }
#endif

    // ===== DISPLAY COMMANDS =====
    if (strncmp(cmd, "DISPLAY ", 8) == 0) {
        char* rest = cmd + 8;
        trim(rest);

        // DISPLAY STATUS
        if (streq(rest, "STATUS")) {
            msg.control.println(F("=== Display Configuration ==="));
            msg.control.print(F("Status: "));
            msg.control.println(systemConfig.displayEnabled ? F("Enabled") : F("Disabled"));
            msg.control.print(F("Type: "));
            switch (systemConfig.displayType) {
                case DISPLAY_NONE: msg.control.println(F("None")); break;
                case DISPLAY_LCD: msg.control.println(F("LCD")); break;
                case DISPLAY_OLED: msg.control.println(F("OLED")); break;
                default: msg.control.println(F("Unknown")); break;
            }
            msg.control.print(F("LCD I2C Address: 0x"));
            msg.control.println(systemConfig.lcdI2CAddress, HEX);
            msg.control.print(F("Temperature Units: "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
            msg.control.print(F("Pressure Units: "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
            msg.control.print(F("Elevation Units: "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
            msg.control.print(F("Speed Units: "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultSpeedUnits));
            return;
        }

        // DISPLAY ENABLE
        if (streq(rest, "ENABLE")) {
            systemConfig.displayEnabled = 1;    // Update config (for SAVE)
            setDisplayRuntime(true);             // Update runtime immediately
            msg.control.println(F("✓ Display enabled (use SAVE to persist)"));
            return;
        }

        // DISPLAY DISABLE
        if (streq(rest, "DISABLE")) {
            systemConfig.displayEnabled = 0;    // Update config (for SAVE)
            setDisplayRuntime(false);            // Update runtime immediately
            msg.control.println(F("✓ Display disabled (use SAVE to persist)"));
            return;
        }

        // DISPLAY TYPE <LCD|OLED|NONE>
        if (strncmp(rest, "TYPE ", 5) == 0) {
            char* typeStr = rest + 5;
            trim(typeStr);
            if (streq(typeStr, "LCD")) {
                systemConfig.displayType = DISPLAY_LCD;
                msg.control.println(F("Display type set to LCD"));
            } else if (streq(typeStr, "OLED")) {
                systemConfig.displayType = DISPLAY_OLED;
                msg.control.println(F("Display type set to OLED"));
            } else if (streq(typeStr, "NONE")) {
                systemConfig.displayType = DISPLAY_NONE;
                msg.control.println(F("Display disabled"));
            } else {
                msg.control.println(F("ERROR: Invalid display type. Valid: LCD, OLED, NONE"));
            }
            return;
        }

        // DISPLAY LCD ADDRESS <hex>
        if (strncmp(rest, "LCD ADDRESS ", 12) == 0) {
            char* addrStr = rest + 12;
            trim(addrStr);
            // Parse hex address (e.g., "0x27" or "27")
            char* endptr;
            long addr = strtol(addrStr, &endptr, 16);
            if (addr >= 0x03 && addr <= 0x77) {
                systemConfig.lcdI2CAddress = (uint8_t)addr;
                msg.control.print(F("LCD I2C address set to 0x"));
                msg.control.println(systemConfig.lcdI2CAddress, HEX);
            } else {
                msg.control.println(F("ERROR: Invalid I2C address (valid range: 0x03-0x77)"));
            }
            return;
        }

        // DISPLAY UNITS TEMP <C|F|CELSIUS|FAHRENHEIT>
        if (strncmp(rest, "UNITS TEMP ", 11) == 0) {
            char* unitStr = rest + 11;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_TEMPERATURE) {
                systemConfig.defaultTempUnits = index;
                msg.control.print(F("Default temperature units set to "));
                msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                msg.control.println(F("ERROR: Invalid units. Valid: C, F, CELSIUS, FAHRENHEIT"));
            }
            return;
        }

        // DISPLAY UNITS PRESSURE <BAR|PSI|KPA>
        if (strncmp(rest, "UNITS PRESSURE ", 15) == 0) {
            char* unitStr = rest + 15;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_PRESSURE) {
                systemConfig.defaultPressUnits = index;
                msg.control.print(F("Default pressure units set to "));
                msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                msg.control.println(F("ERROR: Invalid units. Valid: BAR, PSI, KPA, INHG"));
            }
            return;
        }

        // DISPLAY UNITS ELEVATION <M|FT|METERS|FEET>
        if (strncmp(rest, "UNITS ELEVATION ", 16) == 0) {
            char* unitStr = rest + 16;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_ELEVATION) {
                systemConfig.defaultElevUnits = index;
                msg.control.print(F("Default elevation units set to "));
                msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                msg.control.println(F("ERROR: Invalid units. Valid: M, FT, METERS, FEET"));
            }
            return;
        }

        // DISPLAY UNITS SPEED <KPH|MPH>
        if (strncmp(rest, "UNITS SPEED ", 12) == 0) {
            char* unitStr = rest + 12;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_SPEED) {
                systemConfig.defaultSpeedUnits = index;
                msg.control.print(F("Default speed units set to "));
                msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                msg.control.println(F("ERROR: Invalid units. Valid: KPH, MPH"));
            }
            return;
        }

        msg.control.println(F("ERROR: Unknown DISPLAY command"));
        msg.control.println(F("  Valid commands:"));
        msg.control.println(F("    DISPLAY STATUS"));
        msg.control.println(F("    DISPLAY ENABLE"));
        msg.control.println(F("    DISPLAY DISABLE"));
        msg.control.println(F("    DISPLAY TYPE <LCD|OLED|NONE>"));
        msg.control.println(F("    DISPLAY LCD ADDRESS <hex>"));
        msg.control.println(F("    DISPLAY UNITS TEMP <C|F>"));
        msg.control.println(F("    DISPLAY UNITS PRESSURE <BAR|PSI|KPA>"));
        msg.control.println(F("    DISPLAY UNITS ELEVATION <M|FT>"));
        return;
    }

    // ===== TRANSPORT COMMANDS =====
    if (strncmp(cmd, "TRANSPORT ", 10) == 0) {
        char* rest = cmd + 10;
        trim(rest);

        // TRANSPORT LIST or TRANSPORT STATUS
        if (streq(rest, "LIST") || streq(rest, "STATUS")) {
            router.listTransports();
            return;
        }

        // TRANSPORT <PLANE> <TRANSPORT>
        // Parse plane and transport
        char* space = strchr(rest, ' ');
        if (!space) {
            msg.control.println(F("ERROR: Invalid TRANSPORT command"));
            msg.control.println(F("Usage: TRANSPORT <CONTROL|DATA|DEBUG> <transport>"));
            msg.control.println(F("   or: TRANSPORT LIST"));
            msg.control.println(F(""));
            msg.control.println(F("Available transports:"));
            msg.control.println(F("  USB_SERIAL (or USB, SERIAL) - USB Serial port"));
            msg.control.println(F("  SERIAL1 - Hardware Serial1"));
            msg.control.println(F("  SERIAL2 - Hardware Serial2 (if available)"));
            msg.control.println(F("  SERIAL3 - Hardware Serial3 (if available)"));
            msg.control.println(F("  ESP32_BT (or ESP32) - ESP32 built-in Bluetooth Classic (ESP32 only)"));
            return;
        }

        *space = '\0';
        char* planeName = rest;
        char* transportName = space + 1;
        trim(planeName);
        trim(transportName);

        // Parse plane
        MessagePlane plane = parsePlane(planeName);
        if (plane >= NUM_PLANES) {
            msg.control.print(F("ERROR: Invalid plane '"));
            msg.control.print(planeName);
            msg.control.println(F("'"));
            msg.control.println(F("Valid planes: CONTROL, DATA, DEBUG"));
            return;
        }

        // Parse transport
        TransportID transport = parseTransport(transportName);
        if (transport >= NUM_TRANSPORTS) {
            msg.control.print(F("ERROR: Invalid transport '"));
            msg.control.print(transportName);
            msg.control.println(F("'"));
            msg.control.println(F("Valid transports: USB_SERIAL, SERIAL1, SERIAL2, SERIAL3, ESP32_BT"));
            return;
        }

        // Set transport
        if (router.setTransport(plane, transport)) {
            msg.control.print(F("✓ "));
            msg.control.print(getPlaneName(plane));
            msg.control.print(F(" plane → "));
            msg.control.println(getTransportName(transport));
            msg.control.println(F("NOTE: Configuration will be lost on reboot unless you SAVE"));
        } else {
            msg.control.print(F("ERROR: Transport "));
            msg.control.print(getTransportName(transport));
            msg.control.println(F(" is not available"));
            msg.control.println(F("Make sure the hardware is connected and registered"));
        }
        return;
    }

    // ===== SYSTEM COMMANDS =====
    if (strncmp(cmd, "SYSTEM ", 7) == 0) {
        char* rest = cmd + 7;
        trim(rest);

        // SYSTEM STATUS
        if (streq(rest, "STATUS")) {
            // Platform identification
            msg.control.println(F("=== Platform Information ==="));
            msg.control.print(F("Platform: "));
            msg.control.println(F(PLATFORM_NAME));
            msg.control.print(F("I2C Clock: "));
            msg.control.println(F(I2C_CLOCK_SPEED));
            msg.control.print(F("Features: Serial"));
            #ifdef USE_BME280
                msg.control.print(F(", BME280"));
            #endif
            #ifdef ENABLE_LCD
                msg.control.print(F(", LCD"));
            #endif
            #ifdef ENABLE_TEST_MODE
                msg.control.print(F(", Test Mode"));
            #endif
            #ifdef ENABLE_CAN
                msg.control.print(F(", CAN"));
            #endif
            msg.control.println();
            msg.control.println();

            printSystemConfig();
            msg.control.println();
            msg.control.println(F("Compile-Time Defaults:"));
            msg.control.print(F("  Default Bias Resistor: "));
            msg.control.print(DEFAULT_BIAS_RESISTOR);
            msg.control.println(F(" Ω"));
            msg.control.println();
            msg.control.println(F("Hardware Pins:"));
            msg.control.print(F("  Mode Button: "));
            msg.control.println(systemConfig.modeButtonPin);
            msg.control.print(F("  Buzzer: "));
            msg.control.println(systemConfig.buzzerPin);
            msg.control.print(F("  CAN CS: "));
            msg.control.println(systemConfig.canCSPin);
            msg.control.print(F("  CAN INT: "));
            msg.control.println(systemConfig.canIntPin);
            msg.control.print(F("  SD CS: "));
            msg.control.println(systemConfig.sdCSPin);
            if (systemConfig.testModePin != 0xFF) {
                msg.control.print(F("  Test Mode Pin: "));
                msg.control.println(systemConfig.testModePin);
            }
            return;
        }

        // SYSTEM SEA_LEVEL <hPa>
        if (strncmp(rest, "SEA_LEVEL ", 10) == 0) {
            char* valueStr = rest + 10;
            trim(valueStr);
            float value = atof(valueStr);
            if (value >= 800 && value <= 1200) {
                systemConfig.seaLevelPressure = value;
                msg.control.print(F("Sea level pressure set to "));
                msg.control.print(value);
                msg.control.println(F(" hPa"));
            } else {
                msg.control.println(F("ERROR: Sea level pressure must be 800-1200 hPa"));
            }
            return;
        }

        // SYSTEM INTERVAL SENSOR <ms>
        if (strncmp(rest, "INTERVAL SENSOR ", 16) == 0) {
            char* valueStr = rest + 16;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.sensorReadInterval = value;
                msg.control.print(F("Sensor read interval set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
            } else {
                msg.control.println(F("ERROR: Sensor interval must be 10-10000ms"));
            }
            return;
        }

        // SYSTEM INTERVAL ALARM <ms>
        if (strncmp(rest, "INTERVAL ALARM ", 15) == 0) {
            char* valueStr = rest + 15;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.alarmCheckInterval = value;
                msg.control.print(F("Alarm check interval set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
            } else {
                msg.control.println(F("ERROR: Alarm interval must be 10-10000ms"));
            }
            return;
        }

        // SYSTEM INTERVAL LCD <ms>
        if (strncmp(rest, "INTERVAL LCD ", 13) == 0) {
            char* valueStr = rest + 13;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.lcdUpdateInterval = value;
                msg.control.print(F("LCD update interval set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
            } else {
                msg.control.println(F("ERROR: LCD interval must be 10-10000ms"));
            }
            return;
        }

        msg.control.println(F("ERROR: Unknown SYSTEM command"));
        msg.control.println(F("  Valid commands:"));
        msg.control.println(F("    SYSTEM STATUS"));
        msg.control.println(F("    SYSTEM SEA_LEVEL <hPa>"));
        msg.control.println(F("    SYSTEM INTERVAL SENSOR <ms>"));
        msg.control.println(F("    SYSTEM INTERVAL ALARM <ms>"));
        msg.control.println(F("    SYSTEM INTERVAL LCD <ms>"));
        return;
    }

    // ===== INFO COMMAND =====
    if (strncmp(cmd, "INFO ", 5) == 0) {
        char* rest = cmd + 5;
        trim(rest);

        // Check for INFO <pin> CALIBRATION
        char* spacePos = strchr(rest, ' ');
        if (spacePos) {
            *spacePos = '\0';
            char* pinStr = rest;
            char* subcommand = spacePos + 1;
            trim(subcommand);

            if (streq(subcommand, "ALARM")) {
                bool pinValid = false;
                uint8_t pin = parsePin(pinStr, &pinValid);
                if (!pinValid) return;
                Input* input = getInputByPin(pin);
                if (!input || !input->flags.isEnabled) {
                    msg.control.println(F("ERROR: Input not configured"));
                    return;
                }

                msg.control.print(F("Pin "));
                msg.control.print(pinStr);
                msg.control.println(F(" Alarm Configuration:"));

                msg.control.print(F("  Alarm Enabled: "));
                msg.control.println(input->flags.alarm ? F("YES") : F("NO"));

                msg.control.print(F("  Alarm State: "));
                switch (input->alarmContext.state) {
                    case ALARM_DISABLED: msg.control.println(F("DISABLED")); break;
                    case ALARM_INIT: msg.control.println(F("INIT")); break;
                    case ALARM_WARMUP: msg.control.println(F("WARMUP")); break;
                    case ALARM_READY: msg.control.println(F("READY")); break;
                    case ALARM_ACTIVE: msg.control.println(F("ACTIVE")); break;
                    default: msg.control.println(F("UNKNOWN")); break;
                }

                msg.control.print(F("  Currently In Alarm: "));
                msg.control.println(input->flags.isInAlarm ? F("YES") : F("NO"));

                msg.control.print(F("  Warmup Time: "));
                msg.control.print(input->alarmContext.warmupTime_ms);
                msg.control.println(F("ms"));

                msg.control.print(F("  Persistence Time: "));
                msg.control.print(input->alarmContext.persistTime_ms);
                msg.control.println(F("ms"));

                msg.control.print(F("  Thresholds: "));
                msg.control.print(input->minValue);
                msg.control.print(F(" - "));
                msg.control.print(input->maxValue);
                msg.control.print(F(" "));
                msg.control.println((__FlashStringHelper*)getUnitStringByIndex(input->unitsIndex));

                return;
            }

            if (streq(subcommand, "CALIBRATION")) {
                bool pinValid = false;
                uint8_t pin = parsePin(pinStr, &pinValid);
                if (!pinValid) return;
                Input* input = getInputByPin(pin);
                if (!input || !input->flags.isEnabled) {
                    msg.control.println(F("ERROR: Input not configured"));
                    return;
                }

                msg.control.print(F("Pin "));
                msg.control.print(pinStr);
                msg.control.println(F(" Calibration:"));
                msg.control.print(F("  Type: "));
                switch (input->calibrationType) {
                    case CAL_THERMISTOR_STEINHART: msg.control.println(F("THERMISTOR_STEINHART")); break;
                    case CAL_THERMISTOR_BETA: msg.control.println(F("THERMISTOR_BETA")); break;
                    case CAL_THERMISTOR_LOOKUP: msg.control.println(F("THERMISTOR_LOOKUP")); break;
                    case CAL_PRESSURE_POLYNOMIAL: msg.control.println(F("PRESSURE_POLYNOMIAL")); break;
                    case CAL_LINEAR: msg.control.println(F("PRESSURE_LINEAR")); break;
                    case CAL_VOLTAGE_DIVIDER: msg.control.println(F("VOLTAGE_DIVIDER")); break;
                    case CAL_RPM: msg.control.println(F("RPM")); break;
                    default: msg.control.println(F("NONE")); break;
                }

                msg.control.print(F("  Source: "));
                if (input->flags.useCustomCalibration) {
                    msg.control.println(F("Custom (RAM)"));

                    // Print custom calibration values based on type
                    if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
                        msg.control.print(F("  Bias Resistor: "));
                        msg.control.print(input->customCalibration.steinhart.bias_resistor, 1);
                        msg.control.println(F(" Ω"));
                        msg.control.print(F("  A: "));
                        msg.control.println(input->customCalibration.steinhart.steinhart_a, 10);
                        msg.control.print(F("  B: "));
                        msg.control.println(input->customCalibration.steinhart.steinhart_b, 10);
                        msg.control.print(F("  C: "));
                        msg.control.println(input->customCalibration.steinhart.steinhart_c, 10);
                    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
                        msg.control.print(F("  Bias Resistor: "));
                        msg.control.print(input->customCalibration.beta.bias_resistor, 1);
                        msg.control.println(F(" Ω"));
                        msg.control.print(F("  Beta: "));
                        msg.control.print(input->customCalibration.beta.beta, 1);
                        msg.control.println(F(" K"));
                        msg.control.print(F("  R0: "));
                        msg.control.print(input->customCalibration.beta.r0, 1);
                        msg.control.println(F(" Ω"));
                        msg.control.print(F("  T0: "));
                        msg.control.print(input->customCalibration.beta.t0, 1);
                        msg.control.println(F(" °C"));
                    } else if (input->calibrationType == CAL_THERMISTOR_LOOKUP) {
                        msg.control.print(F("  Bias Resistor: "));
                        msg.control.print(input->customCalibration.lookup.bias_resistor, 1);
                        msg.control.println(F(" Ω"));
                    } else if (input->calibrationType == CAL_LINEAR) {
                        msg.control.print(F("  Voltage Range: "));
                        msg.control.print(input->customCalibration.pressureLinear.voltage_min, 2);
                        msg.control.print(F("-"));
                        msg.control.print(input->customCalibration.pressureLinear.voltage_max, 2);
                        msg.control.println(F(" V"));
                        msg.control.print(F("  Pressure Range: "));
                        msg.control.print(input->customCalibration.pressureLinear.output_min, 2);
                        msg.control.print(F("-"));
                        msg.control.print(input->customCalibration.pressureLinear.output_max, 2);
                        msg.control.println(F(" bar"));
                    } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
                        msg.control.print(F("  Bias Resistor: "));
                        msg.control.print(input->customCalibration.pressurePolynomial.bias_resistor, 1);
                        msg.control.println(F(" Ω"));
                        msg.control.print(F("  A: "));
                        msg.control.println(input->customCalibration.pressurePolynomial.poly_a, 10);
                        msg.control.print(F("  B: "));
                        msg.control.println(input->customCalibration.pressurePolynomial.poly_b, 10);
                        msg.control.print(F("  C: "));
                        msg.control.println(input->customCalibration.pressurePolynomial.poly_c, 10);
                    } else if (input->calibrationType == CAL_RPM) {
                        msg.control.print(F("  Poles: "));
                        msg.control.println(input->customCalibration.rpm.poles);
                        msg.control.print(F("  Pulley Ratio: "));
                        msg.control.println(input->customCalibration.rpm.pulley_ratio, 2);
                        msg.control.print(F("  Calibration Mult: "));
                        msg.control.println(input->customCalibration.rpm.calibration_mult, 4);
                        msg.control.print(F("  Timeout: "));
                        msg.control.print(input->customCalibration.rpm.timeout_ms);
                        msg.control.println(F(" ms"));
                        msg.control.print(F("  RPM Range: "));
                        msg.control.print(input->customCalibration.rpm.min_rpm);
                        msg.control.print(F("-"));
                        msg.control.println(input->customCalibration.rpm.max_rpm);
                    } else if (input->calibrationType == CAL_SPEED) {
                        msg.control.print(F("  Pulses/Rev: "));
                        msg.control.println(input->customCalibration.speed.pulses_per_rev);
                        msg.control.print(F("  Tire Circumference: "));
                        msg.control.print(input->customCalibration.speed.tire_circumference_mm);
                        msg.control.println(F(" mm"));
                        msg.control.print(F("  Drive Ratio: "));
                        msg.control.println(input->customCalibration.speed.final_drive_ratio, 2);
                        msg.control.print(F("  Calibration Mult: "));
                        msg.control.println(input->customCalibration.speed.calibration_mult, 4);
                        msg.control.print(F("  Timeout: "));
                        msg.control.print(input->customCalibration.speed.timeout_ms);
                        msg.control.println(F(" ms"));
                        msg.control.print(F("  Max Speed: "));
                        msg.control.print(input->customCalibration.speed.max_speed_kph);
                        msg.control.println(F(" km/h"));
                    }
                } else {
                    msg.control.println(F("Preset (PROGMEM)"));
                }
                return;
            }

            // Restore space for unknown subcommand
            *spacePos = ' ';
        }

        // Default: INFO <pin>
        bool pinValid = false;
        uint8_t pin = parsePin(rest, &pinValid);
        if (!pinValid) return;
        printInputInfo(pin);
        return;
    }

    // ===== PERSISTENCE COMMANDS =====
    // EEPROM save/load/reset
    if (streq(cmd, "SAVE")) {
        bool success = true;
        success &= saveInputConfig();
        router.saveConfig();  // Syncs transport routing to systemConfig.router AND saves ALL systemConfig to EEPROM

        if (success) {
            msg.control.println(F("✓ Configuration saved to EEPROM"));
        } else {
            msg.control.println(F("ERROR: Failed to save configuration"));
        }
        return;
    }

    if (streq(cmd, "LOAD")) {
        bool inputsOK = loadInputConfig();
        bool systemOK = loadSystemConfig();

        if (inputsOK && systemOK) {
            msg.control.println(F("✓ Configuration loaded from EEPROM"));
        } else if (inputsOK || systemOK) {
            msg.control.println(F("WARNING: Partial configuration loaded"));
        } else {
            msg.control.println(F("ERROR: Failed to load configuration"));
        }
        return;
    }

    if (streq(cmd, "RESET")) {
        msg.control.println(F("WARNING: This will erase all configuration!"));
        msg.control.println(F("Type RESET CONFIRM to proceed"));
        return;
    }

    if (streq(cmd, "RESET CONFIRM")) {
        resetInputConfig();
        resetSystemConfig();
        msg.control.println(F("✓ All configuration reset to defaults"));
        return;
    }

    // ===== VERSION COMMAND =====
    if (streq(cmd, "VERSION")) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.print(F("  Firmware: "));
        msg.control.println(FIRMWARE_VERSION);
        msg.control.print(F("  EEPROM Version: "));
        msg.control.println(EEPROM_VERSION);
        msg.control.print(F("  Active Inputs: "));
        extern uint8_t numActiveInputs;
        msg.control.print(numActiveInputs);
        msg.control.print(F("/"));
        msg.control.println(MAX_INPUTS);
        msg.control.println(F("========================================"));
        msg.control.println();
        return;
    }

    // ===== DUMP COMMAND =====
    if (streq(cmd, "DUMP") || strncmp(cmd, "DUMP ", 5) == 0) {
        // Check for "DUMP JSON" variant
        if (firstSpace) {
            char* subCmd = firstSpace + 1;
            if (streq(subCmd, "JSON")) {
                msg.control.println();
                dumpConfigToJSON(Serial);
                msg.control.println();
                return;
            }
        }

        // Regular DUMP (human-readable)
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  Full Configuration Dump"));
        msg.control.println(F("========================================"));
        msg.control.println();

        // Show inputs
        listAllInputs();
        msg.control.println();

        // Show outputs
        listOutputs();
        msg.control.println();

        // Show display config
        msg.control.println(F("=== Display Configuration ==="));
        msg.control.print(F("Type: "));
        switch (systemConfig.displayType) {
            case DISPLAY_NONE: msg.control.println(F("None")); break;
            case DISPLAY_LCD: msg.control.println(F("LCD")); break;
            case DISPLAY_OLED: msg.control.println(F("OLED")); break;
            default: msg.control.println(F("Unknown")); break;
        }
        msg.control.print(F("LCD I2C Address: 0x"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
        msg.control.print(F("Default Units: Temp="));
        msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
        msg.control.print(F(", Press="));
        msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
        msg.control.print(F(", Elev="));
        msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
        msg.control.print(F(", Speed="));
        msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultSpeedUnits));
        msg.control.println();

        msg.control.println(F("To save this configuration to EEPROM, type: SAVE"));
        msg.control.println();
        return;
    }

    // ===== RELOAD COMMAND =====
    if (streq(cmd, "RELOAD")) {
        // Print directly to Serial to bypass router buffering
        Serial.println();
        Serial.println(F("Triggering watchdog reset..."));
        Serial.println(F("System will reload in 2 seconds."));
        Serial.flush();
        delay(100);  // Ensure message is sent

        // Force watchdog enable (even if it was disabled in CONFIG mode)
        #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
            defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
            // AVR: Proper watchdog initialization sequence
            cli();  // Disable interrupts
            wdt_reset();  // Reset watchdog
            // Clear WDRF in MCUSR
            MCUSR &= ~(1<<WDRF);
            // Enable watchdog with 2s timeout
            wdt_enable(WDTO_2S);
            sei();  // Re-enable interrupts
            Serial.println(F("Watchdog force-enabled (AVR)"));
            Serial.flush();
        #else
            extern void watchdogEnable(uint16_t);
            watchdogEnable(2000);
            Serial.println(F("Watchdog enabled"));
            Serial.flush();
        #endif

        // Infinite loop to trigger watchdog
        // NOTE: Empty loop doesn't work - must call millis() periodically for AVR watchdog
        uint32_t lastCheck = millis();
        while (true) {
            if (millis() - lastCheck >= 500) {
                lastCheck = millis();
            }
        }
        return;
    }

    // Unknown command
    msg.control.print(F("ERROR: Unknown command '"));
    msg.control.print(cmd);
    msg.control.println(F("'"));
    msg.control.println(F("Type HELP for available commands"));
}

#endif // USE_STATIC_CONFIG
