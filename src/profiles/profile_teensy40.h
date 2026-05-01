/*
 * profile_teensy40.h — Build profile for Teensy 4.0
 *
 * This file is the authoritative board definition for the Teensy 4.0.
 * It owns all feature flags AND all hardware pin assignments.
 * config.h contains only application-level constants (timing, thresholds, etc.).
 *
 * Pin convention:
 *   ENABLE_X = 1          feature enabled (boolean; no hardware pin)
 *   ENABLE_X = 1 + X_PIN  feature enabled with a physical pin
 *   ENABLE_X not defined   feature disabled
 *
 * Differences from Teensy 4.1:
 *   - No built-in SD (ENABLE_SD_LOGGING disabled)
 *   - No mode button wired on this board
 *   - No buzzer wired (ENABLE_ALARMS=1 for software alarms; ALARMS_PIN absent)
 *   - ENABLE_LED and ENABLE_TEST_MODE disabled
 *
 * RAM budget same as Teensy 4.1 (512 KB).
 * Teensy 4.0 has only 1080 bytes of EEPROM, so the EEPROM-persistence cap
 * (MAX_EEPROM_INPUTS, derived in input_manager.cpp) is roughly 10 inputs —
 * extras stay in RAM but won't survive a reboot.
 */


// ===== FEATURE FLAGS =====
#define ENABLE_CAN             1
#define ENABLE_REALDASH        1
#define ENABLE_SERIAL_OUTPUT   1
//#define ENABLE_SD_LOGGING    // no SD hardware on this board
#define ENABLE_LCD             1
#define ENABLE_ALARMS          1    // software alarms active; no buzzer wired (ALARMS_PIN absent)
//#define ENABLE_LED           // not wired
//#define ENABLE_TEST_MODE     // not needed
#define ENABLE_BME280          1
#define ENABLE_RELAY_OUTPUT    1
#define MAX_RELAYS             2
#define ENABLE_ELM327          1

// ===== HARDWARE PIN ASSIGNMENTS =====
// ENABLE_MODE_BUTTON not defined — no button on this board
// ALARMS_PIN not defined — no buzzer wired; alarms still trigger over CAN/LCD
// SUPPORTS_SD not defined — no SD hardware on this board
// SD_PIN not defined — saveConfigToSD / loadConfigFromSD are not compiled in
// RGB_PIN_* not defined — LED disabled


// ===== SIZING =====
#define MAX_INPUTS              40
#define MAX_PID_ENTRIES         64
#define MAX_PIN_REGISTRY        64
#define CAN_CACHE_SIZE          16
#define MAX_SCAN_RESULTS        32

// ===== CLI BUFFER SIZES =====
#define CLI_RX_BUFFER_SIZE      128
#define CLI_CMD_BUFFER_SIZE     128
#define CLI_HISTORY_BUFFER_SIZE 64
#define CLI_MAX_BINDINGS        32
#define CLI_BUFFER_SIZE         1280
#define CLI_MAX_ARGS            16
#define CLI_MAX_ARG_LEN         32

// ===== CAN CONTROLLER =====
#define PROFILE_HAS_NATIVE_CAN 1

// ===== JSON CAPABILITY =====
#define SUPPORTS_JSON_CONFIG        1
#define SUPPORTS_JSON_IMPORT_STREAM SUPPORTS_JSON_CONFIG
#define SUPPORTS_JSON_EXPORT        SUPPORTS_JSON_CONFIG
#define JSON_IMPORT_MAX_BYTES       16384
