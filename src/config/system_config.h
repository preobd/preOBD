/*
 * system_config.h - System-Wide Configuration Management
 *
 * Manages runtime configuration for outputs, display, timing intervals,
 * and other system-wide settings. Persisted to EEPROM alongside input configs.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>
#include "eeprom_store.h"   // EEPROM: the core library, or a flash-backed image on STM32
#include <stddef.h>   // offsetof, for the layout static_asserts below
#include "../buses/bus_config.h"
#include "../config.h"

// EEPROM memory layout constants
#define SYSTEM_CONFIG_MAGIC 0x5343      // "SC" in ASCII
#define SYSTEM_CONFIG_VERSION 14        // Increment when struct changes (v14: one set of CAN settings per bus, #612 phase 2a; clean reset from v13)
// Unreleased layout changes under one version number (EEPROM_VERSION = public
// + 1, collapsed): the checksum is salted with the layout, so an image of an
// earlier layout with the same version fails it and loads defaults instead of
// being misread field by field. Change it with every layout change.
#define SYSTEM_CONFIG_LAYOUT 0x2B       // #612 phase 2b-1: outputs left SystemConfig, console/log replace the router
#define SYSTEM_CONFIG_SIZE sizeof(SystemConfig)

// Total EEPROM bytes available on this platform.
// E2END is the last valid byte index (defined by EEPROM.h on AVR and Teensy 3.x/4.x).
// If a future platform doesn't define E2END, override EEPROM_TOTAL_BYTES in its profile header.
#ifndef EEPROM_TOTAL_BYTES
#define EEPROM_TOTAL_BYTES (E2END + 1)
#endif

// Watchdog scope around an EEPROM save (#541). AVR caps every timeout at 8 s.
#define EEPROM_WDT_WINDOW_MS 20000

// Per-input routes (Input::routeMask, one bit per output id, #612): a new
// input is routed to every output. Every bit, not just the outputs that
// exist: OUTPUT ADD routes every input to the new output anyway.
#define ROUTE_MASK_ALL 0xFF

// Display types
enum DisplayType {
    DISPLAY_NONE = 0,
    DISPLAY_LCD = 1,
    DISPLAY_OLED = 2
};

#include "../rules/rules_types.h"  // Rule/Action records + INCLUDE_RULES_ENGINE

// System configuration structure
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 validation
    uint8_t version;             // Schema version
    uint8_t checksum;            // XOR checksum

    // The outputs you add live in the config-store arena (output_table.h,
    // #612 phase 2b-1). The alarm is a built-in output: its switch is here.
    uint8_t alarmEnabled;        // ALARM ENABLE|DISABLE
    // Display Settings (7 bytes)
    uint8_t displayEnabled;      // Display on/off (bool)
    uint8_t displayType;         // LCD/OLED/None (DisplayType enum)
    uint8_t lcdI2CAddress;       // LCD I2C address (default 0x27); see isValidLcdI2CAddress()
    uint8_t defaultTempUnits;    // Unit index: 0=Celsius, 1=Fahrenheit
    uint8_t defaultPressUnits;   // Unit index: 2=Bar, 3=PSI, 4=kPa
    uint8_t defaultElevUnits;    // Unit index: 9=Meters, 10=Feet
    uint8_t defaultSpeedUnits;   // Unit index: 11=KPH, 12=MPH

    // Alarm acknowledgement policy (#433). 0 is the built-in default (buzzer
    // only, SILENCE_DURATION). Encoded, see ALARM_SILENCE_* in outputs/annunciator/output_alarm.h:
    //   bit 7      SCOPE ALL — an acknowledged alarm also stops the LED blink
    //   bits 0..6  duration code: 0 = default, 1..126 = n*5 s, 127 = until clear
    uint8_t alarmSilence;

    // Physical Constants (4 bytes)
    float seaLevelPressure;      // hPa for altitude

    // Where the console and the log go (#612 phase 2b-1, §14.7): a primary
    // and an optional secondary transport each (TransportID). The console reads
    // commands from both and answers on the one a command came from.
    struct {
        uint8_t primary;
        uint8_t secondary;
    } console;
    struct {
        uint8_t primary;
        uint8_t secondary;
    } log;

    // NB (v13): the control-engine Rule/Action tables are NO LONGER embedded
    // here. They are standalone RAM globals (rules_manager.cpp) persisted by
    // the shared config-store arena at the FRONT of EEPROM (input_manager.cpp),
    // so the table size no longer competes with this back-anchored block and is
    // bounded by the EEPROM budget rather than an arbitrary MAX_RULES cap (#296).

    // Bus Configuration (16 bytes) - Simplified "pick one" model
    BusConfig buses;

    // Serial Port Configuration (16 bytes) - Which serial ports are enabled
    SerialPortConfig serial;

    // Log levels and tags (LOG LEVEL / LOG TAG)
    struct {
        uint8_t console_level;    // LogLevel for the console's own messages
        uint8_t debug_level;      // LogLevel for the log (default: INFO)
        uint32_t enabledTags;     // 32-bit bitmap for tag filtering (all enabled by default)
    } logFilter;
};

// SystemConfig is anchored at the end of EEPROM so its address adapts to the
// platform automatically. The input block grows from address 0; a static_assert
// in input_manager.cpp keeps the two from overlapping.
constexpr size_t SYSTEM_CONFIG_ADDRESS = EEPROM_TOTAL_BYTES - sizeof(SystemConfig);
static_assert(EEPROM_TOTAL_BYTES >= sizeof(SystemConfig) + 16,
              "EEPROM too small to hold SystemConfig");

// Global system config instance
extern SystemConfig systemConfig;

// LCD I2C address (#590). A character LCD's backpack is a PCF8574 (0x20-0x27)
// or a PCF8574A (0x38-0x3F); its three address jumpers pick within the range,
// so nothing else is a valid LCD address. `DISPLAY ADDRESS` and JSON import
// both reject anything outside these.
#define LCD_I2C_ADDRESS_DEFAULT 0x27
inline bool isValidLcdI2CAddress(long addr) {
    return (addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F);
}
// The address the LCD is driven at. loadSystemConfig() corrects an
// out-of-range stored value (a pre-#590 EEPROM could hold any 0x03-0x77, and
// driving an unknown device as an LCD could write to it); this guards the
// callers that run before or without a load.
inline uint8_t effectiveLcdI2CAddress() {
    return isValidLcdI2CAddress(systemConfig.lcdI2CAddress)
               ? systemConfig.lcdI2CAddress : (uint8_t)LCD_I2C_ADDRESS_DEFAULT;
}

// Initialization and persistence
void initSystemConfig();         // Load from EEPROM or set defaults
void flushSystemConfigBootDiagnostics();  // Emit buffered boot messages (call after routingApplyConfig()/transportsBegin())
bool saveSystemConfig(bool verbose = true);  // Save to EEPROM (verbose=false for background saves)
bool loadSystemConfig();         // Load from EEPROM
// Sections resetSystemConfig() leaves alone. Wiring, not configuration:
// SYSTEM RESET keeps the link it may have arrived over (#549).
enum ResetKeep : uint8_t {
    RESET_KEEP_NONE = 0,
    RESET_KEEP_LINK = 0x01,       // serial ports (enabled, baud, framed) + console and log transports
    RESET_KEEP_HARDWARE = 0x02,   // buses (I2C/SPI/CAN) + display (#551)
};
void resetSystemConfig(uint8_t keep = RESET_KEEP_NONE);  // Reset to defaults, except `keep`
void applyLogFilterConfig();      // Make the live log filter match systemConfig.logFilter (#561)
uint8_t calculateChecksum(SystemConfig* cfg);
void printSystemStatus();

// Sea-level reference for BME280 elevation (#574). seaLevelPressureHpa()
// returns the stored value, or the config.h default when the stored one is
// out of range (an EEPROM image saved before SYSTEM SEA_LEVEL checked input).
// Accepted range: recorded sea-level extremes are 870 and 1084.8 hPa, so
// anything outside is a typo. Inline so json_config links without this .cpp.
#define SEA_LEVEL_PRESSURE_MIN_HPA 850.0f
#define SEA_LEVEL_PRESSURE_MAX_HPA 1100.0f
inline bool seaLevelPressureValid(float hPa) {
    // Written so NaN fails both comparisons and is rejected.
    return hPa >= SEA_LEVEL_PRESSURE_MIN_HPA && hPa <= SEA_LEVEL_PRESSURE_MAX_HPA;
}
float seaLevelPressureHpa();
// busNeeds: the stored configuration's bus vote (BUS_NEED_I2C/BUS_NEED_SPI,
// see bus_needs.h), computed by main.cpp BEFORE this call and reused later at
// acquireRequiredBuses() — gates the Tier-1 *_WIRED_BUS reservation on the
// bus actually being needed, not just declared (#394).
void registerSystemPins(uint8_t busNeeds);   // Register system pins in pin registry
bool sdCsClaimRefusedTableFull();             // registerSystemPins() refused SD CS for a full pin table (#599)

#endif // SYSTEM_CONFIG_H
