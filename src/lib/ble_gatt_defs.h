/*
 * ble_gatt_defs.h - preOBD BLE GATT Profile Definitions
 *
 * Defines UUIDs, binary struct formats, and protocol constants for the
 * preOBD BLE GATT profile. This file is the shared reference for:
 *   - nRF52840 coprocessor firmware (implements these services)
 *   - Web Bluetooth webapp (connects to these UUIDs)
 *   - Future native BLE implementations (ESP32 NimBLE, etc.)
 *
 * Two-tier architecture:
 *   Tier 1: Text commands over GATT (universal, any BLE hardware)
 *   Tier 2: Binary characteristics + OTA (nRF coprocessor, spec only)
 *
 * See docs/architecture/BLE_GATT_PROFILE.md for the full specification.
 */

#ifndef BLE_GATT_DEFS_H
#define BLE_GATT_DEFS_H

#include <stdint.h>

// =============================================================================
// Protocol Version
// =============================================================================

#define PREOBD_BLE_PROTOCOL_VERSION  2

// =============================================================================
// preOBD GATT UUIDs
//
// Base UUID: 4f424400-7072-654f-4244-000000000000
//            "OBD"    "pr" "eO" "BD"
//
// All preOBD-specific UUIDs share this base with the last 4 bytes varying.
// =============================================================================

// --- Tier 1: preOBD Control Service (text command interface) -----------------

// Service UUID: 4f424400-7072-654f-4244-000000000001
#define PREOBD_SERVICE_UUID           "4f424400-7072-654f-4244-000000000001"

// Command TX (Write / Write Without Response)
// Client writes text commands here (e.g., "LIST INPUTS\n")
// UUID: 4f424400-7072-654f-4244-000000000010
#define PREOBD_CHAR_CMD_TX_UUID       "4f424400-7072-654f-4244-000000000010"

// Command RX (Notify / Read)
// Device sends text command responses here
// Client buffers until "preOBD> " prompt to detect end-of-response
// UUID: 4f424400-7072-654f-4244-000000000011
#define PREOBD_CHAR_CMD_RX_UUID       "4f424400-7072-654f-4244-000000000011"

// System Status (Read / Notify)
// 32-byte binary struct with device info (see PreobdSystemStatus below)
// UUID: 4f424400-7072-654f-4244-000000000020
#define PREOBD_CHAR_STATUS_UUID       "4f424400-7072-654f-4244-000000000020"

// --- Tier 2: preOBD Extended Service (binary, OTA — spec only) ---------------

// Service UUID: 4f424400-7072-654f-4244-000000000002
#define PREOBD_EXT_SERVICE_UUID       "4f424400-7072-654f-4244-000000000002"

// Alarm Notify (Notify) — push alarm events to client
#define PREOBD_CHAR_ALARM_UUID        "4f424400-7072-654f-4244-000000000030"

// Action Control (Read / Write / Notify) — binary Action state (PreobdActionState).
// UUID macro name kept for compatibility; payload is now PreobdActionState (a
// relay is an Action of type RELAY, §11). The legacy PreobdRelayState struct
// below is retained, deprecated, only until the nRF/webapp are confirmed clear.
#define PREOBD_CHAR_RELAY_UUID        "4f424400-7072-654f-4244-000000000031"

// Live Values (Read / Notify) — per-input live sensor readings
// (PreobdLiveValue, framed type 0x06). Read returns the most recent frame.
#define PREOBD_CHAR_LIVE_UUID         "4f424400-7072-654f-4244-000000000032"

// OTA Control (Write / Notify) — OTA coordination commands
#define PREOBD_CHAR_OTA_CTRL_UUID     "4f424400-7072-654f-4244-000000000040"

// OTA Data (Write Without Response) — OTA firmware transfer
#define PREOBD_CHAR_OTA_DATA_UUID     "4f424400-7072-654f-4244-000000000041"

// --- Standard Services -------------------------------------------------------

// Nordic UART Service (NUS) — backward compat with BLE terminal apps
#define NUS_SERVICE_UUID              "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_CHAR_RX_UUID              "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_CHAR_TX_UUID              "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// HM-10 / CC2541 transparent BLE UART modules — webapp fallback only
// These modules expose a single characteristic for bidirectional transparent UART.
// Cannot host custom GATT services; no nRF firmware equivalent.
// Defined here for reference consistency with the webapp.
#define HM10_SERVICE_UUID    "0000ffe0-0000-1000-8000-00805f9b34fb"
#define HM10_CHAR_UUID       "0000ffe1-0000-1000-8000-00805f9b34fb"

// Device Information Service: 0x180A (standard BLE SIG service)
// Characteristics: Manufacturer Name (0x2A29), Firmware Revision (0x2A26),
//                  Model Number (0x2A24)

// =============================================================================
// System Status Characteristic — Binary Format (32 bytes)
// =============================================================================

#define PREOBD_STATUS_MAGIC  0x704F4244  // "pOBD" (little-endian: 0x44, 0x42, 0x4F, 0x70)
#define PREOBD_STATUS_SIZE   32

// Response framing: command responses end with this prompt string
#define PREOBD_RESPONSE_PROMPT  "preOBD> "

#ifdef __cplusplus

#pragma pack(push, 1)

struct PreobdSystemStatus {
    uint32_t magic;            // Bytes 0-3:   PREOBD_STATUS_MAGIC
    uint8_t  protocolVersion;  // Byte 4:      PREOBD_BLE_PROTOCOL_VERSION
    uint8_t  systemMode;       // Byte 5:      0 = CONFIG, 1 = RUN
    uint8_t  activeInputCount; // Byte 6:      Number of active inputs
    uint8_t  relayCount;       // Byte 7:      Number of configured relays
    uint32_t uptimeSeconds;    // Bytes 8-11:  Uptime in seconds (little-endian)
    uint8_t  versionMajor;     // Byte 12:     Firmware major version
    uint8_t  versionMinor;     // Byte 13:     Firmware minor version
    uint8_t  versionPatch;     // Byte 14:     Firmware patch version
    uint8_t  versionBuild;     // Byte 15:     Build number, low byte only (wraps silently at 256; display-only)
    char     deviceName[16];   // Bytes 16-31: Device name (null-terminated)
};

#pragma pack(pop)

// Compile-time check
static_assert(sizeof(PreobdSystemStatus) == PREOBD_STATUS_SIZE,
              "PreobdSystemStatus must be exactly 32 bytes");

#endif // __cplusplus

// =============================================================================
// System Mode Constants
// =============================================================================

#define PREOBD_MODE_CONFIG  0
#define PREOBD_MODE_RUN     1

// =============================================================================
// Alarm Notify Characteristic — Binary Format (19 bytes)
//
// Pushed via framed transport (type 0x04) on every per-input alarm-severity
// transition, including return-to-normal (severity = NORMAL, kind = CLEAR).
// =============================================================================

#define PREOBD_ALARM_EVENT_SIZE 19

#define PREOBD_ALARM_SEVERITY_NORMAL  0
#define PREOBD_ALARM_SEVERITY_WARNING 1
#define PREOBD_ALARM_SEVERITY_ALARM   2
#define PREOBD_ALARM_SEVERITY_INFO    3   // log-only notification (no LED/buzzer);
                                          // appended so the wire stays additive —
                                          // ordering here is independent of the
                                          // firmware RuleSeverity enum (#308)

#define PREOBD_ALARM_KIND_CLEAR 0
#define PREOBD_ALARM_KIND_HIGH  1   // value at/above maxValue threshold
#define PREOBD_ALARM_KIND_LOW   2   // value at/below minValue threshold

#ifdef __cplusplus

#pragma pack(push, 1)

struct PreobdAlarmEvent {
    uint8_t  protocolVersion;  // Byte 0:      PREOBD_BLE_PROTOCOL_VERSION
    uint8_t  inputIndex;       // Byte 1:      Index into inputs[] (0..MAX_INPUTS-1)
    uint8_t  severity;         // Byte 2:      PREOBD_ALARM_SEVERITY_*
    uint8_t  kind;             // Byte 3:      PREOBD_ALARM_KIND_*
    uint16_t sequence;         // Bytes 4-5:   Monotonic event counter, wraps at 65535
    float    value;            // Bytes 6-9:   Current sensor value (standard units)
    float    threshold;        // Bytes 10-13: Threshold that was violated (0 on CLEAR)
    uint32_t timestampMs;      // Bytes 14-17: millis() at emit time
    uint8_t  ruleId;           // Byte 18:     Rule.id of the firing rule
                               //              (stable per-record id, not a slot;
                               //              CONTROL_ID_NONE=0xFF if none). Lets
                               //              a client key firing state by rule
                               //              so multiple rules on one input are
                               //              distinguishable (#310).
};

#pragma pack(pop)

static_assert(sizeof(PreobdAlarmEvent) == PREOBD_ALARM_EVENT_SIZE,
              "PreobdAlarmEvent must be exactly 19 bytes");

#endif // __cplusplus

// =============================================================================
// Relay Control Characteristic — Binary Format (16 bytes)
//
// Symmetric layout: same struct is pushed (frame type 0x05, notify) when any
// relay's mode/state/threshold changes, and written by clients (Write, then
// re-framed as type 0x05 to the host MCU) to request a new mode + thresholds.
//
// Writable fields:  mode, thresholdOn, thresholdOff. (relayIndex selects the
// target.) All other fields are informational read-only.
// =============================================================================

#define PREOBD_RELAY_STATE_SIZE 16

// Mode values match RelayMode enum in src/outputs/output_relay.h
#define PREOBD_RELAY_MODE_DISABLED   0
#define PREOBD_RELAY_MODE_AUTO_HIGH  1
#define PREOBD_RELAY_MODE_AUTO_LOW   2
#define PREOBD_RELAY_MODE_MANUAL_ON  3
#define PREOBD_RELAY_MODE_MANUAL_OFF 4

#define PREOBD_RELAY_STATE_OFF 0
#define PREOBD_RELAY_STATE_ON  1

#ifdef __cplusplus

#pragma pack(push, 1)

struct PreobdRelayState {
    uint8_t  protocolVersion;  // Byte 0:      PREOBD_BLE_PROTOCOL_VERSION
    uint8_t  relayIndex;       // Byte 1:      Index into relays[] (0..MAX_RELAYS-1)
    uint8_t  mode;             // Byte 2:      PREOBD_RELAY_MODE_* (writable)
    uint8_t  state;            // Byte 3:      PREOBD_RELAY_STATE_* (read-only)
    uint8_t  inputIndex;       // Byte 4:      Bound input index, 0xFF if unassigned (read-only)
    uint8_t  outputPin;        // Byte 5:      Physical pin, 0xFF if unconfigured (read-only)
    uint16_t sequence;         // Bytes 6-7:   Monotonic event counter, wraps at 65535
    float    thresholdOn;      // Bytes 8-11:  Activation threshold (writable)
    float    thresholdOff;     // Bytes 12-15: Deactivation threshold (writable)
};

#pragma pack(pop)

static_assert(sizeof(PreobdRelayState) == PREOBD_RELAY_STATE_SIZE,
              "PreobdRelayState must be exactly 16 bytes");

#endif // __cplusplus

// =============================================================================
// Action Control Characteristic — Binary Format (16 bytes)
//
// Supersedes PreobdRelayState (a relay is now an Action of type RELAY, §11).
// Pushed via framed transport (type 0x05, notify) on every Action state/config
// change, and written by clients to toggle a manual Action.
//
// Read-only fields: actionId, type, state, target, ruleRef, ruleCount, behavior,
// failsafe, overrideFlags.
// Writable: command (PREOBD_ACTION_CMD_*) — actionId selects the target Action.
// MANUAL_ON/MANUAL_OFF drive an unlinked Action's manual latch; on a rule-driven
// Action they instead set an operator override that outranks the Rule and the
// failsafe, released with CMD_AUTO (#329). The override is RAM-only on the
// device and reverts to AUTO on reboot; it is reported in overrideFlags.
// =============================================================================

#define PREOBD_ACTION_STATE_SIZE 16
#define PREOBD_ACTION_ID_NONE    0xFF   // unconfigured slot / no triggering rule

// type — matches ActionType in src/control/control_types.h
#define PREOBD_ACTION_TYPE_RELAY   0
#define PREOBD_ACTION_TYPE_BEEPER  1
#define PREOBD_ACTION_TYPE_LED     2
#define PREOBD_ACTION_TYPE_DISPLAY 3

#define PREOBD_ACTION_STATE_OFF 0
#define PREOBD_ACTION_STATE_ON  1

#define PREOBD_ACTION_FAILSAFE_OFF 0
#define PREOBD_ACTION_FAILSAFE_ON  1

// Inbound write command (in the `command` field; reads back as NONE)
#define PREOBD_ACTION_CMD_NONE       0
#define PREOBD_ACTION_CMD_MANUAL_ON  1
#define PREOBD_ACTION_CMD_MANUAL_OFF 2
#define PREOBD_ACTION_CMD_AUTO       3   // release an operator override (#329)

// overrideFlags — additive into a previously-reserved byte, so the protocol
// version stays 2; older clients simply ignore it.
#define PREOBD_ACTION_OVR_MASK           0x03  // bits 0-1: override state
#define PREOBD_ACTION_OVR_AUTO           0
#define PREOBD_ACTION_OVR_FORCE_ON       1
#define PREOBD_ACTION_OVR_FORCE_OFF      2
#define PREOBD_ACTION_OVR_SOURCE_FAULTED 0x04  // bit 2: a member rule's source untrustworthy

// ruleCount — how many Rules OR-drive this Action (§11.6, #343). Additive into a
// previously-reserved byte, so the protocol version stays 2. `ruleRef` remains the
// FIRST member, so a client that predates this byte still shows a valid link; a
// client that reads it renders "N rules" and asks SYSTEM DUMP JSON for the list
// (membership is edited on the text/JSON config plane, not through this frame).
// A pre-#343 device sends 0 here even for a linked Action — treat
// `ruleCount == 0 && ruleRef != 0xFF` as "one member" for compatibility.

#ifdef __cplusplus

#pragma pack(push, 1)

struct PreobdActionState {
    uint8_t  protocolVersion;  // Byte 0:      PREOBD_BLE_PROTOCOL_VERSION
    uint8_t  actionId;         // Byte 1:      stable Action id (0xFF = unconfigured)
    uint8_t  type;             // Byte 2:      PREOBD_ACTION_TYPE_* (read-only)
    uint8_t  state;            // Byte 3:      PREOBD_ACTION_STATE_* current driven (read-only)
    uint8_t  target;           // Byte 4:      pin / channel (read-only)
    uint8_t  ruleRef;          // Byte 5:      FIRST member Rule id, 0xFF = manual (read-only)
    uint16_t sequence;         // Bytes 6-7:   Monotonic event counter, wraps at 65535
    uint8_t  behavior;         // Byte 8:      bit0 = invert/active-low (read-only)
    uint8_t  failsafe;         // Byte 9:      PREOBD_ACTION_FAILSAFE_* (RELAY only, read-only)
    uint8_t  command;          // Byte 10:     PREOBD_ACTION_CMD_* (writable; reads back NONE)
    uint8_t  overrideFlags;    // Byte 11:     PREOBD_ACTION_OVR_* (read-only, #329)
    uint8_t  ruleCount;        // Byte 12:     member Rules (0 = manual, >1 = OR-driven, #343)
    uint8_t  reserved[3];      // Bytes 13-15: future use
};

#pragma pack(pop)

static_assert(sizeof(PreobdActionState) == PREOBD_ACTION_STATE_SIZE,
              "PreobdActionState must be exactly 16 bytes");

#endif // __cplusplus

// =============================================================================
// Live Values Characteristic — Binary Format (16 bytes)
//
// One frame per inputs[] slot, pushed via framed transport (type 0x06) on
// value/flags/units change (500 ms scan) plus a 5 s heartbeat re-send of all
// slots for late subscribers. A client that subscribes mid-session waits at
// most one heartbeat; the characteristic Read returns the most recent frame.
//
// `value` is in DISPLAY units (already through convertFromBaseUnits()), so a
// client renders it verbatim against the input's units string from
// SYSTEM DUMP JSON. Contrast PreobdAlarmEvent value/threshold: BASE units.
// `inputIndex` joins to the dump's per-input "idx" key.
// =============================================================================

#define PREOBD_LIVE_VALUE_SIZE 16

#define PREOBD_LIVE_FLAG_ENABLED 0x01  // slot occupied and input enabled
#define PREOBD_LIVE_FLAG_VALID   0x02  // reading valid (0 = sensor fault / non-finite)

#ifdef __cplusplus

#pragma pack(push, 1)

struct PreobdLiveValue {
    uint8_t  protocolVersion;  // Byte 0:      PREOBD_BLE_PROTOCOL_VERSION
    uint8_t  inputIndex;       // Byte 1:      inputs[] slot (== DUMP JSON "idx")
    uint8_t  flags;            // Byte 2:      PREOBD_LIVE_FLAG_*
    uint8_t  unitsIndex;       // Byte 3:      UNITS_REGISTRY index at emit time
    uint16_t sequence;         // Bytes 4-5:   Monotonic event counter, wraps at 65535
    uint16_t reserved;         // Bytes 6-7:   0 (future use)
    float    value;            // Bytes 8-11:  DISPLAY units; 0.0f when !VALID
    uint32_t timestampMs;      // Bytes 12-15: millis() at emit time
};

#pragma pack(pop)

static_assert(sizeof(PreobdLiveValue) == PREOBD_LIVE_VALUE_SIZE,
              "PreobdLiveValue must be exactly 16 bytes");

#endif // __cplusplus

#endif // BLE_GATT_DEFS_H
