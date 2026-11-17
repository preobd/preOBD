// Native unit test: the outputs table (#612 phase 1, src/outputs/table/output_table.cpp).
//
// Phase 1's table is a view over systemConfig: one output per format at a
// fixed id, its on/off and interval in the legacy outputEnabled[] /
// outputInterval[] slots, the deleted ones in router.outputsAbsent. The
// properties pinned here are the ones a flashed unit depends on: ids 0-3 are
// the old per-input bits, an older image ("nothing deleted") shows every
// output this build has, and an old mask upgrades without losing a route.
//
// The native profile builds ELM327, RealDash and serial CSV, not CAN or SD.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/outputs/table/output_table.cpp"

SystemConfig systemConfig;

void setUp(void) {
    memset(&systemConfig, 0, sizeof(systemConfig));   // an older image: nothing deleted
}
void tearDown(void) {}

static void test_ids_are_the_old_output_bits(void) {
    TEST_ASSERT_EQUAL(OUTPUT_CAN,      outputLegacySlot(OUT_ID_OBD));
    TEST_ASSERT_EQUAL(OUTPUT_REALDASH, outputLegacySlot(OUT_ID_REALDASH));
    TEST_ASSERT_EQUAL(OUTPUT_SERIAL,   outputLegacySlot(OUT_ID_CSV));
    TEST_ASSERT_EQUAL(OUTPUT_SD,       outputLegacySlot(OUT_ID_SDLOG));
    TEST_ASSERT_EQUAL(OUTPUT_ELM327,   outputLegacySlot(OUT_ID_ELM327));
    TEST_ASSERT_EQUAL(FMT_CSV, outputFormatOf(OUT_ID_CSV));
    TEST_ASSERT_EQUAL(FMT_CSV, outputFormatOf(OUT_ID_SDLOG));
    TEST_ASSERT_EQUAL(OTK_SD,  outputKindOf(OUT_ID_SDLOG));
    TEST_ASSERT_EQUAL(0xFF, outputLegacySlot(NUM_OUTPUT_IDS));
}

static void test_an_older_image_shows_every_built_output(void) {
    TEST_ASSERT_TRUE(outputExists(OUT_ID_REALDASH));
    TEST_ASSERT_TRUE(outputExists(OUT_ID_CSV));
    TEST_ASSERT_TRUE(outputExists(OUT_ID_ELM327));
    TEST_ASSERT_FALSE(outputExists(OUT_ID_OBD));     // INCLUDE_CAN is off here
    TEST_ASSERT_FALSE(outputExists(OUT_ID_SDLOG));   // and INCLUDE_SD_LOGGING
    TEST_ASSERT_EQUAL_UINT8(3, outputCount());
    TEST_ASSERT_EQUAL_HEX8((1u << OUT_ID_REALDASH) | (1u << OUT_ID_CSV) | (1u << OUT_ID_ELM327),
                           outputsExistingMask());
}

static void test_enabled_and_interval_read_the_legacy_slots(void) {
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 1;
    systemConfig.outputInterval[OUTPUT_SERIAL] = 500;
    TEST_ASSERT_TRUE(outputIsEnabled(OUT_ID_CSV));
    TEST_ASSERT_FALSE(outputIsEnabled(OUT_ID_REALDASH));
    TEST_ASSERT_EQUAL_UINT16(500, outputIntervalOf(OUT_ID_CSV));
    TEST_ASSERT_TRUE(outputTakesIntervalId(OUT_ID_CSV));
    TEST_ASSERT_FALSE(outputTakesIntervalId(OUT_ID_ELM327));   // answers requests
    TEST_ASSERT_FALSE(outputTakesIntervalId(OUT_ID_OBD));
    TEST_ASSERT_EQUAL_UINT16(50,  outputIntervalMinId(OUT_ID_REALDASH));
    TEST_ASSERT_EQUAL_UINT16(100, outputIntervalMinId(OUT_ID_CSV));
    TEST_ASSERT_EQUAL_UINT16(1,   outputIntervalMinId(OUT_ID_SDLOG));   // CSV on SD: its own floor
}

static void test_deleted_outputs_are_gone_and_come_back(void) {
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 1;
    outputMarkAbsent(OUT_ID_REALDASH, true);
    TEST_ASSERT_FALSE(outputExists(OUT_ID_REALDASH));
    TEST_ASSERT_FALSE(outputIsEnabled(OUT_ID_REALDASH));   // enabled byte alone isn't enough
    TEST_ASSERT_EQUAL_UINT8(2, outputCount());
    TEST_ASSERT_FALSE(outputsExistingMask() & (1u << OUT_ID_REALDASH));
    outputMarkAbsent(OUT_ID_REALDASH, false);
    TEST_ASSERT_TRUE(outputExists(OUT_ID_REALDASH));
    TEST_ASSERT_EQUAL_HEX8(0, systemConfig.router.outputsAbsent);
    outputMarkAbsent(NUM_OUTPUT_IDS, true);                  // out of range: ignored
    TEST_ASSERT_EQUAL_HEX8(0, systemConfig.router.outputsAbsent);
}

static void test_format_lookup_and_names(void) {
    TEST_ASSERT_EQUAL(OUT_ID_CSV,      outputIdFor(FMT_CSV, OTK_STREAM));
    TEST_ASSERT_EQUAL(OUT_ID_SDLOG,    outputIdFor(FMT_CSV, OTK_SD));
    TEST_ASSERT_EQUAL(OUT_ID_REALDASH, outputIdFor(FMT_REALDASH, OTK_STREAM));
    TEST_ASSERT_EQUAL(OUT_ID_OBD,      outputIdFor(FMT_OBD, OTK_STREAM));   // kind only splits CSV
    TEST_ASSERT_EQUAL(OUT_ID_NONE,     outputIdFor(FMT_NONE, OTK_STREAM));
    TEST_ASSERT_EQUAL(FMT_REALDASH, parseOutputFormat("realdash"));
    TEST_ASSERT_EQUAL(FMT_ELM327,   parseOutputFormat("ELM327"));
    TEST_ASSERT_EQUAL(FMT_NONE,     parseOutputFormat("SD_Log"));            // old module names are gone
    TEST_ASSERT_EQUAL(FMT_NONE,     parseOutputFormat(nullptr));
    TEST_ASSERT_EQUAL_STRING("OBD", outputFormatName(FMT_OBD));
}

static void test_old_masks_keep_every_route(void) {
    TEST_ASSERT_EQUAL_HEX8(0x1F, routeMaskFromLegacy(0x0F));   // all four, plus ELM327 from CAN
    TEST_ASSERT_EQUAL_HEX8(0x0E, routeMaskFromLegacy(0x0E));   // CAN off: ELM327 off too
    TEST_ASSERT_EQUAL_HEX8(0x11, routeMaskFromLegacy(0x01));
    TEST_ASSERT_EQUAL_HEX8(0x00, routeMaskFromLegacy(0xF0));   // bits an older build never set
}

// An older build ignores outputsAbsent: switching a deleted output back on
// there and saving must bring it back, not leave it on and deleted.
static void test_an_output_switched_on_by_an_older_build_is_present(void) {
    outputMarkAbsent(OUT_ID_CSV, true);
    outputMarkAbsent(OUT_ID_REALDASH, true);
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 1;    // the older build's ENABLE
    outputsReconcileAbsent();
    TEST_ASSERT_TRUE(outputExists(OUT_ID_CSV));
    TEST_ASSERT_FALSE(outputExists(OUT_ID_REALDASH));  // still off: still deleted
}

static void test_transport_kinds_for_import(void) {
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_OBD, "CAN2"));
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_OBD, "NONE"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_OBD, "SD"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_OBD, "CAN4"));
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_CSV, "USB"));
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_REALDASH, "USB+SERIAL1"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_CSV, "SD"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_REALDASH, "CAN1"));
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_SDLOG, "SD"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_SDLOG, "USB"));
    TEST_ASSERT_TRUE(outputTransportKindOk(OUT_ID_ELM327, "SERIAL2"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_ELM327, "USB"));
    TEST_ASSERT_FALSE(outputTransportKindOk(OUT_ID_ELM327, nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ids_are_the_old_output_bits);
    RUN_TEST(test_an_older_image_shows_every_built_output);
    RUN_TEST(test_enabled_and_interval_read_the_legacy_slots);
    RUN_TEST(test_deleted_outputs_are_gone_and_come_back);
    RUN_TEST(test_format_lookup_and_names);
    RUN_TEST(test_old_masks_keep_every_route);
    RUN_TEST(test_an_output_switched_on_by_an_older_build_is_present);
    RUN_TEST(test_transport_kinds_for_import);
    return UNITY_END();
}
