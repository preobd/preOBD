# OpenEMS Comprehensive Testing Plan

**Version:** 0.3.0-alpha (Post-Architecture Unification)
**Date:** 2025-01-21
**Purpose:** Verify all 70+ capabilities work correctly after unified Input-based architecture migration

---

## Test Environment Requirements

### Hardware Requirements

**Minimum Test Setup:**
- Arduino Mega 2560 or Teensy 4.0/4.1
- 20x4 I2C LCD display (address 0x27)
- USB serial connection (115200 baud)
- 5V/3.3V power supply

**Extended Test Setup (for complete coverage):**
- MAX6675 K-type thermocouple breakout
- MAX31855 K-type thermocouple breakout
- VDO temperature sender (120C or 150C)
- VDO pressure sender (2-bar or 5-bar)
- 12V battery or voltage source
- BME280 environmental sensor (I2C)
- CAN transceiver (MCP2515 or FlexCAN on Teensy)
- SD card module
- Piezo buzzer
- Momentary push button (MODE_BUTTON - alarm silence & config mode entry)
- Digital float switch or jumper wire
- Potentiometers (for simulating analog sensors)

### Software Requirements

- PlatformIO CLI
- Serial terminal (screen, minicom, or Arduino Serial Monitor)
- CAN bus analyzer/logger (optional)
- RealDash app (optional, for protocol testing)

---

## Testing Priority Levels

- 🔴 **CRITICAL**: Core functionality, must pass
- 🟡 **HIGH**: Important features, should pass
- 🟢 **MEDIUM**: Secondary features, nice to have
- ⚪ **LOW**: Edge cases, documentation validation

---

## 1. CORE INPUT CONFIGURATION TESTS

### 1.1 Input Definition (EEPROM Mode)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| IC-E-01 | Create new input via serial | 🔴 | EEPROM | 1. Send "SET A0 APPLICATION OIL_TEMP VDO_150C_STEINHART"<br>2. Send "LIST INPUTS" | New input appears with correct settings |
| IC-E-02 | List available applications | 🟡 | EEPROM | Send "LIST APPLICATIONS" | Shows 16 presets (CHT, EGT, etc.) |
| IC-E-03 | List available sensors | 🟡 | EEPROM | Send "LIST SENSORS" | Shows all 17 sensor types |
| IC-E-04 | Enable/disable input | 🔴 | EEPROM | 1. "DISABLE A0"<br>2. "LIST INPUTS"<br>3. "ENABLE A0" | Input disabled, then re-enabled |
| IC-E-05 | Change sensor type | 🟡 | EEPROM | 1. "SET A0 SENSOR VDO_120C_STEINHART"<br>2. "INFO A0" | Sensor type updated |
| IC-E-06 | Set custom name | 🟢 | EEPROM | 1. "SET A0 NAME MYOIL"<br>2. "INFO A0" | Name changed to "MYOIL" |
| IC-E-07 | Set display name | 🟢 | EEPROM | 1. "SET A0 DISPLAY_NAME CustomOil"<br>2. "INFO A0" | Display name updated |
| IC-E-08 | Override display units | 🟡 | EEPROM | 1. "SET A0 UNITS FAHRENHEIT"<br>2. Check LCD | Value displayed in °F |
| IC-E-09 | Clear input | 🟡 | EEPROM | 1. "CLEAR A0"<br>2. "LIST INPUTS" | Input removed from list |
| IC-E-10 | Maximum inputs | 🟢 | EEPROM | Configure MAX_INPUTS sensors | All inputs work, no overflow |

### 1.2 Input Definition (Compile-Time Mode)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| IC-C-01 | Define input in sensors_config.h | 🔴 | Compile | 1. Uncomment INPUT_4 definitions<br>2. Build<br>3. Upload | New sensor reads correctly |
| IC-C-02 | Comment out input | 🔴 | Compile | 1. Comment INPUT_0<br>2. Build<br>3. Check LCD | Sensor 0 not displayed |
| IC-C-03 | Auto-count active inputs | 🔴 | Compile | 1. Define 3 inputs<br>2. Check serial output | "Active inputs: 3" on startup |
| IC-C-04 | Invalid sensor type | 🟡 | Compile | Define INPUT_5_SENSOR as invalid enum | Compilation fails with clear error |
| IC-C-05 | Invalid application type | 🟡 | Compile | Define INPUT_5_APPLICATION as invalid | Compilation fails with clear error |
| IC-C-06 | Mixed pin types | 🟡 | Compile | Use A0, A5, 6, 10 pins | All pins work correctly |
| IC-C-07 | Enable/disable at runtime | 🟢 | Compile | Modify `inputs[0].isEnabled` in code | Can toggle sensors on/off |

---

## 2. CALIBRATION SYSTEM TESTS

### 2.1 Preset Calibrations

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| CAL-P-01 | VDO 120C lookup table | 🔴 | Both | 1. Configure VDO_120C_LOOKUP<br>2. Apply known resistance<br>3. Read temperature | Temperature within ±2°C |
| CAL-P-02 | VDO 150C lookup table | 🔴 | Both | Same as CAL-P-01 for 150C | Temperature within ±2°C |
| CAL-P-03 | VDO 120C Steinhart-Hart | 🔴 | Both | Compare to lookup table | Values match within 1°C |
| CAL-P-04 | VDO 150C Steinhart-Hart | 🔴 | Both | Compare to lookup table | Values match within 1°C |
| CAL-P-05 | VDO 5-bar polynomial | 🟡 | Both | Apply known resistances | Pressure accurate to ±0.1 bar |
| CAL-P-06 | VDO 2-bar polynomial | 🟡 | Both | Apply known resistances | Pressure accurate to ±0.1 bar |
| CAL-P-07 | Generic boost linear | 🟡 | Both | Apply 0.5V, 2.5V, 4.5V | 0 bar, 2.5 bar, 5 bar |
| CAL-P-08 | MPX4250AP linear | 🟡 | Both | Apply reference voltages | Pressure calculation correct |
| CAL-P-09 | Voltage divider formula | 🔴 | Both | Connect 12V battery | Reads 11.5-12.8V range |
| CAL-P-10 | Preset loading from flash | 🟡 | Both | Configure any preset sensor | Calibration data loaded correctly |

### 2.2 Custom Calibrations (EEPROM Mode)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| CAL-C-01 | Steinhart override (serial) | 🟡 | EEPROM | **NOT YET IMPLEMENTED** - Need to add serial commands | N/A |
| CAL-C-02 | Pressure linear override | 🟡 | EEPROM | **NOT YET IMPLEMENTED** | N/A |
| CAL-C-03 | Pressure poly override | 🟡 | EEPROM | **NOT YET IMPLEMENTED** | N/A |
| CAL-C-04 | Lookup bias override | 🟢 | EEPROM | **NOT YET IMPLEMENTED** | N/A |
| CAL-C-05 | Custom cal persistence | 🟡 | EEPROM | **Blocked by CAL-C-01** | N/A |
| CAL-C-06 | Clear custom calibration | 🟢 | EEPROM | **NOT YET IMPLEMENTED** | N/A |

### 2.3 Custom Calibrations (Compile-Time Mode)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| CAL-S-01 | advanced_config.h integration | ⚪ | Compile | **NOT IMPLEMENTED** - File orphaned | N/A |
| CAL-S-02 | DEFINE_CUSTOM_THERMISTOR macro | ⚪ | Compile | **NOT IMPLEMENTED** - No include path | N/A |
| CAL-S-03 | Custom cal in sensors_config.h | ⚪ | Compile | **NOT IMPLEMENTED** - No macro support | N/A |

---

## 3. DATA OUTPUT MODULE TESTS

### 3.1 CAN Bus Output

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| OUT-C-01 | FlexCAN init (Teensy) | 🟡 | Both | 1. Build for Teensy<br>2. Enable USE_FLEXCAN_NATIVE<br>3. Check serial | "Native FlexCAN initialized" |
| OUT-C-02 | MCP2515 init | 🟡 | Both | 1. Build for Mega<br>2. Connect MCP2515<br>3. Check serial | "MCP2515 CAN initialized" |
| OUT-C-03 | OBD2 frame format | 🔴 | Both | 1. Enable CAN<br>2. Connect CAN logger<br>3. Read frames | ID=0x7E8, length=8, valid OBD2 |
| OUT-C-04 | CAN baud rate | 🟡 | Both | Check with CAN analyzer | 500 kbps confirmed |
| OUT-C-05 | Big-endian encoding | 🔴 | Both | Send 16-bit value 0x1234 | Bytes: 0x12 0x34 (MSB first) |
| OUT-C-06 | Invalid data handling | 🟡 | Both | Disconnect sensor (NaN) | No CAN frame sent |
| OUT-C-07 | All sensor types on CAN | 🟡 | Both | Configure all 17 types | All transmit correctly |
| OUT-C-08 | CAN send rate | 🟢 | Both | Measure with CAN logger | ~200ms interval (LOOP_DELAY_MS) |

### 3.2 RealDash Protocol

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| OUT-R-01 | RealDash preamble | 🟡 | Both | Enable REALDASH, capture serial | Bytes: 0x44 0x33 0x22 0x11 |
| OUT-R-02 | RealDash frame ID | 🟡 | Both | Decode RealDash frame | Frame ID: 0x0C80 |
| OUT-R-03 | RealDash OBD2 data | 🔴 | Both | Decode full frame | Valid OBD2 embedded in RealDash |
| OUT-R-04 | RealDash app connection | 🟢 | Both | Connect RealDash app via BT | Data displays in app |

### 3.3 Serial CSV Output

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| OUT-S-01 | CSV format | 🟡 | Both | Enable SERIAL_OUTPUT, read serial | "Name,Value,Units" format |
| OUT-S-02 | CSV precision | 🟢 | Both | Check decimal places | 2 decimal places for all |
| OUT-S-03 | CSV units | 🟡 | Both | Check unit strings | Matches display units |
| OUT-S-04 | CSV error handling | 🟡 | Both | Disconnect sensor | "Name,ERROR" printed |
| OUT-S-05 | CSV header row | 🟢 | Both | Check startup output | Header printed once |

### 3.4 SD Card Logging

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| OUT-D-01 | SD card detection | 🟡 | Both | 1. Enable SD_LOGGING<br>2. Insert SD card<br>3. Check serial | "SD card initialized" |
| OUT-D-02 | File creation | 🟡 | Both | Check SD card | log_[timestamp].csv exists |
| OUT-D-03 | SD CSV format | 🟡 | Both | Open log file | "Time,Sensor,Value,Units" |
| OUT-D-04 | SD log interval | 🟢 | Both | Check timestamps | ~1 second between entries |
| OUT-D-05 | SD flush interval | 🟢 | Both | Remove card during logging | Data saved up to last flush (5s) |
| OUT-D-06 | SD card failure | 🟡 | Both | Remove card, reinsert | System continues without crashing |

---

## 4. DISPLAY SYSTEM TESTS

### 4.1 LCD Display (20x4 I2C)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| DIS-L-01 | LCD initialization | 🔴 | Both | 1. Connect LCD<br>2. Power on<br>3. Check display | Backlight on, no garbage chars |
| DIS-L-02 | 8-sensor layout | 🔴 | Both | Configure 8 sensors | All 8 displayed (4 left, 4 right) |
| DIS-L-03 | Temperature format | 🔴 | Both | Display CHT sensor | "CHT:125°" (no decimal, degree symbol) |
| DIS-L-04 | Voltage format | 🔴 | Both | Display battery sensor | "BAT:12.5V" (1 decimal) |
| DIS-L-05 | Pressure format (bar) | 🟡 | Both | Display oil pressure in bar | "OPS:3.5b" (1 decimal) |
| DIS-L-06 | Pressure format (PSI) | 🟡 | Both | Display boost in PSI | "BST:25.3p" (1 decimal) |
| DIS-L-07 | Humidity format | 🟡 | Both | Display BME280 humidity | "RH:65%" (no decimal) |
| DIS-L-08 | Altitude format | 🟡 | Both | Display elevation | "ELV:250ft" (no decimal) |
| DIS-L-09 | Error display | 🔴 | Both | Disconnect sensor | "CHT:ERR" shown |
| DIS-L-10 | Unit conversion | 🔴 | Both | Change units to Fahrenheit | Display updates to °F |
| DIS-L-11 | Per-sensor display hide | 🟡 | Both | Set `input->display = false` | Sensor hidden from LCD |
| DIS-L-12 | Degree symbol | 🟢 | Both | Check temperature display | Custom character (0x00) renders |
| DIS-L-13 | Display update rate | 🟢 | Both | Observe with changing input | Updates every ~200ms |
| DIS-L-14 | More than 8 sensors | 🟢 | Both | Configure 10 sensors | Only first 8 displayed |

### 4.2 OLED Display

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| DIS-O-01 | OLED initialization | ⚪ | Both | **NOT IMPLEMENTED** - Stub only | N/A |

---

## 5. ALARM SYSTEM TESTS

### 5.1 Alarm Configuration

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| ALM-C-01 | Global alarm enable | 🔴 | Both | 1. #define ENABLE_ALARMS<br>2. Build | Alarm system active |
| ALM-C-02 | Global alarm disable | 🟡 | Both | 1. Comment out ENABLE_ALARMS<br>2. Build | No alarms, buzzer silent |
| ALM-C-03 | Per-sensor alarm enable | 🔴 | Both | Set `input->alarm = true` | Alarm active for sensor |
| ALM-C-04 | Per-sensor alarm disable | 🟡 | Both | Set `input->alarm = false` | Alarm disabled for sensor |
| ALM-C-05 | Alarm from preset default | 🟡 | Both | Configure CHT (alarm enabled) | Alarm active by default |
| ALM-C-06 | Set alarm thresholds (EEPROM) | 🟡 | EEPROM | "SET A0 ALARM 10 100" | Min=10, Max=100 set |
| ALM-C-07 | Threshold persistence | 🟡 | EEPROM | Set thresholds, SAVE, restart | Thresholds restored |

### 5.2 Alarm Triggering

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| ALM-T-01 | Min threshold trigger | 🔴 | Both | Value drops below minValue | Buzzer sounds |
| ALM-T-02 | Max threshold trigger | 🔴 | Both | Value exceeds maxValue | Buzzer sounds |
| ALM-T-03 | Standard units comparison | 🔴 | Both | Set threshold in Celsius, display in F | Alarm uses Celsius comparison |
| ALM-T-04 | No hysteresis | ⚪ | Both | Value oscillates at threshold | Buzzer may chatter |
| ALM-T-05 | Multiple simultaneous alarms | 🟡 | Both | Trigger 2 sensors | Both alarms fire, single buzzer |
| ALM-T-06 | Alarm recovery | 🟡 | Both | Value returns to normal range | Buzzer stops |

### 5.3 MODE_BUTTON (Multi-Function)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| ALM-MB-01 | Alarm silence detection | 🔴 | Both | Press MODE_BUTTON during alarm in RUN mode | Buzzer stops immediately |
| ALM-MB-02 | Silence duration | 🔴 | Both | Wait 30 seconds after silence | Buzzer resumes if alarm still active |
| ALM-MB-03 | MODE_BUTTON pin config | 🟡 | Both | Check #define MODE_BUTTON pin | Correct pin (default 4) |
| ALM-MB-04 | Silence state tracking | 🟡 | Both | Check `silenced` flag in code | Flag set when button pressed |
| ALM-MB-05 | Silence auto-unsilence | 🟡 | Both | Observe after SILENCE_DURATION | Auto-unsilences after 30s |

### 5.4 Buzzer Control

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| ALM-B-01 | Buzzer frequency | 🟢 | Both | Measure with oscilloscope | 700 Hz tone |
| ALM-B-02 | Buzzer on alarm | 🔴 | Both | Trigger alarm | Continuous tone |
| ALM-B-03 | Buzzer mute on silence | 🔴 | Both | Press silence | Tone stops |
| ALM-B-04 | Buzzer pin config | 🟡 | Both | Check #define BUZZER pin | Correct PWM pin (default 3) |

---

## 6. PERSISTENCE & CONFIGURATION TESTS

### 6.1 EEPROM Save/Load (EEPROM Mode Only)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| PER-E-01 | Save configuration | 🔴 | EEPROM | 1. Configure sensors<br>2. "SAVE"<br>3. Check response | "Saved N inputs to EEPROM" |
| PER-E-02 | Load configuration | 🔴 | EEPROM | 1. Restart system<br>2. Check serial | "Loaded N inputs from EEPROM" |
| PER-E-03 | EEPROM magic validation | 🟡 | EEPROM | Corrupt EEPROM, restart | "No valid config in EEPROM" |
| PER-E-04 | EEPROM version check | 🟡 | EEPROM | Write wrong version, restart | "EEPROM version mismatch" |
| PER-E-05 | Input count bounds | 🟢 | EEPROM | Save more than MAX_INPUTS | Clamped to MAX_INPUTS |
| PER-E-06 | Blank EEPROM startup | 🔴 | EEPROM | First boot, no config | System starts with 0 inputs |
| PER-E-07 | Corrupt data recovery | 🟡 | EEPROM | Partial EEPROM corruption | Graceful failure, blank config |

### 6.2 Serial Command Interface (EEPROM Mode Only)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SER-C-01 | HELP command | 🟡 | EEPROM | Send "HELP" | Lists all commands |
| SER-C-02 | LIST INPUTS | 🔴 | EEPROM | Send "LIST INPUTS" | Shows active inputs |
| SER-C-03 | LIST APPLICATIONS | 🟡 | EEPROM | Send "LIST APPLICATIONS" | Shows 16 presets |
| SER-C-04 | LIST SENSORS | 🟡 | EEPROM | Send "LIST SENSORS" | Shows 17 sensor types |
| SER-C-05 | SET APPLICATION | 🔴 | EEPROM | "SET A0 APPLICATION CHT MAX6675" | Creates input |
| SER-C-06 | SET SENSOR | 🟡 | EEPROM | "SET A0 SENSOR MAX31855" | Changes sensor type |
| SER-C-07 | SET NAME | 🟢 | EEPROM | "SET A0 NAME MYCHT" | Updates abbreviation |
| SER-C-08 | SET DISPLAY_NAME | 🟢 | EEPROM | "SET A0 DISPLAY_NAME MyCHT" | Updates display name |
| SER-C-09 | SET UNITS | 🟡 | EEPROM | "SET A0 UNITS FAHRENHEIT" | Changes display units |
| SER-C-10 | SET ALARM | 🟡 | EEPROM | "SET A0 ALARM 50 400" | Sets thresholds |
| SER-C-11 | ENABLE command | 🟡 | EEPROM | "ENABLE A0" | Enables input |
| SER-C-12 | DISABLE command | 🟡 | EEPROM | "DISABLE A0" | Disables input |
| SER-C-13 | CLEAR command | 🟡 | EEPROM | "CLEAR A0" | Removes input |
| SER-C-14 | INFO command | 🟡 | EEPROM | "INFO A0" | Shows detailed config |
| SER-C-15 | SAVE command | 🔴 | EEPROM | "SAVE" | Persists to EEPROM |
| SER-C-16 | LOAD command | 🟡 | EEPROM | "LOAD" | Reloads from EEPROM |
| SER-C-17 | RESET command | 🟡 | EEPROM | "RESET" then "RESET CONFIRM" | Clears all config |
| SER-C-18 | Invalid command | 🟢 | EEPROM | "INVALID" | Error message |
| SER-C-19 | Case insensitivity | 🟢 | EEPROM | "set a0 application cht max6675" | Works (lowercase) |

### 6.3 Compile-Time Configuration

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| PER-S-01 | No serial commands | 🔴 | Compile | Send "HELP" | No response (serial disabled) |
| PER-S-02 | No EEPROM access | 🟡 | Compile | Check EEPROM after many restarts | No writes (wear-free) |
| PER-S-03 | Faster boot time | 🟢 | Compile | Measure time to "Init complete" | <1s vs ~2s for EEPROM mode |
| PER-S-04 | Guaranteed config | 🟡 | Compile | Corrupt EEPROM, restart | Config unaffected |

### 6.4 Config/Run Mode System (EEPROM Mode Only)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| MODE-01 | Boot to CONFIG (no EEPROM) | 🔴 | EEPROM | 1. Clear EEPROM<br>2. Restart | Boots to CONFIG mode automatically |
| MODE-02 | Boot to CONFIG (button held) | 🔴 | EEPROM | 1. Hold MODE_BUTTON during boot<br>2. Release after 1s | "CONFIG BUTTON DETECTED", enters CONFIG mode |
| MODE-03 | Boot to RUN (valid EEPROM) | 🔴 | EEPROM | 1. Valid config in EEPROM<br>2. Restart without button | "Starting in RUN mode" |
| MODE-04 | CONFIG command from RUN | 🔴 | EEPROM | 1. In RUN mode<br>2. Send "CONFIG" | Enters CONFIG mode, shows transition message |
| MODE-05 | RUN command from CONFIG | 🔴 | EEPROM | 1. In CONFIG mode<br>2. Send "RUN" | Enters RUN mode, shows transition message |
| MODE-06 | Write commands blocked in RUN | 🔴 | EEPROM | 1. In RUN mode<br>2. Send "SET A0 APPLICATION CHT" | Error: "Configuration locked in RUN mode" |
| MODE-07 | Read commands allowed in RUN | 🔴 | EEPROM | 1. In RUN mode<br>2. Send "LIST INPUTS" | Command executes successfully |
| MODE-08 | Write commands allowed in CONFIG | 🔴 | EEPROM | 1. In CONFIG mode<br>2. Send "SET A0 APPLICATION CHT" | Command executes successfully |
| MODE-09 | Serial CSV disabled in CONFIG | 🔴 | EEPROM | 1. In CONFIG mode<br>2. Check serial output | No CSV data printed |
| MODE-10 | Serial CSV enabled in RUN | 🔴 | EEPROM | 1. In RUN mode<br>2. Check serial output | CSV data printed normally |
| MODE-11 | Sensors paused in CONFIG | 🔴 | EEPROM | 1. In CONFIG mode<br>2. Apply sensor changes | Sensors not read, values frozen |
| MODE-12 | Sensors active in RUN | 🔴 | EEPROM | 1. In RUN mode<br>2. Apply sensor changes | Sensors read, values update |
| MODE-13 | MODE_BUTTON in RUN (alarm silence) | 🔴 | EEPROM | 1. In RUN mode<br>2. Alarm active<br>3. Press MODE_BUTTON | Alarm silenced (30s) |
| MODE-14 | MODE_BUTTON in CONFIG (no effect) | 🟡 | EEPROM | 1. In CONFIG mode<br>2. Press MODE_BUTTON | No effect (button only for alarm in RUN) |
| MODE-15 | CONFIG/RUN always available | 🔴 | EEPROM | 1. In any mode<br>2. Send CONFIG or RUN | Mode switches successfully (no deadlock) |

---

## 7. SENSOR TYPE TESTS

### 7.1 Thermocouple Sensors

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-T-01 | MAX6675 reading | 🔴 | Both | Connect MAX6675, apply heat | Temperature reads correctly |
| SEN-T-02 | MAX6675 fault detection | 🟡 | Both | Disconnect thermocouple | NaN or error reading |
| SEN-T-03 | MAX31855 reading | 🔴 | Both | Connect MAX31855, apply heat | Temperature reads correctly |
| SEN-T-04 | MAX31855 fault detection | 🟡 | Both | Disconnect thermocouple | NaN or error reading |
| SEN-T-05 | MAX31855 internal temp | 🟢 | Both | Read without thermocouple | Internal temp valid |
| SEN-T-06 | Thermocouple range test | 🟡 | Both | Test at 0°C, 100°C, 400°C | All readings within ±8°C |

### 7.2 Thermistor Sensors

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-R-01 | VDO 120C lookup accuracy | 🔴 | Both | Apply known resistances | Temp within ±2°C |
| SEN-R-02 | VDO 150C lookup accuracy | 🔴 | Both | Apply known resistances | Temp within ±2°C |
| SEN-R-03 | VDO 120C Steinhart accuracy | 🟡 | Both | Compare to lookup | Within 1°C of lookup |
| SEN-R-04 | VDO 150C Steinhart accuracy | 🟡 | Both | Compare to lookup | Within 1°C of lookup |
| SEN-R-05 | Thermistor open circuit | 🟡 | Both | Disconnect thermistor | Reads very high or NaN |
| SEN-R-06 | Thermistor short circuit | 🟡 | Both | Short thermistor | Reads very low or NaN |
| SEN-R-07 | Bias resistor effect | 🟢 | Both | Test with different bias R | Calculation correct |

### 7.3 Pressure Sensors

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-P-01 | VDO 5-bar accuracy | 🟡 | Both | Apply 0Ω, 100Ω, 200Ω | Pressure correct ±0.1 bar |
| SEN-P-02 | VDO 2-bar accuracy | 🟡 | Both | Apply known resistances | Pressure correct ±0.1 bar |
| SEN-P-03 | Generic boost linear | 🟡 | Both | Apply 0.5V, 2.5V, 4.5V | 0, 2.5, 5 bar |
| SEN-P-04 | MPX4250AP linear | 🟡 | Both | Apply reference voltages | Pressure formula correct |
| SEN-P-05 | Pressure out of range | 🟢 | Both | Apply -1 bar or 10 bar | Clamps or returns valid |

### 7.4 Electrical Sensors

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-E-01 | Voltage divider (12V) | 🔴 | Both | Connect 12V battery | Reads 11.5-12.8V |
| SEN-E-02 | Voltage divider formula | 🟡 | Both | Apply 5V, 10V, 15V | V_in = V_adc * (R1+R2)/R2 |
| SEN-E-03 | Voltage overvoltage | 🟡 | Both | Apply 20V (if safe) | Clamps to ADC_MAX or valid |
| SEN-E-04 | RPM W-phase reading | ⚪ | Both | Connect alternator W-phase | **PARTIAL IMPLEMENTATION** |
| SEN-E-05 | RPM pole count | ⚪ | Both | Test with different pole counts | **PARTIAL IMPLEMENTATION** |

### 7.5 Environmental Sensors (BME280)

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-B-01 | BME280 I2C detection | 🔴 | Both | Connect BME280 | "BME280 OK (at 0x76 or 0x77)" |
| SEN-B-02 | BME280 temperature | 🔴 | Both | Read ambient temperature | Room temp ±2°C |
| SEN-B-03 | BME280 pressure | 🟡 | Both | Read barometric pressure | ~1013 hPa (at sea level) |
| SEN-B-04 | BME280 humidity | 🟡 | Both | Read humidity | 30-70% RH typical |
| SEN-B-05 | BME280 elevation | 🟢 | Both | Calculate altitude | ±50m accuracy |
| SEN-B-06 | BME280 missing | 🟡 | Both | No BME280 connected | "BME280 not found", system continues |
| SEN-B-07 | Sea level pressure setting | 🟢 | Both | Change SEA_LEVEL_PRESSURE_HPA | Altitude recalculates |

### 7.6 Digital Sensors

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| SEN-D-01 | Float switch normal | 🟡 | Both | Connect float switch, normal level | Reads 1 |
| SEN-D-02 | Float switch low | 🟡 | Both | Trigger float switch, low level | Reads 0, alarm triggers |
| SEN-D-03 | Float switch inverted | 🟢 | Both | Use normally-open switch | INVERTED option works |

---

## 8. UNIT CONVERSION TESTS

### 8.1 Temperature Conversion

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| UNI-T-01 | Celsius to Fahrenheit | 🔴 | Both | 100°C input | Displays 212°F |
| UNI-T-02 | Fahrenheit to Celsius | 🟡 | Both | 32°F input | Displays 0°C |
| UNI-T-03 | Negative temperatures | 🟡 | Both | -40°C input | Displays -40°F (equal) |

### 8.2 Pressure Conversion

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| UNI-P-01 | Bar to PSI | 🔴 | Both | 1.0 bar input | Displays 14.5 PSI |
| UNI-P-02 | PSI to bar | 🟡 | Both | 29.0 PSI input | Displays 2.0 bar |
| UNI-P-03 | Bar to kPa | 🟡 | Both | 1.0 bar input | Displays 100 kPa |
| UNI-P-04 | kPa to inHg | 🟢 | Both | 101.325 kPa input | Displays 29.92 inHg |

### 8.3 Altitude Conversion

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| UNI-A-01 | Meters to feet | 🟡 | Both | 100m input | Displays 328 ft |
| UNI-A-02 | Feet to meters | 🟡 | Both | 1000 ft input | Displays 305 m |

---

## 9. PLATFORM-SPECIFIC TESTS

### 9.1 Arduino Mega 2560

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| PLT-M-01 | Mega build | 🔴 | Both | Build for Mega | Successful compilation |
| PLT-M-02 | Mega ADC (10-bit) | 🔴 | Both | Check ADC_MAX_VALUE | 1023 |
| PLT-M-03 | Mega max inputs | 🟡 | Both | Check MAX_INPUTS | 16 |
| PLT-M-04 | Mega MCP2515 CAN | 🟡 | Both | Connect MCP2515, enable CAN | CAN works |
| PLT-M-05 | Mega EEPROM size | 🟢 | EEPROM | Save many configs | 4KB available |

### 9.2 Teensy 4.0/4.1

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| PLT-T-01 | Teensy build | 🔴 | Both | Build for Teensy 4.0 | Successful compilation |
| PLT-T-02 | Teensy ADC (12-bit) | 🔴 | Both | Check ADC_MAX_VALUE | 4095 |
| PLT-T-03 | Teensy max inputs | 🟡 | Both | Check MAX_INPUTS | 40 |
| PLT-T-04 | Teensy FlexCAN | 🟡 | Both | Enable USE_FLEXCAN_NATIVE | Native CAN works |
| PLT-T-05 | Teensy EEPROM emulation | 🟢 | EEPROM | Check EEPROM functionality | 1080 bytes available |

### 9.3 Arduino Uno

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| PLT-U-01 | Uno build | 🟡 | Compile | Build for Uno (static config) | Successful compilation |
| PLT-U-02 | Uno ADC (10-bit) | 🟡 | Compile | Check ADC_MAX_VALUE | 1023 |
| PLT-U-03 | Uno max inputs | 🔴 | Compile | Check MAX_INPUTS | 6 |
| PLT-U-04 | Uno RAM limitation | 🔴 | Compile | Configure 6 sensors | Builds and runs |
| PLT-U-05 | Uno flash limitation | 🟡 | Compile | Enable all outputs | Fits in 32KB |

---

## 10. STRESS & EDGE CASE TESTS

### 10.1 System Stress Tests

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| STR-S-01 | Maximum inputs | 🟡 | Both | Configure MAX_INPUTS sensors | All work correctly |
| STR-S-02 | Rapid sensor changes | 🟢 | Both | Toggle values quickly | System stable |
| STR-S-03 | All outputs enabled | 🟡 | Both | CAN + Serial + SD + LCD + RealDash | No crashes or data loss |
| STR-S-04 | Long-term stability | 🟢 | Both | Run for 24 hours | No crashes, memory leaks |
| STR-S-05 | Rapid alarm triggering | 🟡 | Both | Oscillate value at threshold | System doesn't crash |

### 10.2 Error Handling

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| ERR-H-01 | Disconnected sensor | 🔴 | Both | Unplug thermocouple | NaN, displays "ERR" |
| ERR-H-02 | Short circuit | 🟡 | Both | Short analog input to GND | Reads 0V or clamps |
| ERR-H-03 | Overvoltage | 🟡 | Both | Apply >5V to analog (if safe) | ADC clamps to max |
| ERR-H-04 | I2C bus error | 🟡 | Both | Disconnect LCD/BME280 | System continues |
| ERR-H-05 | SPI bus error | 🟡 | Both | Disconnect thermocouple | Returns NaN, doesn't hang |
| ERR-H-06 | CAN bus error | 🟡 | Both | Disconnect CAN | System continues |
| ERR-H-07 | SD card removal | 🟡 | Both | Remove SD during logging | Recovers or handles gracefully |
| ERR-H-08 | Invalid EEPROM | 🟡 | EEPROM | Corrupt EEPROM header | Blank config, doesn't crash |
| ERR-H-09 | Memory overflow | 🟢 | Both | Configure too many sensors | Rejects gracefully |

### 10.3 Configuration Edge Cases

| Test ID | Description | Priority | Mode | Steps | Expected Result |
|---------|-------------|----------|------|-------|-----------------|
| EDG-C-01 | Zero inputs | 🟡 | Both | Start with no inputs | System runs, blank display |
| EDG-C-02 | Same pin multiple sensors | ⚪ | EEPROM | Assign A0 to two sensors | **UNDEFINED - Need to test** |
| EDG-C-03 | Invalid pin number | 🟡 | EEPROM | "SET A99 APPLICATION CHT" | Error message |
| EDG-C-04 | Alarm min > max | 🟢 | EEPROM | "SET A0 ALARM 100 50" | **NO VALIDATION - Issue** |
| EDG-C-05 | Very long names | 🟢 | EEPROM | 20-char name | Truncated to max length |

---

## 11. REGRESSION TESTS

### 11.1 Post-Architecture Migration

| Test ID | Description | Priority | Mode | Notes |
|---------|-------------|----------|------|-------|
| REG-A-01 | Static sensor deletion verified | ✅ | Both | Old static_sensors.cpp removed |
| REG-A-02 | Wrapper deletion verified | ✅ | Both | Old wrapper files removed |
| REG-A-03 | NUM_CONFIGURED_INPUTS removal | ✅ | Compile | Auto-count via #ifdef works |
| REG-A-04 | Comment accuracy | ✅ | Both | Updated to reflect unified arch |
| REG-A-05 | Build modes isolated | ✅ | Both | EEPROM and Compile modes work independently |

---

## Test Execution Summary Template

```
=== TEST RUN SUMMARY ===
Date: [YYYY-MM-DD]
Tester: [Name]
Hardware: [Mega2560/Teensy40/etc]
Firmware Version: [Git commit hash]
Mode: [EEPROM/Compile-Time]

TOTAL TESTS: [N]
PASSED: [N] (✅)
FAILED: [N] (❌)
SKIPPED: [N] (⏭️)
BLOCKED: [N] (🚫)

CRITICAL FAILURES: [List]
HIGH PRIORITY FAILURES: [List]
NOTES: [Any observations]
```

---

## Known Limitations (Do Not Test)

These features are documented as NOT IMPLEMENTED:

1. **Custom calibration serial commands** (CAL-C-01 to CAL-C-06) - Functions exist but no command handlers
2. **advanced_config.h integration** (CAL-S-01 to CAL-S-03) - File orphaned, not included anywhere
3. **OLED display** (DIS-O-01) - Only stub functions
4. **RPM custom calibration** (SEN-E-04, SEN-E-05) - Struct exists but not fully wired
5. **Pin conflict detection** (EDG-C-02) - No validation implemented
6. **Alarm hysteresis** (ALM-T-04) - Not implemented, may chatter
7. **Multi-frame CAN** - Limited to 6 bytes data per sensor

---

## Automated Testing Recommendations

### Unit Tests (PlatformIO Native)
- Calibration math functions (Steinhart-Hart, linear, polynomial)
- Unit conversion functions
- OBD2 frame building
- EEPROM header validation
- Pin parsing

### Integration Tests (Hardware-in-Loop)
- End-to-end sensor reading (MAX6675, VDO sensors)
- CAN bus frame validation
- LCD display formatting
- Alarm system triggering

### Simulation Tests (Qemu/Wokwi)
- Uno compilation size check
- Basic sensor reads with simulated inputs
- Serial command parsing

---

## Priority Testing Order

**Phase 1 (Critical Path - Must Pass):**
1. Input definition (both modes)
2. Sensor reading (all 17 types)
3. LCD display (formatting and layout)
4. Alarm system (triggering and silencing)
5. EEPROM persistence (save/load)

**Phase 2 (High Priority):**
1. CAN bus output
2. Serial CSV output
3. Unit conversion accuracy
4. Preset calibrations
5. Platform-specific tests (Mega, Teensy)

**Phase 3 (Medium Priority):**
1. RealDash protocol
2. SD card logging
3. Custom calibrations (if implemented)
4. BME280 sensors
5. Serial commands (full suite)

**Phase 4 (Low Priority - Edge Cases):**
1. Stress tests
2. Error handling
3. Edge cases and boundaries
4. Long-term stability

---

## Test Coverage Goal

**Target:** 80%+ coverage of critical path features
**Minimum:** All 🔴 CRITICAL tests must pass
**Stretch:** All 🔴 CRITICAL + 🟡 HIGH tests pass

---

**END OF TESTING PLAN**
