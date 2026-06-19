# BLE GATT Profile Specification

This document defines the preOBD BLE GATT profile — the services, characteristics, and protocols that enable phone apps and web browsers to configure and control preOBD over Bluetooth Low Energy.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [GATT Profile](#gatt-profile)
4. [Tier 1: Text Command Protocol](#tier-1-text-command-protocol)
5. [Tier 2: Extended Binary Protocol](#tier-2-extended-binary-protocol)
6. [Coprocessor UART Protocol](#coprocessor-uart-protocol)
7. [Web Bluetooth Integration](#web-bluetooth-integration)
8. [Security](#security)
9. [Implementation Notes](#implementation-notes)
10. [Reference](#reference)

## Overview

### Two-Tier Strategy

The BLE interface uses a two-tier architecture:

**Tier 1 — Text Commands over GATT (Universal)**
- Works with any BLE hardware: nRF52840 dev boards, Bluefruit modules, HM-10, ESP32
- Reuses the existing preOBD text command interface (SET, RELAY, OUTPUT, LIST, INFO, etc.)
- Custom preOBD GATT service UUID enables device discovery by apps
- Web Bluetooth webapp provides browser-based configuration
- No changes to preOBD firmware — the BLE device bridges GATT↔UART transparently

**Tier 2 — Extended Binary Protocol (Supported Hardware)**
- Requires a BLE-capable coprocessor (nRF52840) running dedicated firmware
- Adds: OTA firmware updates, alarm push notifications, binary relay control, security
- Defined in this spec, built later

### What This Is NOT

This is a **configuration and control** interface, not a data visualization dashboard. Streaming sensor data for gauges and dashboards is handled by RealDash (CAN/serial), not BLE. The BLE interface is for:

- Configuring inputs, sensors, and calibration
- Controlling relays and outputs
- Reading system status and diagnostics
- Managing alarms
- Firmware updates (Tier 2)

## Architecture

### Tier 1 Data Flow

```
Phone / Web Browser
        │
        │  BLE GATT (preOBD Control Service)
        ▼
nRF52840 board (or any BLE device)
        │
        │  UART (TX/RX, 115200 baud)
        ▼
preOBD main MCU (Teensy, STM32, Mega, etc.)
        │
        │  Existing SerialTransport
        ▼
Message Router → Command Dispatch → Response
```

The nRF52840 acts as a BLE-to-UART bridge with the preOBD GATT profile. preOBD sees it as a standard serial port — no new transport code needed. Any nRF52840 board (Adafruit Feather, Sparkfun, custom PCB) works with two wires (TX/RX) to a hardware serial port.

### Tier 2 Data Flow

```
Phone / Web Browser
        │
        │  BLE GATT (Control + Extended Services)
        ▼
nRF52840 coprocessor
        │
        │  Framed UART protocol
        ▼
preOBD main MCU
```

Tier 2 adds a framing layer on the UART link so commands, OTA data, status updates, and alarm events can share the same serial connection without collision.

## GATT Profile

### UUID Scheme

All preOBD-specific UUIDs use a common 128-bit base:

```
Base: 4f424400-7072-654f-4244-00000000XXXX
      "OBD"    "pr" "eO" "BD"          ^^^^-- varies per characteristic
```

Defined in `src/lib/ble_gatt_defs.h`.

### Service Map

| Service | UUID | Tier | Purpose |
|---------|------|------|---------|
| preOBD Control | `4f424400-7072-654f-4244-000000000001` | 1 | Text command interface + system status |
| preOBD Extended | `4f424400-7072-654f-4244-000000000002` | 2 | OTA, alarms, binary relay control |
| Nordic UART (NUS) | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | 1 | Backward compat with BLE terminal apps |
| Device Information | `0x180A` | 1 | Standard BLE SIG device info |

### Characteristics — preOBD Control Service

| Characteristic | UUID (last 4 bytes) | Properties | Description |
|---------------|---------------------|------------|-------------|
| Command TX | `...0010` | Write, Write Without Response | Client writes text commands |
| Command RX | `...0011` | Notify, Read | Device sends text responses |
| System Status | `...0020` | Read, Notify | 32-byte binary device info |

### Characteristics — preOBD Extended Service (Tier 2)

| Characteristic | UUID (last 4 bytes) | Properties | Description |
|---------------|---------------------|------------|-------------|
| Alarm Notify | `...0030` | Notify | Push alarm events to client |
| Relay Control | `...0031` | Read, Write, Notify | Binary relay state and control |
| OTA Control | `...0040` | Write, Notify | OTA session coordination |
| OTA Data | `...0041` | Write Without Response | OTA firmware data transfer |

### Characteristics — Nordic UART Service (NUS)

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| NUS RX | `6e400002-...` | Write, Write Without Response | Client writes (same as serial TX) |
| NUS TX | `6e400003-...` | Notify | Device sends (same as serial RX) |

NUS provides a transparent serial pipe. It exists for backward compatibility with generic BLE terminal apps (nRF Connect, Serial Bluetooth Terminal, etc.). Both NUS and the preOBD Command TX/RX carry the same text command traffic.

### Characteristics — Device Information Service (0x180A)

| Characteristic | UUID | Value |
|---------------|------|-------|
| Manufacturer Name | `0x2A29` | `"preOBD"` |
| Firmware Revision | `0x2A26` | From `version.h` (e.g., `"0.7.0-beta"`) |
| Model Number | `0x2A24` | Platform identifier (e.g., `"Teensy 4.1"`, `"STM32F4"`) |

## Tier 1: Text Command Protocol

### Command Format

Commands are plain ASCII text, identical to the existing serial command interface:

```
COMMAND [SUBCOMMAND] [ARG1] [ARG2] ...\n
```

The client writes the command string (including trailing `\n`) to the **Command TX** characteristic (or NUS RX). The device processes it through the existing command dispatch and sends the response via **Command RX** notifications (or NUS TX).

### Response Framing

Responses are multi-line ASCII text terminated by the prompt string:

```
preOBD>
```

The client buffers incoming notifications until it detects `preOBD> ` (8 characters including trailing space), which marks the end of the response. This is the same prompt that the embedded-cli library prints after every command.

**Example exchange:**

```
Client writes:  VERSION\n

Device notifies (may be split across multiple BLE packets):
  ========================================\r\n
    Firmware: 0.7.0-beta\r\n
    Build: 12345\r\n
    Git: abc1234\r\n
    EEPROM Version: 3\r\n
    Active Inputs: 5/20\r\n
  ========================================\r\n
  \r\n
  preOBD>
```

### Response Parsing

- Lines are terminated with `\r\n` (CRLF)
- Error responses start with `ERROR:`
- Success confirmations are plain text (e.g., `Relay 0 output pin set to 13`)
- Bordered sections use `========` lines
- The prompt `preOBD> ` always follows the last line of output

### MTU Considerations

BLE has a limited packet size. The default ATT MTU is 23 bytes (20 bytes payload). After MTU negotiation, modern devices typically support 247+ bytes.

- **Commands:** Most commands fit in a single write (under 50 bytes). Long commands may need segmentation by the BLE stack (handled automatically by most BLE libraries).
- **Responses:** Multi-line responses are sent as a series of notifications. The client must reassemble them. Each notification contains a chunk of the response text, not necessarily aligned to line boundaries.

### Available Commands

All existing preOBD serial commands work over BLE. Key commands for BLE control:

| Command | Mode | Description |
|---------|------|-------------|
| `HELP` | Any | List available commands |
| `VERSION` | Any | Show firmware version and system info |
| `CONFIG` | Any | Enter configuration mode |
| `RUN` | Any | Enter run mode |
| `LIST INPUTS` | Any | Show all configured inputs |
| `LIST APPLICATIONS` | Any | Show available sensor type presets |
| `LIST SENSORS` | Any | Show available sensor hardware |
| `LIST OUTPUTS` | Any | Show output module status |
| `INFO <pin>` | Any | Show input details for a pin |
| `SET <pin> ...` | CONFIG | Configure an input |
| `RELAY LIST` | Any | Show relay status |
| `RELAY <n> MODE <mode>` | CONFIG | Set relay mode |
| `OUTPUT <name> ENABLE` | CONFIG | Enable an output module |
| `OUTPUT <name> DISABLE` | CONFIG | Disable an output module |
| `SAVE` | CONFIG | Save configuration to EEPROM |

See `docs/reference/SERIAL_COMMANDS.md` for the complete command reference.

### System Status Characteristic

The System Status characteristic provides a compact 32-byte binary snapshot of the device state. This is more efficient than parsing text from `VERSION` or `SYSTEM STATUS`.

```
Offset  Size  Type       Field
------  ----  --------   -----
0       4     uint32_le  Magic: 0x704F4244 ("pOBD")
4       1     uint8      Protocol version (1)
5       1     uint8      System mode: 0=CONFIG, 1=RUN
6       1     uint8      Active input count
7       1     uint8      Configured relay count
8       4     uint32_le  Uptime in seconds
12      1     uint8      Firmware major version
13      1     uint8      Firmware minor version
14      1     uint8      Firmware patch version
15      1     uint8      Build number (low byte)
16      16    char[16]   Device name (null-terminated)
------  ----
Total:  32 bytes
```

The struct is defined as `PreobdSystemStatus` in `src/lib/ble_gatt_defs.h`.

**Read behavior:** Returns the current status snapshot.

**Notify behavior (Tier 2):** The nRF coprocessor notifies the client when system mode changes (CONFIG↔RUN) or on a periodic interval.

## Tier 2: Extended Binary Protocol

> **Status:** Specification only. Not implemented. Requires nRF coprocessor firmware.

Tier 2 adds features that require a real BLE stack and cannot be done through simple UART bridging.

### Alarm Notifications

**Characteristic:** Alarm Notify (`...0030`), Notify

When an alarm triggers (e.g., CHT exceeds threshold), the nRF coprocessor sends a notification:

```
Offset  Size  Type       Field
------  ----  --------   -----
0       1     uint8      Input index
1       1     uint8      Severity: 0=NORMAL, 1=WARNING, 2=ALARM
2       4     float32_le Current value
6       4     float32_le Threshold value
10      8     char[8]    Input abbreviation (null-terminated, e.g., "CHT")
------  ----
Total:  18 bytes
```

### Relay Control

**Characteristic:** Relay Control (`...0031`), Read / Write / Notify

**Read format (per relay, concatenated):**

```
Offset  Size  Type       Field
------  ----  --------   -----
0       1     uint8      Relay index
1       1     uint8      Mode: 0=DISABLED, 1=AUTO_HIGH, 2=AUTO_LOW, 3=MANUAL_ON, 4=MANUAL_OFF
2       1     uint8      Current state: 0=OFF, 1=ON
3       1     uint8      Reserved (padding)
4       4     float32_le Threshold ON
8       4     float32_le Threshold OFF
12      4     float32_le Monitored input value
------  ----
Total:  16 bytes per relay
```

**Write format (set relay mode/thresholds):**

```
Offset  Size  Type       Field
------  ----  --------   -----
0       1     uint8      Relay index
1       1     uint8      Mode
2       2     uint16     Reserved
4       4     float32_le Threshold ON
8       4     float32_le Threshold OFF
------  ----
Total:  12 bytes
```

**Notify:** Sent when relay state changes (ON↔OFF) or after a write.

### OTA Firmware Updates

**Characteristics:** OTA Control (`...0040`) + OTA Data (`...0041`)

OTA follows a simple session-based protocol:

1. **Client → OTA Control (Write):** Start OTA session
   ```
   Byte 0: Command (0x01 = START)
   Bytes 1-4: Total firmware size (uint32_le)
   Bytes 5-8: CRC32 of firmware image
   Byte 9: Target (0x00 = nRF coprocessor, 0x01 = main MCU)
   ```

2. **nRF → OTA Control (Notify):** ACK/NACK
   ```
   Byte 0: Status (0x00 = OK, 0x01 = ERROR, 0x02 = BUSY)
   Bytes 1-4: Max chunk size (uint32_le)
   ```

3. **Client → OTA Data (Write Without Response):** Firmware chunks
   ```
   Bytes 0-1: Chunk sequence number (uint16_le)
   Bytes 2+: Firmware data (up to MTU-5 bytes)
   ```

4. **nRF → OTA Control (Notify):** Progress/completion
   ```
   Byte 0: Status (0x00 = CHUNK_OK, 0x10 = COMPLETE, 0x20 = CRC_FAIL)
   Bytes 1-4: Bytes received so far (uint32_le)
   ```

5. **Client → OTA Control (Write):** Finalize
   ```
   Byte 0: Command (0x02 = FINALIZE)
   ```

For main MCU OTA: the nRF coprocessor receives firmware over BLE, then transfers it to the main MCU over UART using a separate bootloader protocol (platform-specific).

## Coprocessor UART Protocol

> **Status:** Specification only. Required for Tier 2 features.

For Tier 1, the nRF bridges BLE↔UART transparently (raw text passthrough, no framing). For Tier 2, the UART link needs framing so multiple message types can share the connection.

### Frame Format

```
Byte 0:     Start marker: 0xAA
Byte 1:     Message type
Bytes 2-3:  Payload length (uint16_le, max 512)
Bytes 4+:   Payload (variable length)
Last byte:  CRC8 of bytes 1 through end of payload
```

### Message Types

| Type | Direction | Description |
|------|-----------|-------------|
| `0x01` | nRF → MCU | Text command from BLE client |
| `0x02` | MCU → nRF | Text command response |
| `0x03` | MCU → nRF | System status update |
| `0x04` | MCU → nRF | Alarm event |
| `0x05` | MCU → nRF | Relay state change |
| `0x10` | nRF → MCU | Connection event (connect/disconnect) |
| `0x20` | nRF → MCU | OTA data for main MCU |
| `0x21` | MCU → nRF | OTA ACK/progress |
| `0xFF` | Both | Ping/keepalive |

### Backward Compatibility

When the nRF detects that the main MCU does not respond to framed messages (no valid CRC8 responses within 2 seconds of connection), it falls back to Tier 1 transparent passthrough mode. This allows any existing preOBD firmware to work with the nRF coprocessor without modification.

## Web Bluetooth Integration

### Browser Support

| Browser | Platform | Status |
|---------|----------|--------|
| Chrome | Desktop (Windows, macOS, Linux) | Supported |
| Chrome | Android | Supported |
| Edge | Desktop | Supported (Chromium-based) |
| Opera | Desktop | Supported (Chromium-based) |
| Safari | macOS / iOS | Not supported |
| Firefox | All | Not supported |

**iOS workaround:** The third-party browser [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055) supports Web Bluetooth on iOS.

### Requirements

- **HTTPS or localhost:** Web Bluetooth requires a secure context. The webapp must be served over HTTPS or accessed via `localhost` / `127.0.0.1`.
- **User gesture:** `navigator.bluetooth.requestDevice()` must be triggered by a user gesture (button click, etc.).

### Device Discovery

The webapp requests devices advertising the preOBD Control Service UUID:

```javascript
const device = await navigator.bluetooth.requestDevice({
    filters: [
        { services: ['4f424400-7072-654f-4244-000000000001'] },  // preOBD
        { services: ['6e400001-b5a3-f393-e0a9-e50e24dcca9e'] },  // NUS
        { services: ['0000ffe0-0000-1000-8000-00805f9b34fb'] }    // HM-10
    ],
    optionalServices: [
        '4f424400-7072-654f-4244-000000000001',
        '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
        '0000ffe0-0000-1000-8000-00805f9b34fb',
        0x180A
    ]
});
```

After connecting, the webapp probes for the preOBD service (full mode), then NUS (basic terminal mode), then HM-10 FFE0/FFE1 (HM-10 terminal mode). HM-10 devices cannot implement custom GATT services and rely on their proprietary `0xFFE0`/`0xFFE1` characteristic pair.

### Auto-Detection

```
Connect to device
    │
    ├── preOBD Control Service found?
    │     YES → Full mode
    │           • Command TX/RX via preOBD characteristics
    │           • System Status panel visible
    │           • Quick action buttons enabled
    │
    ├── NUS Service found?
    │     YES → Basic terminal mode
    │           • Command TX/RX via NUS characteristics
    │           • System Status panel hidden
    │           • Terminal-only interface
    │
    └── HM-10 Service (0xFFE0) found?
          YES → HM-10 terminal mode
                • Single FFE1 characteristic for both TX and RX
                • System Status panel hidden
                • Terminal-only interface
```

## Security

### Tier 1 (Current)

Tier 1 relies on standard BLE pairing. No application-level authentication. The preOBD text command interface has a mode gate (CONFIG vs. RUN) that prevents accidental configuration changes during operation:

- **RUN mode:** Read-only commands only (LIST, INFO, VERSION, etc.)
- **CONFIG mode:** Full access (SET, RELAY, OUTPUT, SAVE, etc.)
- Mode switch: `CONFIG` / `RUN` commands

### Tier 2 (Future)

Tier 2 should implement:

- **BLE bonding:** Paired devices stored in nRF flash. Only bonded devices can access the Extended Service.
- **Encrypted characteristics:** Extended Service characteristics require an encrypted link (MITM protection).
- **Authenticated writes:** Configuration writes (relay, OTA) require a bonded connection.
- **OTA security:** Firmware images should be signed. The nRF verifies the signature before applying the update.

## Implementation Notes

### nRF52840 (Primary Target)

The nRF52840 is the primary BLE target, either as a standalone dev board or integrated into a custom PCB. It runs its own firmware implementing the GATT profile defined here.

**BLE stack options:**
- **Nordic nRF Connect SDK (Zephyr-based):** Recommended for production. Full BLE 5.x support, DFU/OTA built-in.
- **Arduino + Adafruit Bluefruit library:** Simpler for prototyping on Adafruit nRF52840 boards.
- **Zephyr RTOS (standalone):** Alternative to nRF Connect SDK.

**Hardware connection:**
- UART TX/RX between nRF and main MCU at 115200 baud (configurable)
- Optional: GPIO for reset control, flow control (CTS/RTS)
- Power: 3.3V (nRF52840 is 3.3V logic — level shifting needed for 5V MCUs like Mega)

### ESP32 NimBLE (Potential Future Work)

ESP32-S3 and ESP32-C3 could implement the same GATT profile using the NimBLE library, replacing the current `ESP32_BleSerial` transport. This would provide native BLE without an external coprocessor. Not implemented in this version — ESP32 BLE support remains as the existing transparent serial pipe via `BleSerial`.

### External AT-Command Modules

Modules like Adafruit Bluefruit (nRF51-based) support custom GATT via AT commands (`AT+GATTADDSERVICE`, `AT+GATTADDCHAR`). These could potentially implement the preOBD GATT profile through AT command configuration. However, performance and reliability may be limited compared to native nRF52840 implementations.

### HM-10 and Similar UART Bridges

The HM-10 and similar transparent BLE UART modules cannot support custom GATT services. They work with preOBD as simple serial pipes via the existing `SerialTransport`. The webapp detects these devices via their proprietary `0xFFE0` service UUID and uses the single `0xFFE1` characteristic for both TX and RX — referred to as HM-10 terminal mode. This is distinct from NUS: HM-10 modules expose `0xFFE0/0xFFE1`; NUS devices expose `6E400001...`.

## Reference

### File Locations

| File | Description |
|------|-------------|
| `src/lib/ble_gatt_defs.h` | UUID constants, binary struct definitions, protocol version |
| `webapp/index.html` | Web Bluetooth reference webapp |
| `docs/architecture/BLE_GATT_PROFILE.md` | This specification |
| `docs/reference/SERIAL_COMMANDS.md` | Complete text command reference |
| `docs/architecture/TRANSPORT_ARCHITECTURE.md` | Transport abstraction layer architecture |

### UUID Quick Reference

| Name | UUID |
|------|------|
| preOBD Control Service | `4f424400-7072-654f-4244-000000000001` |
| Command TX | `4f424400-7072-654f-4244-000000000010` |
| Command RX | `4f424400-7072-654f-4244-000000000011` |
| System Status | `4f424400-7072-654f-4244-000000000020` |
| preOBD Extended Service | `4f424400-7072-654f-4244-000000000002` |
| Alarm Notify | `4f424400-7072-654f-4244-000000000030` |
| Relay Control | `4f424400-7072-654f-4244-000000000031` |
| OTA Control | `4f424400-7072-654f-4244-000000000040` |
| OTA Data | `4f424400-7072-654f-4244-000000000041` |
| NUS Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| NUS RX | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| NUS TX | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
