# Direct BLE OBD-II — No ELM327 Hardware Required

preOBD can emulate an ELM327 adapter directly in firmware, so OBD-II phone
apps connect to preOBD over Bluetooth without any external adapter hardware.

## What this replaces

| Before | After |
|--------|-------|
| preOBD CAN → ELM327 hardware → BLE → Phone | preOBD serial → BLE module → Phone |

The ELM327 chip's role is absorbed by firmware. preOBD speaks the ELM327 ASCII
AT command protocol over a UART directly connected to a BLE module.

---

## Supported apps

| App | Platform | BLE support |
|-----|----------|-------------|
| **OBD Fusion** | iOS / Android | ✓ Full BLE GATT |
| **Car Scanner** | iOS / Android | ✓ Full BLE GATT |
| **Torque Pro** | Android | ✓ BLE (requires Torque Pro, not free) |

> **Note:** Torque Lite (free) only supports Bluetooth Classic SPP, not BLE.
> Use Torque Pro or switch to OBD Fusion / Car Scanner.

---

## Hardware: HM-10 BLE module

The HM-10 is a common, inexpensive BLE UART module. One-time setup via AT
commands (connect TX/RX/VCC/GND, open a terminal at the HM-10's default 9600
baud before wiring to preOBD):

```
AT+BAUD4         → set baud to 115200 (matches preOBD default)
AT+NAMEpreOBD    → rename BLE device (phone will see "preOBD")
AT+RESET         → apply and reboot
```

> **If this HM-10 is also used by the preOBD webapp:** use 9600 (`AT+BAUD0`) or
> 19200 (`AT+BAUD1`) instead, and adjust the `BUS SERIAL` baud below to match.
> 115200 is safe for ELM327 scan-tool traffic (short PID request/response pairs)
> but will cause the webapp's `SYSTEM DUMP JSON` to hang, because the HM-10's
> BLE radio drains slower than the UART feeds and overflow bytes are silently
> dropped. See [BLUETOOTH_HARDWARE_GUIDE.md — Recommended Settings](../hardware/BLUETOOTH_HARDWARE_GUIDE.md#recommended-settings-for-preobd)
> for the full explanation. The two-module setup described under
> [Port exclusivity](#port-exclusivity) below avoids this entirely — the
> ELM327 HM-10 can stay at 115200 while the webapp HM-10 runs at 9600/19200.

After setup, wire the HM-10 to a preOBD serial port (e.g. Serial2):

| HM-10 | preOBD |
|-------|--------|
| TX    | Serial2 RX |
| RX    | Serial2 TX |
| VCC   | 3.3V |
| GND   | GND |

> HM-10 is a 3.3V device. On 5V platforms (Mega) use a voltage divider on the
> RX line (HM-10 RX ← 5V signal) or a logic level shifter.

---

## preOBD configuration

Connect to preOBD via USB Serial and run:

```
BUS SERIAL 2 ENABLE 115200     # bring up Serial2 at 115200 baud
BUS SERIAL 2 ELM327 ENABLE     # assign ELM327 role to Serial2 (exclusive)
SAVE
SYSTEM REBOOT
```

Verify the assignment:

```
BUS SERIAL 2
```

Expected output includes `ENABLED @ 115200 baud [ELM327]`.

To disable later:

```
BUS SERIAL 2 ELM327 DISABLE
SAVE
```

### Port exclusivity

A port assigned to ELM327 cannot simultaneously carry preOBD CONTROL/DATA/DEBUG
traffic. If you need both the preOBD webapp (CONTROL/DATA traffic over BLE UART)
and ELM327, use two separate serial ports — typically one BLE module (e.g. HM-10
on Serial1) for the webapp and a second module (e.g. HM-10 or HC-05 on Serial2)
dedicated to ELM327 for the scanner app.

---

## App configuration

### OBD Fusion
1. Settings → Connections → Add Connection
2. Connection type: **ELM327 BLE**
3. Select "preOBD" from the device list
4. Tap Connect

### Car Scanner
1. Settings → OBD Adapter → Bluetooth LE
2. Select "preOBD"
3. Protocol: **Auto** (or ISO 15765-4 CAN 11-bit 500kbps)

### Torque Pro
1. Settings → OBD2 Adapter Settings
2. Connection type: **Bluetooth LE (BLE)**
3. Select "preOBD"

---

## Response format notes

### MTU and spaces

The HM-10 BLE MTU is 20 bytes. preOBD defaults to spaces-off (`ATS0`) to keep
responses compact. A typical RPM response with spaces off is `410C1AF8` (8
chars); with spaces on it would be `41 0C 1A F8` (11 chars + overhead).

Most apps work fine with spaces off. If an app forces `ATS1` and responses are
truncated on multi-byte PIDs, switch to OBD Fusion which handles both formats.

### Headers

Headers are off by default (`ATH0`). When on, preOBD uses the fixed header
`7E8` (standard ECU response ID), e.g. `7E8 04 41 0C 1A F8`.

### Supply voltage (ATRV)

If a `PRIMARY_BATTERY` input is configured, `ATRV` returns the live measured
voltage. Otherwise it returns `12.0V`.

---

## Simultaneous USB debug output

The ELM327 port owns Serial2 exclusively. USB Serial (`Serial`) remains fully
available for debug commands and log output while the BLE session is active.
