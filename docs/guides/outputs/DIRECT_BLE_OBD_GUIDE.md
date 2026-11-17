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

The HM-10 is a common, inexpensive BLE UART module. Out of the box it comes up
at 9600 baud, which is what preOBD uses for all HM-10 traffic — the HM-10's
BLE 4.0 radio only sustains ~1–2 KB/s, so a higher UART baud just lets bytes
pile up faster than the radio can drain them, causing silent overflow drops
(see [BLUETOOTH_HARDWARE_GUIDE.md — Recommended Settings](../hardware/BLUETOOTH_HARDWARE_GUIDE.md#recommended-settings-for-preobd)).

One-time setup via AT commands (connect TX/RX/VCC/GND, open a terminal at the
HM-10's default 9600 baud before wiring to preOBD):

```
AT+NAMEpreOBD    → rename BLE device (phone will see "preOBD")
AT+RESET         → apply and reboot
```

> The HM-10's default baud (9600, `AT+BAUD0`) is correct as-shipped — don't
> change it. If a previous user set it higher, restore with `AT+BAUD0`.

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
BUS SERIAL 2 ENABLE 9600       # bring up Serial2 at 9600 baud (HM-10 default)
OUTPUT ADD ELM327 SERIAL2      # the ELM327 emulator on Serial2 (exclusive), at once; prints its id
SAVE
```

Verify it: `BUS SERIAL 2` shows `ENABLED @ 9600 baud`, and `OUTPUT LIST`
shows `<id>: ELM327 on SERIAL2 - enabled`. The emulator answers only the inputs
routed to its output (a new input is routed to every output; `INFO <pin> ROUTE`
shows it).

**With the nRF52840 coprocessor** (on a framed port) no HM-10 is needed: an
ELM327 output on `BLE` is on by default, and phone OBD apps find the device as
"preOBD-xxxx". BLE carries one data output, so if a RealDash output holds `BLE`
(see the [RealDash guide](REALDASH_SETUP_GUIDE.md)), disable it and enable the
ELM327 one: `OUTPUT LIST` shows both ids.

To remove it later (the port stays enabled):

```
OUTPUT DEL <id>         # its id from OUTPUT LIST
SAVE
```

### Port exclusivity

A port assigned to ELM327 cannot also carry the console or the log. If you need
both the preOBD webapp (the console over a BLE UART) and ELM327 on HM-10-style
modules, use two separate serial ports — typically one BLE module (e.g. HM-10
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

### Spaces

Like a real ELM327, preOBD starts with spaces on (`ATS1`): RPM reads
`41 0C 1A F8`. Apps that prefer compact replies send `ATS0` (`410C1AF8`), and
`ATZ` restores the default. A reply longer than one BLE packet (20 bytes at the
default MTU) is split across several notifications and reassembled by the app,
so spaces cost a little airtime and never truncate a reply.

### Headers

Headers are off by default (`ATH0`). When on, preOBD uses the fixed header
`7E8` (standard ECU response ID) and shows the whole 8-byte CAN frame, padding
included, e.g. `7E8 04 41 0C 1A F8 00 00 00`. That, and the space after the last
byte when spaces are on, is how a genuine STN/ELM327 adapter prints it.

### Supply voltage (ATRV)

If a `PRIMARY_BATTERY` input is configured, `ATRV` returns the live measured
voltage. Otherwise it returns `12.0V`.

---

## Simultaneous USB debug output

The ELM327 port owns Serial2 exclusively. USB Serial (`Serial`) remains fully
available for debug commands and log output while the BLE session is active.
