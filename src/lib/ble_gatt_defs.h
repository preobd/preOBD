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

#define PREOBD_BLE_PROTOCOL_VERSION  1

// =============================================================================
// preOBD GATT UUIDs
//
// Base UUID: 4f424400-7072-6542-4244-000000000000
//            "OBD"    "pr" "eB" "BD"
//
// All preOBD-specific UUIDs share this base with the last 4 bytes varying.
// =============================================================================

// --- Tier 1: preOBD Control Service (text command interface) -----------------

// Service UUID: 4f424400-7072-6542-4244-000000000001
#define PREOBD_SERVICE_UUID           "4f424400-7072-6542-4244-000000000001"

// Command TX (Write / Write Without Response)
// Client writes text commands here (e.g., "LIST INPUTS\n")
// UUID: 4f424400-7072-6542-4244-000000000010
#define PREOBD_CHAR_CMD_TX_UUID       "4f424400-7072-6542-4244-000000000010"

// Command RX (Notify / Read)
// Device sends text command responses here
// Client buffers until "preOBD> " prompt to detect end-of-response
// UUID: 4f424400-7072-6542-4244-000000000011
#define PREOBD_CHAR_CMD_RX_UUID       "4f424400-7072-6542-4244-000000000011"

// System Status (Read / Notify)
// 32-byte binary struct with device info (see PreobdSystemStatus below)
// UUID: 4f424400-7072-6542-4244-000000000020
#define PREOBD_CHAR_STATUS_UUID       "4f424400-7072-6542-4244-000000000020"

// --- Tier 2: preOBD Extended Service (binary, OTA — spec only) ---------------

// Service UUID: 4f424400-7072-6542-4244-000000000002
#define PREOBD_EXT_SERVICE_UUID       "4f424400-7072-6542-4244-000000000002"

// Alarm Notify (Notify) — push alarm events to client
#define PREOBD_CHAR_ALARM_UUID        "4f424400-7072-6542-4244-000000000030"

// Relay Control (Read / Write / Notify) — binary relay state
#define PREOBD_CHAR_RELAY_UUID        "4f424400-7072-6542-4244-000000000031"

// OTA Control (Write / Notify) — OTA coordination commands
#define PREOBD_CHAR_OTA_CTRL_UUID     "4f424400-7072-6542-4244-000000000040"

// OTA Data (Write Without Response) — OTA firmware transfer
#define PREOBD_CHAR_OTA_DATA_UUID     "4f424400-7072-6542-4244-000000000041"

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

#endif // BLE_GATT_DEFS_H
