/*
 * system_config.h - System-Wide Configuration Management
 *
 * Manages runtime configuration for outputs, display, timing intervals,
 * and other system-wide settings. Persisted to EEPROM alongside input configs.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>   // offsetof, for the layout static_asserts below
#include "bus_config.h"

// EEPROM memory layout constants
#define SYSTEM_CONFIG_MAGIC 0x5343      // "SC" in ASCII
#define SYSTEM_CONFIG_VERSION 13        // Increment when struct changes (v13: Rule/Action tables moved OUT to the front config-store arena, #296; clean reset from v12)
#define SYSTEM_CONFIG_SIZE sizeof(SystemConfig)

// Total EEPROM bytes available on this platform.
// E2END is the last valid byte index (defined by EEPROM.h on AVR and Teensy 3.x/4.x).
// If a future platform doesn't define E2END, override EEPROM_TOTAL_BYTES in its profile header.
#ifndef EEPROM_TOTAL_BYTES
#define EEPROM_TOTAL_BYTES (E2END + 1)
#endif

// Watchdog scope around an EEPROM save (#541). AVR caps every timeout at 8 s.
#define EEPROM_WDT_WINDOW_MS 20000

// Per-input output mask: all 4 data outputs enabled by default
#define OUTPUT_MASK_ALL_DATA 0x0F

// Output module IDs
// Always define all IDs so EEPROM layout (outputEnabled[], outputInterval[])
// is stable regardless of compile flags. Unused slots are harmless zeros.
enum OutputID {
    OUTPUT_CAN      = 0,
    OUTPUT_REALDASH = 1,
    OUTPUT_SERIAL   = 2,
    OUTPUT_SD       = 3,
    OUTPUT_ALARM    = 4,
    OUTPUT_RELAY    = 5,
    OUTPUT_ELM327   = 6,
    NUM_OUTPUTS     = 7
};

// Display types
enum DisplayType {
    DISPLAY_NONE = 0,
    DISPLAY_LCD = 1,
    DISPLAY_OLED = 2
};

#if INCLUDE_ELM327
#include "../outputs/output_elm327.h"
#endif
#include "../rules/rules_types.h"  // Rule/Action records + INCLUDE_RULES_ENGINE

// System configuration structure
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 validation
    uint8_t version;             // Schema version
    uint8_t checksum;            // XOR checksum

    // Output Modules (12 bytes)
    uint8_t outputEnabled[NUM_OUTPUTS];    // 5 bytes (bool per output)
    uint16_t outputInterval[NUM_OUTPUTS];  // 10 bytes (interval ms)

    // Display Settings (7 bytes)
    uint8_t displayEnabled;      // Display on/off (bool)
    uint8_t displayType;         // LCD/OLED/None (DisplayType enum)
    uint8_t lcdI2CAddress;       // I2C address (default 0x27)
    uint8_t defaultTempUnits;    // Unit index: 0=Celsius, 1=Fahrenheit
    uint8_t defaultPressUnits;   // Unit index: 2=Bar, 3=PSI, 4=kPa
    uint8_t defaultElevUnits;    // Unit index: 9=Meters, 10=Feet
    uint8_t defaultSpeedUnits;   // Unit index: 11=KPH, 12=MPH

    // Timing Intervals (8 bytes)
    uint16_t sensorReadInterval;
    uint16_t alarmCheckInterval;
    uint16_t lcdUpdateInterval;
    // These two bytes were a single `uint16_t reserved1`. Splitting them keeps
    // the struct's size, field offsets, alignment, and XOR checksum byte-for-byte
    // identical, so an EEPROM image written before this field existed stays
    // valid and SYSTEM_CONFIG_VERSION does NOT need a bump (a bump resets the
    // arena to defaults and would wipe the user's configured inputs).
    // A legacy image reads back 0 here, which is why SDLOG_FORMAT_WIDE is 0.
    uint8_t sdLogFormat;         // SdLogFormat enum (output_format.h)
    // Alarm acknowledgement policy (#433) — the former spare byte, same rule:
    // a legacy image reads 0, which decodes to the built-in default (buzzer
    // only, SILENCE_DURATION). Encoded, see ALARM_SILENCE_* in outputs/output_alarm.h:
    //   bit 7      SCOPE ALL — an acknowledged alarm also stops the LED blink
    //   bits 0..6  duration code: 0 = default, 1..126 = n*5 s, 127 = until clear
    uint8_t alarmSilence;

    // Physical Constants (4 bytes)
    float seaLevelPressure;      // hPa for altitude

    // Transport Router Configuration (16 bytes) - NEW in v4
    struct {
        uint8_t control_primary;     // TransportID for CONTROL plane
        uint8_t control_secondary;   // Multi-cast target or TRANSPORT_NONE
        uint8_t data_primary;        // TransportID for DATA plane
        uint8_t data_secondary;      // Multi-cast target or TRANSPORT_NONE
        uint8_t debug_primary;       // TransportID for DEBUG plane
        uint8_t debug_secondary;     // Multi-cast target or TRANSPORT_NONE
        uint8_t bt_type;             // BluetoothType enum (0=none)
        uint8_t bt_auth_required;    // 0=disabled, 1=enabled
        uint16_t bt_pin;             // 4-digit PIN (0=not set)
        uint8_t reserved_router[6];  // Future expansion
    } router;

    // NB (v13): the control-engine Rule/Action tables are NO LONGER embedded
    // here. They are standalone RAM globals (rules_manager.cpp) persisted by
    // the shared config-store arena at the FRONT of EEPROM (input_manager.cpp),
    // so the table size no longer competes with this back-anchored block and is
    // bounded by the EEPROM budget rather than an arbitrary MAX_RULES cap (#296).

    // Bus Configuration (16 bytes) - Simplified "pick one" model
    BusConfig buses;

    // Serial Port Configuration (16 bytes) - Which serial ports are enabled
    SerialPortConfig serial;

    // Log Filter Configuration (12 bytes) - NEW in v6
    struct {
        uint8_t control_level;    // LogLevel for CONTROL plane
        uint8_t data_level;       // LogLevel for DATA plane
        uint8_t debug_level;      // LogLevel for DEBUG plane (default: INFO)
        uint32_t enabledTags;     // 32-bit bitmap for tag filtering (all enabled by default)
        uint8_t reserved[5];      // Future expansion
    } logFilter;
};

// sdLogFormat and alarmSilence replaced a single `uint16_t reserved1` in place, so
// that no SYSTEM_CONFIG_VERSION bump (and therefore no EEPROM reset of the
// user's configured inputs) was needed to add the field. That only holds while
// the two stay a contiguous pair at the original 2-aligned offset — separating
// them, or inserting anything between, shifts every field below and silently
// invalidates existing images while the version number still claims v13.
static_assert(offsetof(SystemConfig, alarmSilence) == offsetof(SystemConfig, sdLogFormat) + 1,
              "sdLogFormat and alarmSilence must stay adjacent (they replace one uint16_t)");
static_assert(offsetof(SystemConfig, sdLogFormat) % 2 == 0,
              "sdLogFormat must sit at the replaced uint16_t's aligned offset");

// SystemConfig is anchored at the end of EEPROM so its address adapts to the
// platform automatically. The input block grows from address 0; a static_assert
// in input_manager.cpp keeps the two from overlapping.
constexpr size_t SYSTEM_CONFIG_ADDRESS = EEPROM_TOTAL_BYTES - sizeof(SystemConfig);
static_assert(EEPROM_TOTAL_BYTES >= sizeof(SystemConfig) + 16,
              "EEPROM too small to hold SystemConfig");

// Global system config instance
extern SystemConfig systemConfig;

// Initialization and persistence
void initSystemConfig();         // Load from EEPROM or set defaults
void flushSystemConfigBootDiagnostics();  // Emit buffered boot messages (call after router.begin())
bool saveSystemConfig(bool verbose = true);  // Save to EEPROM (verbose=false for background saves)
bool loadSystemConfig();         // Load from EEPROM
// Sections resetSystemConfig() leaves alone. Wiring, not configuration:
// SYSTEM RESET keeps the link it may have arrived over (#549).
enum ResetKeep : uint8_t {
    RESET_KEEP_NONE = 0,
    RESET_KEEP_LINK = 0x01,       // serial ports (enabled, baud, framed) + transport router
    RESET_KEEP_HARDWARE = 0x02,   // buses (I2C/SPI/CAN, ELM327 port) + display (#551)
};
void resetSystemConfig(uint8_t keep = RESET_KEEP_NONE);  // Reset to defaults, except `keep`
void applyLogFilterConfig();      // Make the live log filter match systemConfig.logFilter (#561)
uint8_t calculateChecksum(SystemConfig* cfg);
void printSystemStatus();
// busNeeds: the stored configuration's bus vote (BUS_NEED_I2C/BUS_NEED_SPI,
// see bus_needs.h), computed by main.cpp BEFORE this call and reused later at
// acquireRequiredBuses() — gates the Tier-1 *_WIRED_BUS reservation on the
// bus actually being needed, not just declared (#394).
void registerSystemPins(uint8_t busNeeds);   // Register system pins in pin registry

#endif // SYSTEM_CONFIG_H
