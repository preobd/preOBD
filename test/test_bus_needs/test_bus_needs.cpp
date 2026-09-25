// Native unit test: bus acquisition is driven by what needs a bus (#358).
//
// Two properties, both of which the firmware got wrong before:
//
//  1. WHO decides. Bus bring-up used to be a side effect of whichever client
//     initialized first — in practice only the LCD, because it was the sole
//     caller of acquireI2C(). An I2C sensor therefore worked only if a display
//     happened to be configured. The stored CONFIGURATION decides now:
//     storedInputBusNeeds() pre-scans the records and the display is one more
//     voter (busNeedsForDisplayType) with no special status.
//
//  2. WHO is consulted before a bus claims copper. i2cBusPinsConflict() was the
//     only check that looked at configured inputs (which never enter the pin
//     registry — mega2560 has no RAM for them), and it was wired to exactly two
//     CLI commands: no boot path, and no bus but I2C. pinsConflict() /
//     claimPins() in pin_owner.cpp is that check's one home now, used by every
//     pin claimant — I2C, SPI, CAN, serial, RGB, system pins, actions — at
//     boot and at runtime.
//
// The peripheral half (Wire/SPI/FlexCAN) is not reachable on the host, so what
// is tested here is the decision logic — which is where the bug lived.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/storage/bus_needs.cpp"   // display vote; the sensor mapping is
                                        // inline in sensor_helpers.h
// SENSOR_LIBRARY / UNITS_REGISTRY / APPLICATION_PRESETS are now defined in
// their own .cpp (#400), not inline in the header — pull the data in.
// SENSOR_CATEGORIES too (missed by #400, fixed separately) — pull it in.
#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/input_manager.cpp"
#include "../../src/storage/input_store.cpp"   // EEPROM persistence, split out by #402 step 2
#include "../../src/inputs/measurement_compat.cpp"  // units check on load (#458)

// ---------------------------------------------------------------------------
// Firmware symbols the included TUs expect from elsewhere.
// ---------------------------------------------------------------------------
MessageRouter router;
MessageAPI msg;
SystemConfig systemConfig;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// initInputManager()/loadInputConfig() (exercised end to end by the #385
// self-conflict regression tests below) touch the control-engine tables even
// though nothing here configures any Rule/Action — an empty, inert stand-in
// for each is enough.
#if INCLUDE_RULES_ENGINE
Rule rules[MAX_RULES];
Action actions[MAX_ACTIONS];
ActionLink actionLinks[MAX_ACTION_LINKS];
void rulesResetTables() {}
uint8_t rulesSweepActionRules() { return 0; }
#endif

int8_t analogIndexForPin(uint8_t) { return -1; }
// requireInput() now comes from the amalgamated input_manager.cpp (#411).
bool i2cBusPinsConflict(uint8_t) { return false; }

// Sensor read/init entry points referenced by SENSOR_LIBRARY[] as data.
#define STUB_READ(fn) void fn(Input*) {}
STUB_READ(readMAX6675)          STUB_READ(readMAX31855)
STUB_READ(readThermistorSteinhart) STUB_READ(readThermistorLookup) STUB_READ(readThermistorBeta)
STUB_READ(readLinearSensor)     STUB_READ(readPressureTable)
STUB_READ(readPressureTableDescending) STUB_READ(readPressurePolynomial)
STUB_READ(readVoltageDivider)   STUB_READ(readLevelTable)
STUB_READ(readDigitalFloatSwitch) STUB_READ(readHallSpeed)
STUB_READ(readWPhaseRPM)        STUB_READ(readBME280Temp)
STUB_READ(readBME280Pressure)   STUB_READ(readBME280Humidity)
STUB_READ(readBME280Elevation)
STUB_READ(initThermocoupleCS)   STUB_READ(initFloatSwitch)
STUB_READ(initHallSpeed)        STUB_READ(initWPhaseRPM)
STUB_READ(initBME280)
#undef STUB_READ

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
static const char* OWNER_WIRE = "Wire";
static const char* OWNER_SPI  = "SPI";
static const char* OWNER_CAN  = "CAN1";

void setUp(void) {
    initPinRegistry();
    EEPROM.clear();
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) inputs[i].pin = 0xFF;
    numActiveInputs = 0;
    systemConfig.displayType = DISPLAY_NONE;
    setPinOwnerInputProbe(inputHoldsPin);
    // This suite drives inputs[] directly via giveInputThePin() rather than
    // through initInputManager(), so tell inputHoldsPin() (#385) inputs[] is
    // the live table. Section 4 below flips this back off to exercise the
    // boot-window EEPROM path on its own.
    inputsTableIsLive = true;
}
void tearDown(void) { setPinOwnerInputProbe(nullptr); }

// Put a configured input on a physical pin (slot 0 unless taken).
static void giveInputThePin(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) { inputs[i].pin = pin; return; }
    }
    TEST_FAIL_MESSAGE("no free input slot");
}

static uint16_t hashOfSensor(const char* name) {
    uint8_t index = getSensorIndexByName(name);
    TEST_ASSERT_TRUE_MESSAGE(index != 0 || strcmp(name, "NONE") == 0,
                             "sensor name not in the library");
    return pgm_read_word(&SENSOR_LIBRARY[index].nameHash);
}

// Lay down a valid config store holding `n` records with the given sensors.
// Only the fields storedInputBusNeeds() reads have to be right — it never
// verifies the checksum (loadInputConfig() does, moments later).
static void writeStore(const char* const* sensorNames, const uint8_t* pins, uint8_t n) {
    ConfigStoreHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.numInputs = n;
    EEPROM.put(0, header);

    for (uint8_t i = 0; i < n; i++) {
        InputEEPROM rec;
        memset(&rec, 0, sizeof(rec));
        rec.pin = pins[i];
        rec.sensorHash = hashOfSensor(sensorNames[i]);
        EEPROM.put(EEPROM_HEADER_SIZE + (size_t)i * EEPROM_INPUT_SIZE, rec);
    }
}

// A FULLY valid store — checksummed, unlike writeStore() above — for exercising
// loadInputConfig() end to end (initInputManager() calls it). xorBytes() is a
// static in input_manager.cpp, reachable here because this TU #includes that
// .cpp directly (line 30).
static void writeValidStore(const char* const* sensorNames, const uint8_t* pins, uint8_t n) {
    ConfigStoreHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.numInputs = n;

    uint8_t checksum = 0;
    for (uint8_t i = 0; i < n; i++) {
        InputEEPROM rec;
        memset(&rec, 0, sizeof(rec));
        rec.pin = pins[i];
        rec.sensorHash = hashOfSensor(sensorNames[i]);
        EEPROM.put(EEPROM_HEADER_SIZE + (size_t)i * EEPROM_INPUT_SIZE, rec);
        checksum ^= xorBytes(&rec, sizeof(rec));
    }
    header.checksum = checksum;
    EEPROM.put(0, header);
}

// ===========================================================================
// 1. The predicate: a bus must not claim copper a configured input is on
// ===========================================================================

// The core of #358's silent-wrong ordering: inputs are invisible to the pin
// registry, so reservePins_unchecked() alone would hand this bus the pin and say nothing.
static void test_i2c_claim_refused_when_an_input_holds_a_bus_pin(void) {
    giveInputThePin(19);                       // A5 / SCL on Teensy 4.x

    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_FALSE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));

    // …and the refusal claims nothing at all, not even the free half.
    TEST_ASSERT_TRUE(isPinAvailable(18));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// The generalization: the check used to exist for I2C only.
static void test_spi_claim_refused_when_an_input_holds_a_bus_pin(void) {
    giveInputThePin(13);                       // SCK

    const uint8_t pins[3] = { 11, 12, 13 };
    TEST_ASSERT_FALSE(claimPins(pins, 3, PIN_RESERVED, PIN_RANK_BUS, "SPI", OWNER_SPI));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

static void test_can_claim_refused_when_an_input_holds_a_bus_pin(void) {
    giveInputThePin(23);

    const uint8_t pins[2] = { 22, 23 };
    TEST_ASSERT_FALSE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "CAN", OWNER_CAN));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// A bus whose pins nobody holds still comes up — the guard must not be a veto
// on everything.
static void test_claim_succeeds_when_no_input_is_in_the_way(void) {
    giveInputThePin(14);

    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));
    TEST_ASSERT_EQUAL_STRING(OWNER_WIRE, getPinDescription(18));
    TEST_ASSERT_EQUAL_STRING(OWNER_WIRE, getPinDescription(19));
}

// initConfiguredBuses()'s fallback and acquireI2C() can both reach a bus that
// is already up; re-claiming must be a no-op, not a refusal.
static void test_reclaim_by_the_same_owner_still_succeeds(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));
    TEST_ASSERT_EQUAL(2, getPinRegistrySize());
}

// 0xFF means "this bus has no such pin here" — and is also the free-slot marker
// in inputs[], which is exactly the phantom conflict the probe must not report.
static void test_absent_pins_are_not_an_input_conflict(void) {
    const uint8_t pins[2] = { 0xFF, 0xFF };
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "CAN", OWNER_CAN));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// Another system function on the pin is still a conflict — the registry half of
// the predicate survives.
static void test_registry_owner_is_still_a_conflict(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(16, PIN_RESERVED, "Serial4", PIN_RANK_SERIAL));

    const uint8_t pins[2] = { 17, 16 };
    TEST_ASSERT_FALSE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", "Wire1"));
    TEST_ASSERT_EQUAL_STRING("Serial4", getPinDescription(16));
}

// With no probe registered (early boot, before inputs exist) the answer is
// "no input holds it" — true at that point, and it must not crash.
static void test_unregistered_probe_falls_back_to_the_registry(void) {
    setPinOwnerInputProbe(nullptr);
    giveInputThePin(19);

    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));
}

// ===========================================================================
// 2. Requirements are declared by the sensor, not discovered at init time
// ===========================================================================

static void test_i2c_sensor_declares_an_i2c_need(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_I2C, busNeedsForSensorHash(hashOfSensor("BME280_TEMP")));
}

static void test_thermocouple_declares_an_spi_need(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_SPI, busNeedsForSensorHash(hashOfSensor("MAX6675")));
    TEST_ASSERT_EQUAL(BUS_NEED_SPI, busNeedsForSensorHash(hashOfSensor("MAX31855")));
}

static void test_analog_and_digital_sensors_need_no_bus(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_NONE, busNeedsForSensorHash(hashOfSensor("VDO_120C_TABLE")));
    // A float switch is CAL_NONE on a digital pin like a thermocouple, but it is
    // MEASURE_DIGITAL — it must not be mistaken for an SPI part.
    TEST_ASSERT_EQUAL(BUS_NEED_NONE, busNeedsForSensorHash(hashOfSensor("FLOAT_SWITCH")));
}

static void test_unknown_sensor_hash_needs_no_bus(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_NONE, busNeedsForSensorHash(0xDEAD));
}

// The display votes like anything else: it neither wins nor is exempt.
static void test_display_is_one_more_voter(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_I2C, busNeedsForDisplayType(DISPLAY_LCD));
    TEST_ASSERT_EQUAL(BUS_NEED_NONE, busNeedsForDisplayType(DISPLAY_NONE));
}

// ===========================================================================
// 3. The boot pre-scan: the stored records are read BEFORE anything loads
// ===========================================================================

static void test_stored_i2c_sensor_asks_for_the_i2c_bus(void) {
    const char* sensors[] = { "BME280_TEMP" };
    const uint8_t pins[]  = { 0xF0 };
    writeStore(sensors, pins, 1);

    TEST_ASSERT_EQUAL(BUS_NEED_I2C, storedInputBusNeeds());
}

static void test_stored_thermocouple_asks_for_the_spi_bus(void) {
    const char* sensors[] = { "MAX6675" };
    const uint8_t pins[]  = { 10 };
    writeStore(sensors, pins, 1);

    TEST_ASSERT_EQUAL(BUS_NEED_SPI, storedInputBusNeeds());
}

// The ordering hazard the issue is about: with acquisition driven by the
// records rather than by init order, WHERE the bus-needing record sits in the
// store cannot change the outcome.
static void test_record_order_does_not_change_the_answer(void) {
    const char* first[]  = { "BME280_TEMP", "VDO_120C_TABLE", "VDO_120C_TABLE" };
    const char* last[]   = { "VDO_120C_TABLE", "VDO_120C_TABLE", "BME280_TEMP" };
    const uint8_t pins[] = { 0xF0, 14, 19 };

    writeStore(first, pins, 3);
    const uint8_t needsFirst = storedInputBusNeeds();

    EEPROM.clear();
    writeStore(last, pins, 3);
    const uint8_t needsLast = storedInputBusNeeds();

    TEST_ASSERT_EQUAL(BUS_NEED_I2C, needsFirst);
    TEST_ASSERT_EQUAL(needsFirst, needsLast);
}

static void test_both_buses_can_be_needed_at_once(void) {
    const char* sensors[] = { "MAX6675", "BME280_PRESSURE" };
    const uint8_t pins[]  = { 10, 0xF0 };
    writeStore(sensors, pins, 2);

    TEST_ASSERT_EQUAL(BUS_NEED_I2C | BUS_NEED_SPI, storedInputBusNeeds());
}

// A board with only analog inputs must keep its SDA/SCL free — bringing a bus
// up "just in case" is the behaviour break this whole change has to avoid.
static void test_analog_only_config_asks_for_nothing(void) {
    const char* sensors[] = { "VDO_120C_TABLE", "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 18, 19 };
    writeStore(sensors, pins, 2);

    TEST_ASSERT_EQUAL(BUS_NEED_NONE, storedInputBusNeeds());
}

static void test_blank_eeprom_asks_for_nothing(void) {
    TEST_ASSERT_EQUAL(BUS_NEED_NONE, storedInputBusNeeds());
}

// A store from another firmware version is not walked at all: its records are
// not this layout, so their bytes must not be read as sensor hashes.
static void test_foreign_store_version_asks_for_nothing(void) {
    const char* sensors[] = { "BME280_TEMP" };
    const uint8_t pins[]  = { 0xF0 };
    writeStore(sensors, pins, 1);

    ConfigStoreHeader header;
    EEPROM.get(0, header);
    header.version = EEPROM_VERSION + 1;
    EEPROM.put(0, header);

    TEST_ASSERT_EQUAL(BUS_NEED_NONE, storedInputBusNeeds());
}

// A record count that would run past the SystemConfig block is a corrupt
// header; the scan must refuse it rather than read into the config region.
static void test_impossible_record_count_asks_for_nothing(void) {
    const char* sensors[] = { "BME280_TEMP" };
    const uint8_t pins[]  = { 0xF0 };
    writeStore(sensors, pins, 1);

    ConfigStoreHeader header;
    EEPROM.get(0, header);
    header.numInputs = 255;
    EEPROM.put(0, header);

    TEST_ASSERT_EQUAL(BUS_NEED_NONE, storedInputBusNeeds());
}

// ===========================================================================
// 4. The boot window: inputHoldsPin() before inputs[] is live (#385)
//
// registerSystemPins() and the bus bring-up that follows it run BEFORE
// initInputManager() populates inputs[] — in that window every real slot
// reads pin==0xFF (BSS-zero), so a probe that only ever looked at inputs[]
// would answer "not held" for every stored input, and "held" for pin 0 (the
// 0xFF guard's job). inputHoldsPin() must instead read the SAME stored
// records loadInputConfig() will apply, exactly like storedInputBusNeeds()
// does for bus needs.
// ===========================================================================

static void test_stored_input_is_seen_before_inputs_table_is_live(void) {
    inputsTableIsLive = false;   // the exact state at the top of setup()
    const char* sensors[] = { "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 7 };
    writeStore(sensors, pins, 1);

    // inputs[] itself is still blank (setUp() cleared it) — the probe cannot
    // be reading THAT and getting the right answer.
    TEST_ASSERT_NULL(getInputByPin(7));
    TEST_ASSERT_TRUE(inputHoldsPin(7));
    TEST_ASSERT_FALSE(inputHoldsPin(8));   // an unrelated pin is not "held"
}

// The #351/#358 failure mode restated for boot: a bus claim over a pin a
// STORED (not-yet-loaded) input occupies must still be refused.
static void test_bus_claim_refused_against_a_stored_input_before_load(void) {
    inputsTableIsLive = false;
    const char* sensors[] = { "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 19 };
    writeStore(sensors, pins, 1);

    const uint8_t busPins[2] = { 18, 19 };
    TEST_ASSERT_FALSE(claimPins(busPins, 2, PIN_RESERVED, PIN_RANK_BUS, "I2C", OWNER_WIRE));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// pin 0xFF is BOTH "no such pin on this platform" (bus_defaults) and the
// free-slot sentinel inputs[] uses — the boot-window scan must not confuse
// "no records reference pin 0xFF" with "pin 0xFF is held".
static void test_boot_window_probe_does_not_misread_the_free_slot_sentinel(void) {
    inputsTableIsLive = false;
    EEPROM.clear();   // no store at all — every slot would read 0x00 if BSS-zero were consulted
    TEST_ASSERT_FALSE(inputHoldsPin(0));
    TEST_ASSERT_FALSE(inputHoldsPin(0xFF));
}

// Regression: loadInputConfig() validates record i by asking
// the SAME store (via the boot-window probe above) whether the pin is already
// held — WHILE record i's own bytes are still sitting right there in EEPROM,
// unapplied. Without self-exclusion by record index, every legitimate single
// input record "conflicted" with itself and was silently dropped on every
// boot (#385 follow-up). Reaching it requires a properly checksummed store:
// a stub that skips the checksum is rejected earlier, before this code runs.
static void test_loadInputConfig_does_not_self_conflict_its_own_records(void) {
    // The real boot state (setUp() flips this true for the OTHER tests in
    // this file, which drive inputs[] directly — undo that here): before
    // initInputManager() runs, inputs[] is not live yet, which is exactly the
    // condition that has to hold for this regression to be reachable at all.
    inputsTableIsLive = false;
    const char* sensors[] = { "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 7 };
    writeValidStore(sensors, pins, 1);

    TEST_ASSERT_TRUE(initInputManager());
    TEST_ASSERT_NOT_NULL_MESSAGE(getInputByPin(7),
        "a lone stored record must survive its own load, not self-conflict");
}

// The same load, with TWO records on DIFFERENT pins — both must survive, not
// just whichever happens to be checked first.
static void test_loadInputConfig_does_not_self_conflict_multiple_records(void) {
    inputsTableIsLive = false;
    const char* sensors[] = { "VDO_120C_TABLE", "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 7, 9 };
    writeValidStore(sensors, pins, 2);

    TEST_ASSERT_TRUE(initInputManager());
    TEST_ASSERT_NOT_NULL(getInputByPin(7));
    TEST_ASSERT_NOT_NULL(getInputByPin(9));
}

// A GENUINE duplicate (two stored records naming the SAME pin) must still be
// caught — self-exclusion must be scoped to exactly the record being checked,
// not to "any record sharing this pin".
static void test_loadInputConfig_still_catches_a_genuine_duplicate_pin(void) {
    const char* sensors[] = { "VDO_120C_TABLE", "VDO_120C_TABLE" };
    const uint8_t pins[]  = { 7, 7 };
    writeValidStore(sensors, pins, 2);

    TEST_ASSERT_TRUE(initInputManager());
    // Exactly one of the two survives (the first one loaded); the record
    // count is what proves the second was refused, not silently merged.
    TEST_ASSERT_NOT_NULL(getInputByPin(7));
    uint8_t occupied = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) if (inputs[i].pin != 0xFF) occupied++;
    TEST_ASSERT_EQUAL_UINT8(1, occupied);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_i2c_claim_refused_when_an_input_holds_a_bus_pin);
    RUN_TEST(test_spi_claim_refused_when_an_input_holds_a_bus_pin);
    RUN_TEST(test_can_claim_refused_when_an_input_holds_a_bus_pin);
    RUN_TEST(test_claim_succeeds_when_no_input_is_in_the_way);
    RUN_TEST(test_reclaim_by_the_same_owner_still_succeeds);
    RUN_TEST(test_absent_pins_are_not_an_input_conflict);
    RUN_TEST(test_registry_owner_is_still_a_conflict);
    RUN_TEST(test_unregistered_probe_falls_back_to_the_registry);

    RUN_TEST(test_i2c_sensor_declares_an_i2c_need);
    RUN_TEST(test_thermocouple_declares_an_spi_need);
    RUN_TEST(test_analog_and_digital_sensors_need_no_bus);
    RUN_TEST(test_unknown_sensor_hash_needs_no_bus);
    RUN_TEST(test_display_is_one_more_voter);

    RUN_TEST(test_stored_i2c_sensor_asks_for_the_i2c_bus);
    RUN_TEST(test_stored_thermocouple_asks_for_the_spi_bus);
    RUN_TEST(test_record_order_does_not_change_the_answer);
    RUN_TEST(test_both_buses_can_be_needed_at_once);
    RUN_TEST(test_analog_only_config_asks_for_nothing);
    RUN_TEST(test_blank_eeprom_asks_for_nothing);
    RUN_TEST(test_foreign_store_version_asks_for_nothing);
    RUN_TEST(test_impossible_record_count_asks_for_nothing);

    RUN_TEST(test_stored_input_is_seen_before_inputs_table_is_live);
    RUN_TEST(test_bus_claim_refused_against_a_stored_input_before_load);
    RUN_TEST(test_boot_window_probe_does_not_misread_the_free_slot_sentinel);

    RUN_TEST(test_loadInputConfig_does_not_self_conflict_its_own_records);
    RUN_TEST(test_loadInputConfig_does_not_self_conflict_multiple_records);
    RUN_TEST(test_loadInputConfig_still_catches_a_genuine_duplicate_pin);
    return UNITY_END();
}
