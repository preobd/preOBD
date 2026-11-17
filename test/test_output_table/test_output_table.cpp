// Native unit test: the stored output table and its admission rules
// (#612 phase 2b-1; src/outputs/table/output_table.cpp, output_admission.cpp).
//
// What a unit depends on: ADD takes the lowest free id and an id means one
// output until DEL frees it; a record from EEPROM or JSON that this board
// can't run is refused; the defaults are OBD (off) on the wired CAN bus and
// ELM327 on BLE; and one format per stream, with the console's streams
// closed to RealDash/ELM327.
//
// The native profile builds ELM327 (serial and BLE), RealDash and serial CSV,
// not CAN or SD, and has no hardware serial ports (bus_defaults.h), so USB is
// the stream these tests use.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/outputs/table/output_table.cpp"
#include "../../src/outputs/table/output_admission.cpp"
#include "../../src/outputs/formats/can_layout_ids.cpp"

SystemConfig systemConfig;

void setUp(void) {
    memset(&systemConfig, 0, sizeof(systemConfig));
    systemConfig.console.primary = OT_ESP32_BT;   // off the streams below unless a test moves it
    outputsClear();
}
void tearDown(void) {}

static void test_add_takes_the_lowest_free_id(void) {
    TEST_ASSERT_EQUAL_UINT8(0, outputCreate(FMT_CSV, OT_USB, true));
    TEST_ASSERT_EQUAL_UINT8(1, outputCreate(FMT_REALDASH, OT_USB, false));
    TEST_ASSERT_EQUAL_UINT8(2, outputCreate(FMT_ELM327, OT_BLE, true));
    outputErase(1);
    TEST_ASSERT_FALSE(outputExists(1));
    TEST_ASSERT_EQUAL_UINT8(1, outputFreeId());
    TEST_ASSERT_EQUAL_UINT8(1, outputCreate(FMT_CSV, OT_USB, false));   // id 1 reused
    TEST_ASSERT_EQUAL_UINT8(3, outputCount());
    TEST_ASSERT_EQUAL_HEX8(0x07, outputsExistingMask());
    TEST_ASSERT_EQUAL(FMT_ELM327, outputFormatOf(2));
    TEST_ASSERT_EQUAL_UINT8(OT_BLE, outputTransportOf(2));
    TEST_ASSERT_EQUAL_UINT8(2, outputFind(FMT_ELM327, OTK_BLE));
    TEST_ASSERT_EQUAL_UINT8(OUT_ID_NONE, outputFind(FMT_ELM327, OTK_STREAM));
}

static void test_a_full_table_takes_nothing(void) {
    for (uint8_t i = 0; i < MAX_OUTPUTS; i++) TEST_ASSERT_EQUAL_UINT8(i, outputCreate(FMT_CSV, OT_USB, false));
    TEST_ASSERT_EQUAL_UINT8(OUT_ID_NONE, outputCreate(FMT_CSV, OT_USB, false));
    TEST_ASSERT_NOT_NULL(outputAddLimitReason(FMT_REALDASH, OT_USB, outputRecords));
}

static void test_new_records_get_their_format_defaults(void) {
    const uint8_t rd = outputCreate(FMT_REALDASH, OT_USB, true);
    const uint8_t elm = outputCreate(FMT_ELM327, OT_BLE, true);
    TEST_ASSERT_EQUAL_UINT16(REALDASH_INTERVAL_MS, outputRecord(rd)->interval);
    TEST_ASSERT_EQUAL_UINT16(0, outputRecord(elm)->interval);   // answers requests: no interval
    TEST_ASSERT_TRUE(outputRecordValid(*outputRecord(rd), rd));
    TEST_ASSERT_TRUE(outputRecordValid(*outputRecord(elm), elm));
}

static void test_defaults_are_elm327_on_ble_only_here(void) {
    outputCreate(FMT_CSV, OT_USB, true);
    outputsSeedDefaults();   // no CAN_WIRED_BUS in the native build: no OBD record
    TEST_ASSERT_EQUAL_UINT8(1, outputCount());
    TEST_ASSERT_EQUAL(FMT_ELM327, outputFormatOf(0));
    TEST_ASSERT_EQUAL_UINT8(OT_BLE, outputTransportOf(0));
    TEST_ASSERT_TRUE(outputIsEnabled(0));
}

// Records from EEPROM or JSON are checked one by one: a valid checksum over
// all-zero bytes proves nothing (#612 review)
static void test_stored_records_this_board_cant_run_are_invalid(void) {
    OutputRecord r = {};
    TEST_ASSERT_TRUE(outputRecordValid(r, 0));                       // a free slot
    r = {2, FMT_CSV, OT_USB, 1, 0, 0, 500};
    TEST_ASSERT_TRUE(outputRecordValid(r, 2));
    TEST_ASSERT_FALSE(outputRecordValid(r, 3));                      // id isn't its slot
    r.interval = 10;
    TEST_ASSERT_FALSE(outputRecordValid(r, 2));                      // under CSV's 100 ms floor
    r = {0, FMT_OBD, OT_CAN1, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(outputRecordValid(r, 0));                      // OBD not built here
    r = {0, FMT_REALDASH, OT_SD, 1, 0, 0, 100};
    TEST_ASSERT_FALSE(outputRecordValid(r, 0));                      // RealDash on SD
    r = {0, FMT_ELM327, OT_BLE, 1, 0, 0, 7};
    TEST_ASSERT_FALSE(outputRecordValid(r, 0));                      // an answer format with an interval
    r = {0, 99, OT_USB, 1, 0, 0, 0};
    TEST_ASSERT_FALSE(outputRecordValid(r, 0));                      // no such format
}

static void test_formats_take_their_kinds_of_transport(void) {
    TEST_ASSERT_TRUE(outputTransportAllowed(FMT_CSV, OT_USB));
    TEST_ASSERT_FALSE(outputTransportAllowed(FMT_CSV, OT_SD));       // no SD in this build
    TEST_ASSERT_TRUE(outputTransportAllowed(FMT_REALDASH, OT_USB));
    TEST_ASSERT_TRUE(outputTransportAllowed(FMT_REALDASH, OT_BLE));  // the nRF data channel (2c)
    TEST_ASSERT_TRUE(outputTransportAllowed(FMT_ELM327, OT_BLE));
    TEST_ASSERT_FALSE(outputTransportAllowed(FMT_ELM327, OT_USB));   // USB is the console's
    TEST_ASSERT_FALSE(outputTransportAllowed(FMT_CSV, OT_SERIAL1));  // no serial ports here
    TEST_ASSERT_EQUAL(OTK_STREAM, outputTransportKind(OT_SERIAL1 + 7));
    TEST_ASSERT_EQUAL(OTK_CAN, outputTransportKind(OT_CAN1 + 2));
    TEST_ASSERT_EQUAL(OTK_NONE, outputTransportKind(0x77));
    TEST_ASSERT_EQUAL_UINT8(5, outputTransportSerialPort(OT_SERIAL1 + 4));
    TEST_ASSERT_EQUAL_UINT8(1, outputTransportCanBus(OT_CAN1 + 1));
}

static void test_intervals_per_format(void) {
    TEST_ASSERT_EQUAL_UINT16(50, outputIntervalMin(FMT_REALDASH, OT_USB));
    TEST_ASSERT_EQUAL_UINT16(100, outputIntervalMin(FMT_REALDASH, OT_BLE)); // shares the nRF link (2c)
    TEST_ASSERT_FALSE(outputIntervalValid(FMT_REALDASH, OT_BLE, 99));
    TEST_ASSERT_EQUAL_UINT16(100, outputIntervalMin(FMT_CSV, OT_USB));
    TEST_ASSERT_EQUAL_UINT16(1, outputIntervalMin(FMT_CSV, OT_SD));   // the SD log buffers its rows
    TEST_ASSERT_TRUE(outputIntervalValid(FMT_CSV, OT_USB, 60000));
    TEST_ASSERT_FALSE(outputIntervalValid(FMT_CSV, OT_USB, 60001));
    TEST_ASSERT_FALSE(outputIntervalValid(FMT_ELM327, OT_BLE, 100));
}

static void test_names_round_trip(void) {
    char b[12];
    const uint8_t ts[] = {OT_USB, OT_SERIAL1 + 1, OT_ESP32_BT, OT_CAN1 + 2, OT_SD, OT_BLE, OT_NONE};
    for (uint8_t t : ts) {
        outputTransportName(t, b, sizeof(b));
        uint8_t back = 0xFF;
        TEST_ASSERT_TRUE_MESSAGE(parseOutputTransport(b, &back), b);
        TEST_ASSERT_EQUAL_UINT8(t, back);
    }
    uint8_t x;
    TEST_ASSERT_FALSE(parseOutputTransport("SERIAL9", &x));
    TEST_ASSERT_FALSE(parseOutputTransport("CAN4", &x));
    TEST_ASSERT_TRUE(parseOutputTransport("serial2", &x));            // case-insensitive
    TEST_ASSERT_EQUAL(FMT_REALDASH, parseOutputFormat("realdash"));
    TEST_ASSERT_EQUAL(FMT_NONE, parseOutputFormat("SD_Log"));
    TEST_ASSERT_EQUAL_STRING("ELM327", outputFormatName(FMT_ELM327));
}

// ---- admission (§14.4) ----

static void test_one_format_per_stream(void) {
    TEST_ASSERT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_REALDASH, OT_USB, systemConfig, outputRecords));
    const uint8_t rd = outputCreate(FMT_REALDASH, OT_USB, true);
    // A second output on the same stream, of any format, while the first is on
    TEST_ASSERT_NOT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_CSV, OT_USB, systemConfig, outputRecords));
    // ...but the output itself re-admits on its own stream (ENABLE, TRANSPORT)
    TEST_ASSERT_NULL(outputAdmitReason(rd, FMT_REALDASH, OT_USB, systemConfig, outputRecords));
    // Disabled outputs coexist
    outputRecord(rd)->enabled = 0;
    TEST_ASSERT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_CSV, OT_USB, systemConfig, outputRecords));
}

static void test_csv_shares_with_the_console_realdash_does_not(void) {
    systemConfig.console.primary = OT_USB;
    TEST_ASSERT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_CSV, OT_USB, systemConfig, outputRecords));
    TEST_ASSERT_NOT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_REALDASH, OT_USB, systemConfig, outputRecords));
    systemConfig.console.primary = OT_ESP32_BT;
    systemConfig.log.secondary = OT_USB;   // the log counts too
    TEST_ASSERT_NOT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_REALDASH, OT_USB, systemConfig, outputRecords));
}

// CONSOLE/LOG TRANSPORT and BUS SERIAL ask who needs a stream
static void test_who_holds_a_stream(void) {
    const uint8_t csv = outputCreate(FMT_CSV, OT_USB, true);
    TEST_ASSERT_EQUAL_UINT8(OUT_ID_NONE, outputOnStream(OT_USB, false, outputRecords));   // CSV shares
    TEST_ASSERT_EQUAL_UINT8(csv, outputOnStream(OT_USB, true, outputRecords));
    outputErase(csv);
    const uint8_t rd = outputCreate(FMT_REALDASH, OT_USB, true);
    TEST_ASSERT_EQUAL_UINT8(rd, outputOnStream(OT_USB, false, outputRecords));
    TEST_ASSERT_EQUAL_UINT8(OUT_ID_NONE, outputOnStream(OT_BLE, true, outputRecords));   // not a stream
}

static void test_one_of_each_singleton(void) {
    TEST_ASSERT_NULL(outputAddLimitReason(FMT_ELM327, OT_BLE, outputRecords));
    outputCreate(FMT_ELM327, OT_BLE, false);
    TEST_ASSERT_NOT_NULL(outputAddLimitReason(FMT_ELM327, OT_BLE, outputRecords));
    // ...one on BLE doesn't block one on a serial port
    TEST_ASSERT_NULL(outputAddLimitReason(FMT_ELM327, OT_SERIAL1, outputRecords));
    // Push formats: several
    outputCreate(FMT_CSV, OT_USB, false);
    TEST_ASSERT_NULL(outputAddLimitReason(FMT_CSV, OT_USB, outputRecords));
}

// BLE is one data channel (the phone's FFE1): one enabled output on it, both
// directions; disabled ones coexist; one RealDash on BLE (#612 2c)
static void test_ble_carries_one_data_output(void) {
    const uint8_t elm = outputCreate(FMT_ELM327, OT_BLE, true);
    TEST_ASSERT_NOT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_REALDASH, OT_BLE, systemConfig, outputRecords));
    TEST_ASSERT_NULL(outputAdmitReason(elm, FMT_ELM327, OT_BLE, systemConfig, outputRecords));   // itself
    outputRecord(elm)->enabled = 0;
    TEST_ASSERT_NULL(outputAdmitReason(OUT_ID_NONE, FMT_REALDASH, OT_BLE, systemConfig, outputRecords));
    const uint8_t rd = outputCreate(FMT_REALDASH, OT_BLE, true);
    TEST_ASSERT_NOT_EQUAL(OUT_ID_NONE, rd);
    TEST_ASSERT_NOT_NULL(outputAdmitReason(elm, FMT_ELM327, OT_BLE, systemConfig, outputRecords)); // re-enable refused
    TEST_ASSERT_NOT_NULL(outputAddLimitReason(FMT_REALDASH, OT_BLE, outputRecords));   // one RealDash on BLE
    TEST_ASSERT_NULL(outputAddLimitReason(FMT_REALDASH, OT_USB, outputRecords));       // streams: several
    // Not a stream: the console and the log don't count against it
    systemConfig.console.primary = OT_USB;
    TEST_ASSERT_NULL(outputAdmitReason(rd, FMT_REALDASH, OT_BLE, systemConfig, outputRecords));
}

// One job per CAN bus (#632): an output on the bus CAN input reads is refused
// (outputAdmitReason asks this; OBD itself isn't built natively). One-bus
// boards too: both on bus 0. CAN input off, another bus, or a non-CAN
// transport: no conflict.
static void test_a_can_output_cannot_share_can_inputs_bus(void) {
    SystemConfig cfg = {};
    cfg.buses.canInputBus = 0;                                        // CAN input on CAN1
    TEST_ASSERT_TRUE(outputOnCanInputBus(OT_CAN1, cfg));
    TEST_ASSERT_FALSE(outputOnCanInputBus(OT_CAN1 + 1, cfg));         // CAN2
    TEST_ASSERT_FALSE(outputOnCanInputBus(OT_USB, cfg));              // not CAN
    cfg.buses.canInputBus = 0xFF;                                     // CAN input off
    TEST_ASSERT_FALSE(outputOnCanInputBus(OT_CAN1, cfg));
    cfg.buses.canInputBus = 1;                                        // CAN input on CAN2
    TEST_ASSERT_TRUE(outputOnCanInputBus(OT_CAN1 + 1, cfg));
    TEST_ASSERT_FALSE(outputOnCanInputBus(OT_CAN1, cfg));
}

// PREOBD owns 0x500-0x508 and OBD 0x7DF/0x7E0/0x7E8: OBD and PREOBD share a
// bus, two PREOBD do not (the same function at every door)
static void test_can_id_overlap(void) {
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_OBD, FMT_PREOBD));
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_PREOBD, FMT_OBD));
    TEST_ASSERT_TRUE(outputCanIdsOverlap(FMT_PREOBD, FMT_PREOBD));
    TEST_ASSERT_TRUE(outputCanIdsOverlap(FMT_OBD, FMT_OBD));
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_CSV, FMT_PREOBD));
    TEST_ASSERT_TRUE(outputFormatSendsCan(FMT_PREOBD));
    TEST_ASSERT_FALSE(outputFormatSendsCan(FMT_CSV));
    // HALTECH owns 0x360-0x6F8 (11 IDs): beside OBD and PREOBD, never beside another HALTECH
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_HALTECH, FMT_PREOBD));
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_PREOBD, FMT_HALTECH));
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_HALTECH, FMT_OBD));
    TEST_ASSERT_FALSE(outputCanIdsOverlap(FMT_OBD, FMT_HALTECH));
    TEST_ASSERT_TRUE(outputCanIdsOverlap(FMT_HALTECH, FMT_HALTECH));
    TEST_ASSERT_TRUE(outputFormatSendsCan(FMT_HALTECH));
}

// The IDs come from one table: the spec's, in frame order
static void test_haltech_ids(void) {
    static const uint16_t spec[] = {0x360, 0x361, 0x36B, 0x370, 0x372, 0x373, 0x374, 0x376, 0x3E0, 0x3E1, 0x6F8};
    TEST_ASSERT_EQUAL_UINT8(sizeof(spec) / sizeof(spec[0]), layoutFormatFrames(FMT_HALTECH));
    for (uint8_t f = 0; f < HALTECH_FRAMES; f++) {
        TEST_ASSERT_EQUAL_HEX16(spec[f], layoutFormatFrameId(FMT_HALTECH, f));
        TEST_ASSERT_TRUE(layoutFormatOwnsCanId(FMT_HALTECH, spec[f]));
    }
    TEST_ASSERT_FALSE(layoutFormatOwnsCanId(FMT_HALTECH, 0x362));   // a Haltech ID preOBD doesn't send
    TEST_ASSERT_EQUAL_UINT8(9, layoutFormatFrames(FMT_PREOBD));
    TEST_ASSERT_EQUAL_HEX16(0x508, layoutFormatFrameId(FMT_PREOBD, 8));
    TEST_ASSERT_EQUAL_UINT8(0, layoutFormatFrames(FMT_OBD));
}

static void test_haltech_format_table(void) {
    TEST_ASSERT_EQUAL(FMT_HALTECH, parseOutputFormat("haltech"));
    TEST_ASSERT_EQUAL_STRING("HALTECH", outputFormatName(FMT_HALTECH));
    TEST_ASSERT_TRUE(outputFormatBuilt(FMT_HALTECH));
    TEST_ASSERT_TRUE(outputFormatPaced(FMT_HALTECH));
    TEST_ASSERT_TRUE(outputFormatIsLayout(FMT_HALTECH));
    TEST_ASSERT_FALSE(outputFormatIsLayout(FMT_OBD));
    TEST_ASSERT_EQUAL_UINT16(20, outputIntervalDefault(FMT_HALTECH, OT_CAN1));
    TEST_ASSERT_EQUAL_UINT16(20, outputIntervalMin(FMT_HALTECH, OT_CAN1));
    TEST_ASSERT_EQUAL_UINT32(1000000UL, outputFormatCanBaud(FMT_HALTECH));
    TEST_ASSERT_EQUAL_UINT32(0, outputFormatCanBaud(FMT_PREOBD));
    TEST_ASSERT_FALSE(outputTransportAllowed(FMT_HALTECH, OT_USB));   // CAN only (native has no CAN bus)
    // Stored as a number: appended after PREOBD, never renumbered
    TEST_ASSERT_EQUAL_UINT8(6, (uint8_t)FMT_HALTECH);
}

// §14.7 item 3: Haltech's protocol is 1 Mbit/s, so HALTECH is refused on a bus
// at another rate (outputAdmitReason asks this; the native build has no CAN bus
// to reach it through, the bench does)
static void test_haltech_needs_a_1mbit_bus(void) {
    systemConfig.buses.can[0].baudIndex = CAN_BAUD_500K;
    systemConfig.buses.can[1].baudIndex = CAN_BAUD_1M;
    TEST_ASSERT_FALSE(outputCanRateFits(FMT_HALTECH, OT_CAN1, systemConfig));
    TEST_ASSERT_TRUE(outputCanRateFits(FMT_HALTECH, OT_CAN1 + 1, systemConfig));
    TEST_ASSERT_TRUE(outputCanRateFits(FMT_PREOBD, OT_CAN1, systemConfig));   // any rate
    TEST_ASSERT_TRUE(outputCanRateFits(FMT_OBD, OT_CAN1, systemConfig));
    TEST_ASSERT_TRUE(outputCanRateFits(FMT_HALTECH, OT_USB, systemConfig));   // not CAN: not this rule's
    for (uint8_t b = CAN_BAUD_125K; b < NUM_CAN_BAUDS; b++) {
        systemConfig.buses.can[2].baudIndex = b;
        TEST_ASSERT_EQUAL(b == CAN_BAUD_1M, outputCanRateFits(FMT_HALTECH, OT_CAN1 + 2, systemConfig));
    }
}

static void test_preobd_format_table(void) {
    TEST_ASSERT_EQUAL(FMT_PREOBD, parseOutputFormat("preobd"));
    TEST_ASSERT_EQUAL_STRING("PREOBD", outputFormatName(FMT_PREOBD));
    TEST_ASSERT_TRUE(outputFormatBuilt(FMT_PREOBD));
    TEST_ASSERT_TRUE(outputFormatPaced(FMT_PREOBD));
    TEST_ASSERT_EQUAL_UINT16(100, outputIntervalDefault(FMT_PREOBD, OT_CAN1));
    TEST_ASSERT_EQUAL_UINT16(20, outputIntervalMin(FMT_PREOBD, OT_CAN1));
    TEST_ASSERT_TRUE(outputIntervalValid(FMT_PREOBD, OT_CAN1, 20));
    TEST_ASSERT_FALSE(outputIntervalValid(FMT_PREOBD, OT_CAN1, 19));
    TEST_ASSERT_TRUE(outputIntervalValid(FMT_PREOBD, OT_CAN1, 60000));
    TEST_ASSERT_FALSE(outputIntervalValid(FMT_PREOBD, OT_CAN1, 60001));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_can_output_cannot_share_can_inputs_bus);
    RUN_TEST(test_can_id_overlap);
    RUN_TEST(test_haltech_ids);
    RUN_TEST(test_haltech_format_table);
    RUN_TEST(test_haltech_needs_a_1mbit_bus);
    RUN_TEST(test_preobd_format_table);
    RUN_TEST(test_add_takes_the_lowest_free_id);
    RUN_TEST(test_a_full_table_takes_nothing);
    RUN_TEST(test_new_records_get_their_format_defaults);
    RUN_TEST(test_defaults_are_elm327_on_ble_only_here);
    RUN_TEST(test_stored_records_this_board_cant_run_are_invalid);
    RUN_TEST(test_formats_take_their_kinds_of_transport);
    RUN_TEST(test_intervals_per_format);
    RUN_TEST(test_names_round_trip);
    RUN_TEST(test_one_format_per_stream);
    RUN_TEST(test_csv_shares_with_the_console_realdash_does_not);
    RUN_TEST(test_who_holds_a_stream);
    RUN_TEST(test_one_of_each_singleton);
    RUN_TEST(test_ble_carries_one_data_output);
    return UNITY_END();
}
