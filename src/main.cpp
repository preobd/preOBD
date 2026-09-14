/*
 * main.cpp - preOBD (Open Engine Monitoring System)
 * Main program loop
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "version.h"

// TEST_MODE_PIN is optional: when defined it provides a boot-time hardware
// trigger for scenario auto-activation; when omitted, the TEST CLI commands
// (INJECT/CLEAR/STOP/STATUS/<n>) remain fully available without burning a GPIO.
#include "lib/platform.h"
#include "lib/watchdog.h"

#include "sensors/calibration_types.h"
#include "sensors/sensor_library.h"
#include "lib/system_config.h"
#include "lib/bus_manager.h"
#include "lib/bus_needs.h"
#include "lib/pin_owner.h"
#include "lib/serial_manager.h"
#include "lib/pin_registry.h"
#include "lib/sd_manager.h"
#include "inputs/input.h"
#include "inputs/input_manager.h"
#include "cli/serial_config.h"
#include "control/control_manager.h"   // Control engine runtime (Rules)
#include "lib/system_mode.h"        // System mode (CONFIG/RUN)
#if INCLUDE_MODE_BUTTON
#include "lib/button_handler.h"
#endif
#include "lib/display_manager.h"        // Display runtime state management
#if INCLUDE_LED
    #include "lib/rgb_led.h"
#endif
#include "outputs/output_base.h"
#include "lib/obd_query.h"
#if INCLUDE_CAN
    #include "inputs/input_can.h"
    #include "sensors/drivers/can/can_scan.h"
#endif

// Transport abstraction layer
#include "lib/message_router.h"
#include "lib/message_api.h"
#include "lib/log_tags.h"
#include "lib/transport_mux.h"
#include "lib/transport_serial.h"
#include "lib/status_broadcaster.h"
#include "lib/alarm_broadcaster.h"
#include "lib/action_broadcaster.h"
#include "lib/live_value_broadcaster.h"
#include "lib/elm327_framed.h"
#ifdef ESP32
// Include appropriate Bluetooth transport for ESP32 variant
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #include "lib/transports/transport_ble_esp32.h"
#else
    #include "lib/transports/transport_bluetooth_esp32.h"
#endif
#endif

// Global transport instances
SerialTransport usbSerial(&Serial, "USB", 115200);

// Hardware serial transports - created for all available ports
// Actual initialization happens in initConfiguredSerialPorts() based on config
#if NUM_SERIAL_PORTS >= 1
// Serial1 hosts the nRF52840 coprocessor link. MuxTransport handles two
// modes selected at boot from systemConfig.serial.framed_enabled_mask:
//   - transparent (default): byte-for-byte passthrough, compatible with
//     any BLE module (HM-10, stock Bluefruit, pre-framed nRF firmware)
//   - framed: nRF coprocessor binary protocol (ARCHITECTURE.md §3)
// Phase 3 software flow control (HOST_NRF_CTS_PIN) wraps both modes.
#if INCLUDE_FRAMED_TRANSPORT
#  if defined(HOST_NRF_CTS_PIN)
MuxTransport hwSerial1(&Serial1, "SERIAL1", 115200, HOST_NRF_CTS_PIN);
#  else
MuxTransport hwSerial1(&Serial1, "SERIAL1", 115200);
#  endif
#else
// No nRF coprocessor on this build — Serial1 is a plain transparent transport
// (byte-for-byte equivalent to MuxTransport's transparent mode), saving the
// ~1.6 KB of framed RX/TX/decoder buffers. See profile INCLUDE_FRAMED_TRANSPORT.
#  if defined(HOST_NRF_CTS_PIN)
SerialTransport hwSerial1(&Serial1, "SERIAL1", 115200, HOST_NRF_CTS_PIN);
#  else
SerialTransport hwSerial1(&Serial1, "SERIAL1", 115200);
#  endif
#endif
#endif
#if NUM_SERIAL_PORTS >= 2
SerialTransport hwSerial2(&Serial2, "SERIAL2", 115200);
#endif
#if NUM_SERIAL_PORTS >= 3
SerialTransport hwSerial3(&Serial3, "SERIAL3", 115200);
#endif
#if NUM_SERIAL_PORTS >= 4
SerialTransport hwSerial4(&Serial4, "SERIAL4", 115200);
#endif
#if NUM_SERIAL_PORTS >= 5
SerialTransport hwSerial5(&Serial5, "SERIAL5", 115200);
#endif
#if NUM_SERIAL_PORTS >= 6
SerialTransport hwSerial6(&Serial6, "SERIAL6", 115200);
#endif
#if NUM_SERIAL_PORTS >= 7
SerialTransport hwSerial7(&Serial7, "SERIAL7", 115200);
#endif
#if NUM_SERIAL_PORTS >= 8
SerialTransport hwSerial8(&Serial8, "SERIAL8", 115200);
#endif

#ifdef ESP32
// ESP32 Bluetooth transport (Classic or BLE depending on chip)
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    BLETransportESP32 btESP32("preOBD");
#else
    BluetoothTransportESP32 btESP32("preOBD");
#endif
#endif

// Declare output module functions
extern void initOutputModules();
extern void sendToOutputs(Input*);
extern void updateOutputs();

// Declare display functions
extern void initLCD();
extern void updateLCD(Input**, int);

// Alarm logic module

// Test mode (if enabled)
#if INCLUDE_TEST_MODE
#include "test/test_mode.h"
#endif

// ===== TIME-SLICING STATE =====
// Tracks last execution time for each time-sliced operation
static uint32_t lastInputRead[MAX_INPUTS];  // Per-sensor read timing
#if INCLUDE_ALARMS
#endif
#if INCLUDE_LCD
static uint32_t lastLCDUpdate = 0;
#endif

// ===== TIME-SLICED OPERATION FUNCTIONS =====
// Extract repetitive time-slice checks into named functions for clarity

// Update LCD display in CONFIG mode
static void updateConfigModeDisplay(uint32_t now) {
    #if INCLUDE_LCD
    if (isDisplayActive() && now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
        static Input* inputPtrs[MAX_INPUTS];
        uint8_t activeCount = 0;
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            // In CONFIG mode, show any sensor with a valid pin (configured)
            if (inputs[i].pin != 0xFF) {
                inputPtrs[activeCount++] = &inputs[i];
            }
        }
        updateLCD(inputPtrs, activeCount);
        lastLCDUpdate = now;
    }
    #endif
}

// Read sensors at their individual configured intervals
static void updateSensors(uint32_t now) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        // isEnabled alone is not enough: a slot is only readable once
        // setInputSensor() has installed readFunction. Match the guard
        // readAllInputs() already uses (input_manager.cpp) so a
        // half-initialized slot can never be dispatched into. See #361.
        if (inputs[i].pin == 0xFF) continue;
        if (!inputs[i].flags.isEnabled) continue;
        if (inputs[i].readFunction == nullptr) continue;

        // Get sensor-specific minimum read interval
        const SensorInfo* sensorInfo = getSensorByIndex(inputs[i].sensorIndex);
        uint16_t interval = sensorInfo ? pgm_read_word(&sensorInfo->minReadInterval) : SENSOR_READ_INTERVAL_MS;

        // Check if enough time has elapsed for this sensor
        if (now - lastInputRead[i] >= interval) {
            inputs[i].readFunction(&inputs[i]);
            lastInputRead[i] = now;
        }
    }
}

// (Legacy per-input alarm engine retired — alarms are control-engine Rules,
// evaluated in updateControlEngine() below.)

// Evaluate control-engine Rules against live inputs (independent of INCLUDE_ALARMS
// — the engine is also active when only relays are compiled in). Drives nothing
// yet: state is observable via the RULE CLI until the annunciator/actions land.
static void updateControlEngine(uint32_t now) {
    #if INCLUDE_CONTROL_ENGINE
    static uint32_t lastControlUpdate = 0;
    if (now - lastControlUpdate >= ALARM_CHECK_INTERVAL_MS) {
        controlManagerUpdate(now);
        controlActionsUpdate(isInConfigMode());  // drive actions; suppressed in CONFIG
        lastControlUpdate = now;
    }
    #endif
}


// Update LCD display in RUN mode
static void updateDisplay(uint32_t now) {
    #if INCLUDE_LCD
    if (isDisplayActive() && now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
        static Input* inputPtrs[MAX_INPUTS];
        uint8_t activeCount = 0;
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].flags.isEnabled) {
                inputPtrs[activeCount++] = &inputs[i];
            }
        }
        updateLCD(inputPtrs, activeCount);
        lastLCDUpdate = now;
    }
    #endif
}

// Update test mode (wrapper for conditional compilation)
static void updateTestMode_Wrapper() {
    #if INCLUDE_TEST_MODE
    if (isTestModeActive()) {
        updateTestMode();
    }
    #endif
}

void setup() {

    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {};  // Wait up to 3 seconds for serial

    // Initialize system config (loads from EEPROM or uses defaults from config.h)
    // MUST happen before router.begin() so router can load correct transport mappings
    initSystemConfig();

    // Initialize pin registry FIRST - before any code registers pins
    // This was previously called later, which caused the registry to be cleared
    // after serial ports had already registered their pins
    initPinRegistry();

    // Configured inputs never enter the pin registry (mega2560 has no RAM for
    // them), and a stored-but-not-yet-started BUS selection isn't in it either
    // — hand pin_owner two probes to ask instead (#385, #379). Registered here,
    // BEFORE registerSystemPins() and every claim after it, so every claimant —
    // including the wired-bus Tier-1 reservation two lines down — sees the
    // full truth from the first pin claim of the boot, not from whichever line
    // used to come after it. inputHoldsPin() is safe to register this early:
    // it answers off the STORED config until initInputManager() makes inputs[]
    // the live table (#385's fix), so it never lies for lack of that not having
    // run yet the way it silently did before.
    setPinOwnerInputProbe(inputHoldsPin);
    setPinOwnerBusProbe(storedBusHoldsPin);

    // ---- Bus need vote, computed here so registerSystemPins() (next) can
    // gate its Tier-1 *_WIRED_BUS reservation on it (#394) --------------------
    //
    // Both voters are already available this early: storedInputBusNeeds()
    // reads the EEPROM records directly (it is explicitly written to run
    // before inputs[] is populated — see its comment in input_manager.cpp),
    // and systemConfig.displayType was loaded by initSystemConfig() above.
    // acquireRequiredBuses() at the bottom of this function reuses this same
    // value rather than recomputing it — the vote must not change between the
    // two call sites.
    uint8_t busNeeds = storedInputBusNeeds();
    #if INCLUDE_LCD
    // The display votes only where its driver is in the image: a displayType
    // carried over from another board can't be served by a firmware that has no
    // LCD code, and must not hold that board's SDA/SCL hostage.
    busNeeds |= busNeedsForDisplayType(systemConfig.displayType);
    #endif

    // Register system pins in the pin registry. Includes the Tier-1 wired-bus
    // reservation (#379) when a *_WIRED_BUS declaration is in effect AND the
    // stored configuration actually needs that bus (#394) — see the comment
    // in system_config.cpp for why both conditions are required.
    registerSystemPins(busNeeds);

    // Initialize configured serial ports based on SystemConfig.serial
    // This replaces the old hardcoded Serial1.begin() / Serial2.begin() calls
    initConfiguredSerialPorts();

    // Apply framed-mode setting for the nRF coprocessor link. Default zero
    // = transparent passthrough (backwards-compatible with HM-10/stock
    // Bluefruit/pre-framed nRF firmware). Set via the CLI:
    //   BUS SERIAL <port> FRAMED ENABLE
#if INCLUDE_FRAMED_TRANSPORT && NUM_SERIAL_PORTS >= 1
    hwSerial1.setFramedEnabled(systemConfig.serial.framed_enabled_mask & (1 << 0));
#endif

    // Initialize transport router - register USB serial (always available)
    router.registerTransport(TRANSPORT_USB_SERIAL, &usbSerial);

    // Register all available hardware serial transports
    // They will only be usable if enabled via BUS SERIAL command
#if NUM_SERIAL_PORTS >= 1
    router.registerTransport(TRANSPORT_SERIAL1, &hwSerial1);
#endif
#if NUM_SERIAL_PORTS >= 2
    router.registerTransport(TRANSPORT_SERIAL2, &hwSerial2);
#endif
#if NUM_SERIAL_PORTS >= 3
    router.registerTransport(TRANSPORT_SERIAL3, &hwSerial3);
#endif
#if NUM_SERIAL_PORTS >= 4
    router.registerTransport(TRANSPORT_SERIAL4, &hwSerial4);
#endif
#if NUM_SERIAL_PORTS >= 5
    router.registerTransport(TRANSPORT_SERIAL5, &hwSerial5);
#endif
#if NUM_SERIAL_PORTS >= 6
    router.registerTransport(TRANSPORT_SERIAL6, &hwSerial6);
#endif
#if NUM_SERIAL_PORTS >= 7
    router.registerTransport(TRANSPORT_SERIAL7, &hwSerial7);
#endif
#if NUM_SERIAL_PORTS >= 8
    router.registerTransport(TRANSPORT_SERIAL8, &hwSerial8);
#endif

#ifdef ESP32
    if (btESP32.begin()) {
        router.registerTransport(TRANSPORT_ESP32_BT, &btESP32);
        msg.debug.info(TAG_BT, "ESP32 Bluetooth initialized");
    } else {
        msg.debug.warn(TAG_BT, "ESP32 Bluetooth failed to initialize");
    }
#endif
    router.begin();  // Load config from EEPROM
    flushSystemConfigBootDiagnostics();
    // Every pin claim made above — registerSystemPins() (:289) and
    // initConfiguredSerialPorts() (:293) both run before router.begin() has a
    // transport — buffered its refusal, if any, instead of printing to a
    // router with nowhere to send it (#396). Replay now that a transport
    // exists; every claim after this point prints immediately, as before.
    flushPinOwnerBootDiagnostics();
    // INVARIANT: every broadcaster's init() below runs BEFORE initInputManager()
    // (further down this function) sentinel-fills and loads inputs[], and
    // before the control tables (rules[]/actions[]) are loaded with them.
    // Nothing in these init()s may read inputs[], rules[], or actions[] —
    // BSS-zero is indistinguishable from real data (pin==0 looks like a
    // configured digital-pin-0 input; id==0 looks like a configured
    // rule/action) — so each one primes its broadcaster with a CONSTANT
    // instead. Reading the table here was #352 (LiveValueBroadcaster read
    // occupancy off the not-yet-loaded table and phantom-cleared every unused
    // slot for a minute after boot); see live_value_broadcaster.cpp:130-138
    // and action_broadcaster.cpp:117-122 for the per-broadcaster reasoning,
    // and #369 for why this comment and the boot-order test fixtures
    // (test/test_broadcaster/, test/test_action_broadcaster/,
    // test/test_live_value_broadcaster/, boot_order_fixture.h) exist — no
    // green suite would have caught a regression here otherwise, because
    // every broadcaster test configured its table BEFORE calling init().
    // ASSERT_INPUTS_TABLE_LIVE() (input_manager.h) backstops this at each
    // table-reading function in DEBUG/NATIVE_TEST builds.
    StatusBroadcaster::init();
    AlarmBroadcaster::init();
    ActionBroadcaster::init();
    LiveValueBroadcaster::init();
    msg.control.println();
    msg.control.println(F("                 ____  ___  ___  "));
    msg.control.println(F("   ___  _______ / __ \\/ _ )/ _ \\ "));
    msg.control.println(F("  / _ \\/ __/ -_) /_/ / _  / // / "));
    msg.control.println(F(" / .__/_/  \\__/\\____/____/____/  "));
    msg.control.println(F("/_/                              "));
    msg.control.println();    
    msg.control.println(F("Open Vehicle Monitoring ==============="));
    msg.control.print(F("Firmware version "));
    msg.control.println(firmwareVersionString());
    msg.control.println();

    // Configure ADC for this platform
    setupADC();
    msg.debug.info(TAG_ADC, "ADC configured");

    // ---- Bus ownership is settled HERE, in one pass, before any client ------
    //
    // First the explicit selections (`BUS I2C <n>` / `BUS SPI <n>` / CAN), then
    // every bus the stored CONFIGURATION needs. The voters are the stored input
    // records — via each sensor's declared requirement — and the display, which
    // is one more voter with no special status: an add-on LCD must not be what
    // decides whether this board has an I2C bus (#358).
    //
    // Doing both before initLCD() and before loadInputConfig()'s record loop is
    // the point: SDA/SCL are reserved before any stored pin is validated, so the
    // outcome cannot depend on which client initializes first, or in what order
    // the records happen to sit in EEPROM.
    //
    // busNeeds was already computed above, before registerSystemPins() (#394) —
    // reused here rather than recomputed so the two call sites can never see a
    // different vote.
    initConfiguredBuses();
    acquireRequiredBuses(busNeeds);

    // storedInputBusNeeds() is not wasted work now that #377 removed the
    // DEFAULT_*_BUS guess: acquireRequiredBuses() can legitimately come back
    // empty-handed, so the pre-scan changes verb from "acquire" to "diagnose" —
    // an unmet need is reported rather than silently leaving a configured sensor
    // unable to read.
    //
    // That reporting belongs INSIDE acquire*(), not here. There are two distinct
    // ways to come back with no bus — "I refused to guess" and "you named a bus
    // and it could not start" — and only acquire*() can tell them apart; a test
    // out here sees the same nullptr for both. Doing it here anyway is what made
    // a pin conflict on a DECLARED bus report "no I2C bus declared or selected",
    // telling the user their declaration did not exist. acquireRequiredBuses()
    // above already covers every bit in busNeeds, so this needs no line of its own.

    // Initialize display. The bus it needs, if any, is already up and owned by
    // the pass above; initLCD()'s acquireI2C() is now just a lookup.
    //
    // INCLUDE_LCD means the driver is in the image, NOT that a screen is fitted —
    // nothing at compile time can know that, which is why there is no wiring flag
    // for it. The runtime CONFIGURATION is displayType, so gate the actual
    // bring-up on that: a board with DISPLAY TYPE NONE must not acquire the I2C
    // bus, and its SDA/SCL pins stay free for inputs (#351).
    // displayEnabled remains the on/off toggle, handled by initDisplayManager().
    #if INCLUDE_LCD
    if (systemConfig.displayType == DISPLAY_LCD) {
        initLCD();
    } else {
        // The board can drive an LCD but none is declared. Say so: a fitted
        // display would otherwise stay dark with no explanation, and DISPLAY
        // STATUS reports "Enabled" (the on/off toggle) either way, so nothing
        // else on the board would tell the user what is wrong.
        msg.control.println(F("Note: no display configured (DISPLAY TYPE LCD to enable one)"));
        // Not "the I2C pins are free": since #358 an I2C SENSOR brings the bus up
        // on its own, so the display's absence no longer decides that either way.
        msg.control.println(F("  The display is not holding an I2C bus. BUS I2C shows what is."));
    }
    #endif

    // Initialize CAN input subsystem (if enabled)
    #if INCLUDE_CAN
    if (initCANInput()) {
        msg.debug.info(TAG_CAN, "CAN input subsystem initialized");
    }
    #endif

    // Initialize SD card (shared by SD logging and JSON config)
    initSD();

    // Initialize input manager (loads from EEPROM or config.h)
    bool eepromConfigLoaded = initInputManager();

#if INCLUDE_CONTROL_ENGINE
    // Arm Rules from EEPROM now that SystemConfig + inputs are loaded (boot-load).
    controlManagerInit(millis());
    controlActionsInit();   // configure action target pins as outputs
#endif

#if INCLUDE_MODE_BUTTON
    initButtonHandler();
#endif

    initDisplayManager();

    // Initialize RGB LED indicator
    #if INCLUDE_LED
    initRGBLed();
    #endif

    // Initialize output modules
    initOutputModules();

    // Build OBD-II PID lookup table (shared by CAN and ELM327 output paths).
    // Called unconditionally so it works even when CAN output is not enabled.
    obdQuery_buildLookupTable();

    // Bind the framed-transport ELM327 channel (type 0x30) to the parser. Uses
    // the same OBD lookup table just built; no-op stub unless both
    // INCLUDE_FRAMED_TRANSPORT and INCLUDE_ELM327 are set.
    Elm327Framed::init();

    // Wait for sensors to stabilize
    msg.debug.info(TAG_SENSOR, "Waiting for sensors to stabilize...");
    // Feed the watchdog across the wait. initSD() arms the WDT at 2 s and
    // Teensy 4.x cannot disarm it (#243), yet nothing feeds it again until the
    // end of setup() — so this 1 s sleep plus the surrounding init was running
    // on ~100 ms of margin, and any growth in setup tipped it into a boot loop.
    // MAX6675 still gets its full ~1 s (needs ~220 ms for the first conversion).
    for (uint8_t i = 0; i < 10; i++) {
        delay(100);
        watchdogReset();
    }

    // Initialize per-sensor read timers (all start at 0)
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        lastInputRead[i] = 0;
    }

    msg.debug.info(TAG_SYSTEM, "Initialization complete!");

    // ===== TEST MODE ACTIVATION =====
#if INCLUDE_TEST_MODE
    // Initialize test mode system
    initTestMode();

    // Boot-time hardware trigger is optional — only when a profile defines
    // TEST_MODE_PIN. CLI-driven injection works regardless.
    #ifdef TEST_MODE_PIN
    // Check if test mode trigger pin is pulled LOW
    pinMode(TEST_MODE_PIN, INPUT_PULLUP);
    delay(10);  // Allow pin to stabilize

    if (digitalRead(TEST_MODE_PIN) == LOW) {
        msg.debug.info(TAG_SYSTEM, "TEST MODE TRIGGER DETECTED!");

        // List available scenarios
        listTestScenarios();

        // Start default scenario (if not 0xFF)
        #if DEFAULT_TEST_SCENARIO != 0xFF
        startTestScenario(DEFAULT_TEST_SCENARIO);
        #else
        msg.debug.info(TAG_SYSTEM, "Test mode initialized but no default scenario set.");
        msg.debug.info(TAG_SYSTEM, "Use serial commands to start a scenario.");
        #endif
    } else {
        msg.debug.info(TAG_SYSTEM, "Test mode available (pin %d is HIGH, normal operation)", TEST_MODE_PIN);
    }
    #else
    msg.debug.info(TAG_SYSTEM, "Test mode available (CLI injection; no hardware trigger pin)");
    #endif
#endif

    initSerialConfig();

#if INCLUDE_CONTROL_ENGINE
    // Register action target pins LAST — after every hardware-fixed pin (system,
    // bus, RGB, serial) is in the registry — so a stray action target is flagged
    // as a conflict rather than silently winning a pin those subsystems need.
    controlSyncActionPins();
#endif

    // Initialize system mode and detect boot mode
    initSystemMode();
    SystemMode bootMode = detectBootMode(eepromConfigLoaded);
    setMode(bootMode);

    // Only enable watchdog in RUN mode (CONFIG mode doesn't need it)
    if (bootMode == MODE_RUN) {
        watchdogEnable(2000);
        msg.debug.info(TAG_SYSTEM, "Watchdog enabled (2s timeout)");
    } else {
        // On Teensy 4.x the watchdog simply was never enabled here (vs. actively
        // disabled) — it can't be disabled once armed. See #243.
        msg.debug.info(TAG_SYSTEM, "Watchdog disabled (CONFIG mode)");
    }

    // Boot is one reporting episode for the no-bus guidance; everything past
    // this point is interactive. Re-arm so configuring an I2C/SPI input at the
    // CLI with no bus available says what to run, instead of being silenced by
    // a boot that already printed it once (#377).
    rearmBusDiagnostics();

    // Same boundary, for pin ownership: every boot-time claim has now been made,
    // so a conflict from here on is one the user is creating interactively, not
    // a stored config that disagrees with the hardware. Switches pinsConflict()
    // off the rank-based "stale configuration" wording (ARCHITECTURE.md §12.4).
    pinOwnerBootComplete();
}

void loop() {
    // Get current time once per loop
    uint32_t now = millis();

    // Reset watchdog at start of every loop iteration
    watchdogReset();

    // Update transport router (poll transports, handle housekeeping, process commands)
    router.update();  // Now handles command input from ALL transports

    // Push system status to framed peers (nRF coprocessor). Cheap no-op when
    // the transport is in transparent mode; placed before the CONFIG-mode
    // early return so heartbeats fire in both modes.
    StatusBroadcaster::tick();
    AlarmBroadcaster::tick();
    ActionBroadcaster::tick();
    LiveValueBroadcaster::tick();

    // Process button events (short press = silence alarm, long press = toggle display)
#if INCLUDE_MODE_BUTTON
    ButtonPress buttonEvent = updateButtonHandler();
    if (buttonEvent == BUTTON_LONG_PRESS) {
        toggleDisplayRuntime();
    }
#endif

    // CONFIG mode is a write-gate, NOT a stop-the-world freeze (#268). The
    // engine keeps running in both modes so live consumers — OBD/ELM, CAN
    // broadcast, RealDash, and the webapp's live values — stay fresh even
    // while a config client is connected. (Previously a CONFIG early-return
    // froze updateSensors(), which is why a connected webapp stalled Torque's
    // readings — see #266.) Gated in CONFIG: physical actuation
    // (control-engine Actions via controlActionsUpdate() + the alarm buzzer
    // self-suppress so disconnected/test sensors on the bench can't flip
    // relays or sound the buzzer), and
    // the recording/serial outputs — serial CSV (shared-terminal usability)
    // and SD logging (CONFIG is setup, not operation; don't persist bench
    // junk into the drive log) both self-silence in CONFIG.
    #if INCLUDE_CAN
    updateCANInput();  // Poll CAN bus and populate frame cache
    updateCANScan();   // No-op unless a CONFIG-mode SCAN is active
    #endif
    updateSensors(now);
    updateControlEngine(now);
    sendToOutputs(now);  // Data-driven time-sliced output sending
    if (isInConfigMode()) {
        updateConfigModeDisplay(now);
    } else {
        updateDisplay(now);
    }
    updateOutputs();     // Housekeeping: drain buffers, handle RX

    // Update RGB LED effects (non-blocking)
    #if INCLUDE_LED
    updateRGBLed();
    #endif

    // Update test mode if active
    updateTestMode_Wrapper();

    // NO DELAY - loop runs as fast as possible
    // Time-slicing controls when operations execute
}
