# RealDash Setup Guide for openEMS

## What is RealDash?

RealDash is a customizable digital dashboard application for iOS and Android that connects to your vehicle's sensors and displays real-time data. With openEMS and RealDash, you can create professional-looking gauges, graphs, and warning displays on your tablet or phone.

**Key Features:**
- Fully customizable dashboard layouts
- Pre-built gauge designs and themes
- Data logging and playback
- Warning lights and alarms
- Supports multiple data sources simultaneously

## Hardware Requirements

To connect openEMS to RealDash, you need one of the following:

### Option 1: Bluetooth Serial Adapter (Recommended)
- **Hardware**: HC-05 or HM-10 Bluetooth module
- **Connection**: Connect to openEMS serial TX/RX pins
- **Pros**: Wireless, easy to use, works while driving
- **Cons**: Requires additional hardware (~$5-10)

### Option 2: USB Serial Connection
- **Hardware**: USB-OTG cable + USB-to-serial adapter
- **Connection**: Connect Teensy USB port directly to phone/tablet
- **Pros**: No additional hardware needed, reliable connection
- **Cons**: Wired connection, less practical for installed systems

## Software Requirements

1. **RealDash App**
   - Download from [App Store (iOS)](https://apps.apple.com/app/realdash/id736190810) or [Google Play (Android)](https://play.google.com/store/apps/details?id=fi.realdash)
   - Free version supports 1 dashboard with basic features
   - Premium version unlocks unlimited dashboards and advanced features

2. **openEMS Firmware**
   - RealDash output must be enabled in `config.h`
   - Verify `#define ENABLE_REALDASH` is uncommented
   - Upload firmware to Teensy

## Installation Steps

### Step 1: Enable RealDash Output in openEMS

Edit `/src/config.h`:

```cpp
// ===== OUTPUT MODULES =====
#define ENABLE_REALDASH      // Enable RealDash CAN output
```

Compile and upload the firmware to your Teensy.

### Step 2: Install openEMS XML Channel Description

The XML file tells RealDash how to decode openEMS sensor data.

**On Computer:**
1. Locate the XML file at: `/docs/realdash/openEMS.xml`
2. Transfer it to your mobile device using:
   - Email attachment
   - Cloud storage (Google Drive, Dropbox, iCloud)
   - USB file transfer
   - AirDrop (iOS) or Nearby Share (Android)

**On Mobile Device:**
1. Save the `openEMS.xml` file to a known location (Downloads folder works fine)

### Step 3: Configure RealDash Connection

**In RealDash App:**

1. Open RealDash
2. Tap the **hamburger menu** (three lines) → **Settings**
3. Tap **Connections**
4. Tap **Add Connection** (+ button)

**For Bluetooth:**
5. Select **Bluetooth** as connection type
6. Select your Bluetooth module from the list
   - HM-10 usually shows as "HM-10"
   - You may need to pair it first in your device's Bluetooth settings
7. Set **Baud Rate**: `115200`
8. Select **Protocol**: `RealDash CAN`
9. Tap **Custom Channel Description File**
10. Navigate to and select `openEMS.xml`
11. Tap **Done**

**For USB Serial:**
5. Select **USB/Serial** as connection type
6. Your device will detect the USB serial port automatically
7. Set **Baud Rate**: `115200`
8. Select **Protocol**: `RealDash CAN`
9. Tap **Custom Channel Description File**
10. Navigate to and select `openEMS.xml`
11. Tap **Done**

### Step 4: Connect and Verify

1. Tap **Connect** on the connection you just created
2. You should see a **green "Connected"** indicator
3. Return to the dashboard view

**Check sensor data:**
- Tap hamburger menu → **Dashboard Garage**
- Create a new dashboard or edit an existing one
- Add gauges for your sensors (CHT, Oil Pressure, etc.)
- Values should appear and update in real-time

## Available Sensors

openEMS provides the following sensors to RealDash:

| Sensor Name | RealDash Input Name | Units |
|-------------|---------------------|-------|
| Cylinder Head Temp (CHT) | CHT: Cylinder Head Temp | °C / °F |
| Exhaust Gas Temp (EGT) | EGT1 | °C / °F |
| Coolant Temperature | Coolant Temperature | °C / °F |
| Engine Oil Temperature | Engine Oil Temperature | °C / °F |
| Transfer Case Temperature | Oil Temperature (Transmission) | °C / °F |
| Engine Oil Pressure | Engine Oil Pressure | bar / psi |
| Boost Pressure | Manifold Absolute Pressure | kPa / psi |
| Primary Battery | Battery | Volts |
| Auxiliary Battery | Auxiliary Battery | Volts |
| Ambient Temperature | Ambient Temperature | °C / °F |
| Barometric Pressure | Barometric Pressure | kPa / psi |
| Elevation | Elevation | meters / feet |
| Coolant Level | Coolant Level | % |

**Note**: RealDash automatically handles unit conversions. You can switch between metric/imperial in RealDash settings.

## Creating Your First Dashboard

1. Tap hamburger menu → **Dashboard Garage**
2. Tap **+ Add Dashboard**
3. Choose a template or start from **Blank Dashboard**
4. Tap **Edit** (pencil icon)

**Adding Gauges:**
5. Tap **+ Add Widget**
6. Select gauge type (round gauge, bar graph, numeric, etc.)
7. Tap the gauge to configure it
8. Select **Input** → Choose your sensor (e.g., "Engine Oil Pressure")
9. Adjust appearance, colors, warning thresholds
10. Tap **Done**

Repeat for all sensors you want to display.

## Troubleshooting

### Problem: "No data" or sensors show "---"

**Possible causes:**
1. **openEMS not sending data**
   - Verify `ENABLE_REALDASH` is defined in config.h
   - Check sensors are enabled and configured in openEMS
   - Use Arduino Serial Monitor to verify data is being sent (you should see binary data at 115200 baud)

2. **Connection issues**
   - Bluetooth: Ensure module is paired and connected
   - USB: Check USB-OTG cable and adapter work
   - Try disconnecting and reconnecting

3. **Wrong baud rate**
   - Must be 115200 in both openEMS and RealDash
   - Double-check connection settings

4. **XML not loaded**
   - Go to RealDash connection settings
   - Verify "Custom Channel Description File" shows `openEMS.xml`
   - Try re-importing the XML file

### Problem: Gauges show incorrect values

**Possible causes:**
1. **Unit conversion mismatch**
   - RealDash displays show wrong units (check RealDash settings)
   - Sensor calibration in openEMS is incorrect
   - Check gauge configuration → Input scaling

2. **Sensor not reading correctly**
   - Compare RealDash values to openEMS LCD display
   - If LCD is also wrong, problem is with sensor/calibration in openEMS
   - If LCD is correct but RealDash is wrong, check XML conversion formulas

### Problem: Connection keeps dropping

**Bluetooth-specific:**
- Bluetooth module may have power issues
- Check wiring and power supply to HC-05/HM-10
- Some phones have aggressive Bluetooth power saving - disable it
- Move phone closer to Bluetooth module

**USB-specific:**
- Check USB cable quality
- Some USB-OTG adapters are unreliable
- Phone may be entering power-saving mode - disable USB power saving

### Problem: RealDash says "Unknown protocol"

- You likely selected wrong protocol
- Go to connection settings
- Change protocol to **RealDash CAN** (not OBD-II, not Generic, not Megasquirt)

## Advanced Configuration

### Data Logging

RealDash can log all sensor data for later analysis:

1. Tap hamburger menu → **Settings** → **Data Logging**
2. Enable **Log to Device**
3. Set logging rate (1-10 Hz recommended)
4. Start/stop logging from main dashboard

Logs are saved as CSV files you can analyze on a computer.

### Warning Lights

Set up visual/audio warnings for critical conditions:

1. Edit dashboard
2. Add a **Warning Light** widget
3. Configure trigger condition:
   - Input: Oil Pressure
   - Trigger: Less than 1.5 bar
   - Color: Red
   - Flash: Yes
4. Save

### Custom Formulas

You can apply additional math to sensor values:

1. Edit a gauge
2. Tap **Input**
3. Tap **Formula** button
4. Enter custom formula using **V** as value
   - Example: `V*1.5+10` adds 10 after scaling by 1.5
5. Save

## Technical Reference

### Frame Format

openEMS sends RealDash-CAN Type 44 frames:

```
Bytes 0-3:   [0x44, 0x33, 0x22, 0x11]  Preamble
Bytes 4-7:   [0x80, 0x0C, 0x00, 0x00]  Frame ID 0x0C80 (little-endian)
Bytes 8-15:  [OBD-II data]             8-byte payload
```

### OBD-II Payload Structure

```
Byte 0:  Length (2 + data bytes)
Byte 1:  0x41 (Mode 01: Show current data)
Byte 2:  PID (sensor identifier)
Byte 3+: Data (big-endian encoding)
```

### Composite Frame IDs

The XML uses RealDash's composite frame ID feature to discriminate sensors by PID:

```xml
<frame id="0x0c80:200,2,1">
```

This means:
- Frame ID: `0x0C80`
- Discriminator value: `200` (PID 0xC8 for CHT)
- Discriminator offset: `2` (PID is at byte 2)
- Discriminator length: `1` (PID is 1 byte)

RealDash checks byte 2 of each frame and applies the matching definition.

## Support

For issues with:
- **openEMS firmware**: See main openEMS documentation
- **RealDash app**: Visit [RealDash Forum](https://forum.realdash.net/)
- **This integration**: Open an issue on the openEMS GitHub repository

## Additional Resources

- [RealDash Official Manual](https://realdash.net/manuals/)
- [RealDash CAN Protocol Specification](https://github.com/janimm/RealDash-extras/tree/master/RealDash-CAN)
- [RealDash Community Dashboards](https://realdash.net/community.php)
- [RealDash TargetID Reference](https://realdash.net/manuals/targetid.php)
