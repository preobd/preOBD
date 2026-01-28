# OBD-II Scanner Setup Guide for openEMS

## Overview

openEMS now supports standard OBD-II request/response protocol (SAE J1979), allowing you to use inexpensive Bluetooth OBD-II adapters ($15-25) and mobile apps like **Torque** to view your sensor data in real-time.

###What This Means

- **Use your phone as a display** - No need for a dedicated LCD or tablet
- **Works with existing tools** - Compatible with popular OBD-II scanner apps
- **Hybrid mode** - Simultaneously supports RealDash broadcast and scanner request/response
- **Standard protocol** - Uses the same OBD-II PIDs as modern vehicles

### How It Differs from RealDash

| Feature | OBD-II Scanners (Torque) | RealDash |
|---------|--------------------------|----------|
| **Protocol** | Request/Response (on-demand) | Broadcast (periodic) |
| **Hardware** | Standard ELM327 adapter | Bluetooth module or USB |
| **Apps** | Torque, OBD Fusion, Car Scanner | RealDash only |
| **Customization** | Limited gauges | Fully customizable dashboards |
| **Data logging** | Yes (CSV export) | Yes (playback) |
| **Cost** | ELM327 adapter: $15-25 | Same |
| **Setup complexity** | Plug-and-play | Requires XML configuration |

**Best of both worlds**: You can use **both simultaneously**! openEMS runs in hybrid mode, broadcasting for RealDash while responding to scanner queries.

---

## Hardware Requirements

### 1. CAN Transceiver

openEMS CAN pins operate at logic level (3.3V or 5V) and need a transceiver to communicate on the CAN bus.

**⚠️ Critical requirement**: You must use a CAN transceiver that matches your platform's voltage:
- **3.3V platforms** (Teensy, ESP32, Due): Use **SN65HVD230** or MCP2562
- **5V platforms** (Arduino Mega, Uno): Use **MCP2551** or TJA1050

**For complete transceiver selection, wiring diagrams, and troubleshooting, see:**
[CAN Transceiver Hardware Guide](../hardware/CAN_TRANSCEIVER_GUIDE.md)

### 2. ELM327 Bluetooth Adapter

**What is ELM327?**
- Industry-standard OBD-II to Bluetooth/WiFi adapter chip
- Translates between vehicle CAN bus and your phone
- Available from many manufacturers ($15-25)

**Recommended adapters:**
- **Veepeak** - Bluetooth, reliable, good reviews
- **BAFX Products** - Bluetooth, popular on Amazon
- **Vgate iCar Pro** - Bluetooth 4.0 (BLE), fast
- **OBDLink MX+** - Professional grade, expensive but excellent

**⚠️ Avoid ultra-cheap clones** - Many $5-10 adapters use counterfeit ELM327 chips with bugs. Stick with known brands.

### 3. OBD-II Port Connection

You'll need to wire openEMS CAN bus to an OBD-II connector.

**OBD-II pinout (focusing on CAN):**
```
   1  2  3  4  5  6  7  8
   9 10 11 12 13 14 15 16

Pin 4:  Chassis ground
Pin 5:  Signal ground
Pin 6:  CAN High (CAN-H)
Pin 14: CAN Low (CAN-L)
Pin 16: +12V battery power
```

**Wiring:**
```
MCP2551 CANH  → OBD-II Pin 6 (CAN-H)
MCP2551 CANL  → OBD-II Pin 14 (CAN-L)
Ground         → OBD-II Pin 5 (Signal GND)
```

---

## Platform-Specific Wiring

### Teensy 4.x with Native FlexCAN

```
Teensy 4.x      MCP2551      OBD-II Port
----------      --------     -----------
Pin 22 (TX) →   TXD
Pin 23 (RX) ←   RXD
3.3V        →   VDD
GND         →   VSS
                CANH     →   Pin 6 (CAN-H)
                CANL     →   Pin 14 (CAN-L)
                         →   Pin 5 (GND)
```

**Note**: Teensy 4.x has native CAN support via FlexCAN_T4 library. No external MCP2515 needed.

### ESP32 with Native TWAI

```
ESP32           MCP2551      OBD-II Port
----------      --------     -----------
GPIO21 (TX) →   TXD          (ESP32 original)
GPIO22 (RX) ←   RXD          (ESP32 original)
  -or-
GPIO20 (TX) →   TXD          (ESP32-S3/C3)
GPIO21 (RX) ←   RXD          (ESP32-S3/C3)
3.3V        →   VDD
GND         →   VSS
                CANH     →   Pin 6 (CAN-H)
                CANL     →   Pin 14 (CAN-L)
                         →   Pin 5 (GND)
```

**Note**: ESP32 has native TWAI (CAN) support. Use 3.3V-tolerant transceiver like SN65HVD230.

### Arduino with MCP2515 (SPI)

```
Arduino         MCP2515      OBD-II Port
----------      --------     -----------
Pin 13 (SCK)→   SCK
Pin 12 (MISO)←  SO
Pin 11 (MOSI)→  SI
Pin 9 (CS)  →   CS
Pin 2 (INT) ←   INT
5V          →   VCC
GND         →   GND
                CANH     →   Pin 6 (CAN-H)
                CANL     →   Pin 14 (CAN-L)
                         →   Pin 5 (GND)
```

**Note**: External MCP2515 CAN controller module required (~$3-5).

---

## Software Setup

### Step 1: Enable CAN Output in openEMS

CAN output must be enabled in your build configuration:

**In `platformio.ini`:**
```ini
build_flags =
    -D ENABLE_CAN              ; Enable CAN bus output
```

Verify CAN is enabled:
```
pio run -e teensy41
```

### Step 2: Configure Sensors

Configure your sensors normally using serial commands:

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
SET A3 OIL_PRESSURE VDO_5BAR_CURVE
SET 7 EGT MAX31855
SAVE
```

Each sensor with an OBD-II PID assignment will automatically be available via the scanner.

### Step 3: Upload Firmware

```bash
pio run -e teensy41 -t upload
```

On boot, you should see:
```
✓ Native FlexCAN initialized (500kbps)
✓ OBD-II RX filters configured (0x7DF, 0x7E0)
✓ Built OBD-II PID lookup table: 8 PIDs available
```

---

## Mobile App Setup

### Option 1: Torque (Android) - Recommended

**Download**: [Google Play Store](https://play.google.com/store/apps/details?id=org.prowl.torque)

**Setup Steps:**

1. **Pair ELM327 adapter**
   - Plug ELM327 into OBD-II port
   - Phone Settings → Bluetooth → Scan → Pair with "OBDII" or "ELM327"
   - Default PIN: `1234` or `0000`

2. **Configure Torque**
   - Open Torque app
   - Settings → OBD2 Adapter Settings
   - Connection: Bluetooth
   - Choose Adapter: Select your ELM327 device
   - Connection Type: Automatic

3. **Connect**
   - Tap Connect button (plug icon)
   - Should show "Connected" and display PIDs

4. **View Data**
   - Swipe to Realtime Information screen
   - Tap + to add sensors
   - Select available PIDs (Engine Temp, Oil Pressure, etc.)
   - View live data!

**Tips:**
- Use "Dashboard" view for multiple gauges
- Enable data logging: Settings → Data Logging & Upload
- Create custom layouts: Settings → Dashboard Layouts

### Option 2: OBD Fusion (iOS/Android)

**Download**: [App Store](https://apps.apple.com/app/obd-fusion/id463022082) | [Google Play](https://play.google.com/store/apps/details?id=com.octo.obidon)

**Setup:**
1. Pair ELM327 adapter via Bluetooth
2. Open OBD Fusion
3. Settings → OBD-II Adapter → Choose adapter
4. Tap Scan to connect
5. View dashboards and gauges

### Option 3: Car Scanner (Android)

**Download**: [Google Play Store](https://play.google.com/store/apps/details?id=com.ovz.carscanner)

**Setup:**
1. Pair ELM327 adapter
2. Open Car Scanner
3. Settings → Adapter → Bluetooth
4. Select adapter → Connect
5. Dashboard tab shows live data

---

## Available OBD-II PIDs

openEMS automatically maps your configured sensors to standard OBD-II PIDs based on the application type.

### Common PIDs

| PID | Name | Application | Range | Units |
|-----|------|-------------|-------|-------|
| `0x00` | Supported PIDs 01-20 | *(auto-generated)* | Bitmap | - |
| `0x05` | Engine Coolant Temp | COOLANT_TEMP | -40 to 215°C | °C |
| `0x0C` | Engine RPM | ENGINE_RPM | 0 to 16383 rpm | rpm |
| `0x0D` | Vehicle Speed | VEHICLE_SPEED | 0 to 255 km/h | km/h |
| `0x0F` | Intake Air Temp | AMBIENT_TEMP | -40 to 215°C | °C |
| `0x11` | Throttle Position | *(custom)* | 0 to 100% | % |
| `0x23` | Fuel Rail Pressure | FUEL_PRESSURE | 0 to 5177 kPa | kPa |
| `0x33` | Barometric Pressure | BAROMETRIC_PRESSURE | 0 to 255 kPa | kPa |
| `0x42` | Control Module Voltage | PRIMARY_BATTERY | 0 to 65.535 V | V |
| `0x5C` | Engine Oil Temperature | OIL_TEMP | -40 to 210°C | °C |

**To see YOUR active PIDs:**
```
LIST SENSORS     # Shows all configured sensors with their PIDs
```

**Custom PIDs:**
- PIDs 0x21-0xFF are manufacturer-specific
- You can assign custom PIDs in `application_presets.h`
- openEMS uses standard PIDs where possible

For a complete PID reference, see [OBD2_PID_REFERENCE.md](../../reference/OBD2_PID_REFERENCE.md).

---

## Troubleshooting

### "No connection" or "Unable to connect"

**Possible causes:**
1. **ELM327 not paired** - Check Bluetooth pairing
2. **Wrong adapter selected** - Verify adapter name in app settings
3. **CAN bus not initialized** - Check serial output for CAN init messages
4. **Wiring issue** - Verify CAN-H/CAN-L connections
5. **Baud rate mismatch** - openEMS uses 500 kbps (standard)

**Debugging:**
```
# Check serial monitor on boot:
✓ Native FlexCAN initialized (500kbps)
✓ OBD-II RX filters configured (0x7DF, 0x7E0)
✓ Built OBD-II PID lookup table: X PIDs available
```

### "No PIDs found" or "Supported PIDs shows 0000"

**Possible causes:**
1. **No sensors configured** - Run `LIST SENSORS` to verify
2. **Sensors not enabled** - Check sensor flags: `DUMP`
3. **Invalid PID assignments** - PIDs are 0x00 (none configured)

**Fix:**
- Configure sensors with valid applications
- Ensure sensors are enabled: `SET A2 ENABLE`
- Verify OBD-II PIDs are assigned in application_presets.h

### "Incorrect readings" or "Values don't match LCD"

**Possible causes:**
1. **Unit mismatch** - Torque may show different units (°F vs °C, PSI vs bar)
2. **Scaling issue** - Check OBD conversion functions in sensor_read.cpp
3. **PID conflict** - Multiple sensors assigned same PID

**Fix:**
- Change units in Torque: Settings → Data Display → Units
- Verify sensor PID assignments: `LIST SENSORS`
- Check for duplicate PID warnings on boot

### "Slow response" or "Delayed updates"

**Normal behavior**:
- Scanner apps query PIDs sequentially (~1-10 Hz per PID)
- More PIDs = slower update rate per gauge
- This is a limitation of OBD-II protocol, not openEMS

**Optimization:**
- Limit number of displayed gauges in app
- Use RealDash for faster updates (broadcast mode, not request/response)

### "Some PIDs work, others don't"

**Possible causes:**
1. **Sensor value is NaN** - Invalid/unconnected sensor
2. **PID outside 0x01-0x20 range** - Scanner may not query extended PIDs
3. **App-specific PID support** - Some apps only query standard PIDs

**Fix:**
- Verify sensor readings: `DUMP` or serial CSV output
- Use PID 0x00 to see which PIDs scanner detects
- Try different scanner app (Torque is most comprehensive)

---

## Advanced Topics

### Custom PID Assignments

You can modify PID assignments in `src/lib/application_presets.h`:

```cpp
static const ApplicationPreset APP_OIL_TEMP = {
    .name = PSTR_OIL_TEMP,
    // ... other fields ...
    .obd2pid = 0x5C,        // Engine Oil Temperature (standard)
    .obd2length = 1,        // 1-byte response
    // ...
};
```

Rebuild and upload after changes.

### Hybrid Mode with RealDash

**Works simultaneously!** No configuration needed.

- **Broadcast mode** (for RealDash): Periodic transmission of all sensors
- **Request/response mode** (for scanners): On-demand PID queries

Both modes use CAN ID 0x7E8 for responses, so they coexist peacefully.

**Typical setup:**
- RealDash on tablet (permanent dash)
- Torque on phone (portable diagnostics)

### Multi-ECU Setups

openEMS responds as **ECU 0** (physical address 0x7E0, response 0x7E8).

If you have multiple ECUs on the bus:
- openEMS: ECU 0 (0x7E0 → 0x7E8)
- Factory ECU: ECU 1 (0x7E1 → 0x7E9)
- Transmission: ECU 2 (0x7E2 → 0x7EA)

Scanner apps can query specific ECUs. Select "ECU 0" for openEMS data.

### Testing Without Hardware

**Loopback mode** (Teensy FlexCAN only):

In `initCAN()`, uncomment:
```cpp
Can0.enableLoopBack();  // TX messages loop back to RX
```

Sends a test request via serial:
```cpp
// Add to test mode or serial command handler
byte testRequest[8] = {0x02, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00};
processOBD2Request(0x7DF, testRequest, 8);
```

---

## Comparison: OBD-II Scanners vs RealDash

| Use Case | Recommended Tool |
|----------|------------------|
| Quick diagnostics while driving | **Torque** (request/response) |
| Permanent in-car display | **RealDash** (broadcast) |
| Data logging for analysis | **Torque** or **RealDash** |
| Custom gauge layouts | **RealDash** |
| Checking codes/warnings | **Torque** (limited - openEMS doesn't store DTCs yet) |
| Multi-sensor overview | **RealDash** dashboards |
| Single-gauge focus | **Torque** |

**Bottom line**: Use both! RealDash for dashboard, Torque for quick checks.

---

## See Also

- [CAN Transceiver Hardware Guide](../hardware/CAN_TRANSCEIVER_GUIDE.md) - Transceiver selection and wiring
- [RealDash Setup Guide](REALDASH_SETUP_GUIDE.md) - Broadcast mode configuration
- [OBD-II PID Reference](../../reference/OBD2_PID_REFERENCE.md) - Complete PID table
- [Serial Commands](../../reference/SERIAL_COMMANDS.md) - Sensor configuration
- [CAN Bus Output](../../README.md#can-bus) - General CAN configuration

