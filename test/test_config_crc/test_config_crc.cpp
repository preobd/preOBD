// Native unit test: the SystemConfig EEPROM checksum and the load/save path
// that relies on it (#340).
//
// calculateChecksum() is a byte-wise XOR over the whole SystemConfig, skipping
// the checksum field itself. loadSystemConfig() is its only reader: a stored
// block is accepted only if magic, version AND checksum all match, and on any
// refusal the live `systemConfig` must be left untouched (initSystemConfig()
// then falls back to resetSystemConfig()).
//
// #340 was filed as blocked on stubbing system_config.cpp's dependencies
// (EEPROM, message_router, pin_registry, output_relay). That seam has since
// been opened by test_bus_wired_need_gate, which amalgamates the same TU set;
// this suite reuses it, plus units_registry.cpp for resetSystemConfig().
//
// Known property, deliberately NOT asserted as a feature: XOR is blind to an
// even number of flips of the same bit position (in any bytes). It catches any
// single-byte corruption, which is what the tests below pin down; anything
// stronger would be a format change and needs a SYSTEM_CONFIG_VERSION bump.

#include <unity.h>
#include <Arduino.h>

#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/drivers/bus_manager.cpp"
#include "../../src/storage/system_config.cpp"
// resetSystemConfig() resolves default unit names to indices.
#include "../../src/sensors/units_registry.cpp"
// saveSystemConfig() holds a WatchdogScope (#541); the HAL is a no-op stub here.
#include "../../src/core/watchdog.cpp"
#include "../../src/core/watchdog_policy.cpp"
#include "../../src/core/watchdog_trace.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols these TUs expect from elsewhere (same set as
// test_bus_wired_need_gate). system_config.cpp defines `systemConfig` itself.
// ---------------------------------------------------------------------------
MessageRouter router;
MessageAPI msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

static const size_t CKSUM_OFF = offsetof(SystemConfig, checksum);

// Independent reference: XOR of every byte but the checksum's.
static uint8_t referenceXor(const SystemConfig& cfg) {
    const uint8_t* b = (const uint8_t*)&cfg;
    uint8_t x = 0;
    for (size_t i = 0; i < sizeof(SystemConfig); i++)
        if (i != CKSUM_OFF) x ^= b[i];
    return x;
}

// Byte offset of the stored block inside the EEPROM part.
static int eepromByte(size_t i) { return (int)(SYSTEM_CONFIG_ADDRESS + i); }

void setUp(void) {
    EEPROM.clear();
    memset(&systemConfig, 0, sizeof(systemConfig));
    resetSystemConfig();
}
void tearDown(void) {}

// ===========================================================================
// 1. calculateChecksum() itself
// ===========================================================================

static void test_checksum_of_all_zero_config_is_zero(void) {
    SystemConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL_HEX8(0x00, calculateChecksum(&cfg));
}

static void test_checksum_matches_independent_xor_of_defaults(void) {
    TEST_ASSERT_EQUAL_HEX8(referenceXor(systemConfig), calculateChecksum(&systemConfig));
}

// The checksum field must be excluded, or a stored block could never verify
// (its own value would feed back into the sum).
static void test_checksum_field_is_excluded(void) {
    const uint8_t before = calculateChecksum(&systemConfig);
    systemConfig.checksum ^= 0xFF;
    TEST_ASSERT_EQUAL_HEX8(before, calculateChecksum(&systemConfig));
}

// Every byte other than the checksum contributes — including the first and
// last, so an off-by-one loop bound fails here. A single-byte change always
// changes an XOR, so this is also the "any single-byte corruption is caught"
// guarantee load relies on.
static void test_every_other_byte_contributes(void) {
    uint8_t* b = (uint8_t*)&systemConfig;
    const uint8_t base = calculateChecksum(&systemConfig);
    for (size_t i = 0; i < sizeof(SystemConfig); i++) {
        if (i == CKSUM_OFF) continue;
        b[i] ^= 0x5A;
        char what[32];
        snprintf(what, sizeof(what), "byte %u", (unsigned)i);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(base, calculateChecksum(&systemConfig), what);
        b[i] ^= 0x5A;
    }
}

static void test_reset_leaves_a_self_consistent_checksum(void) {
    TEST_ASSERT_EQUAL_HEX16(SYSTEM_CONFIG_MAGIC, systemConfig.magic);
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_CONFIG_VERSION, systemConfig.version);
    TEST_ASSERT_EQUAL_HEX8(calculateChecksum(&systemConfig), systemConfig.checksum);
}

// ===========================================================================
// 2. save -> load round trip
// ===========================================================================

static void test_save_then_load_round_trips(void) {
    systemConfig.seaLevelPressure = 1001.5f;
    systemConfig.sensorReadInterval = 123;
    systemConfig.checksum = 0;           // stale on purpose: save must recompute
    TEST_ASSERT_TRUE(saveSystemConfig(false));

    SystemConfig saved;
    memcpy(&saved, &systemConfig, sizeof(saved));
    TEST_ASSERT_EQUAL_HEX8(calculateChecksum(&saved), saved.checksum);

    memset(&systemConfig, 0, sizeof(systemConfig));
    TEST_ASSERT_TRUE(loadSystemConfig());
    TEST_ASSERT_EQUAL_MEMORY(&saved, &systemConfig, sizeof(SystemConfig));
}

// #608: an interval saved before the range check is repaired on load —
// clamped to the nearest valid value, 0 to the default — for every paced
// slot; the unpaced ones are left exactly as stored.
static void test_load_repairs_out_of_range_output_intervals(void) {
    systemConfig.outputInterval[OUTPUT_REALDASH] = 10;      // valid before #608
    systemConfig.outputInterval[OUTPUT_SERIAL]   = 0;       // sent every loop
    systemConfig.outputInterval[OUTPUT_SD]       = 65000;   // above the ceiling
    systemConfig.outputInterval[OUTPUT_ALARM]    = 7;       // no interval: untouched
    TEST_ASSERT_TRUE(saveSystemConfig(false));
    TEST_ASSERT_TRUE(loadSystemConfig());
    TEST_ASSERT_EQUAL_UINT16(50,                     systemConfig.outputInterval[OUTPUT_REALDASH]);
    TEST_ASSERT_EQUAL_UINT16(SERIAL_CSV_INTERVAL_MS, systemConfig.outputInterval[OUTPUT_SERIAL]);
    TEST_ASSERT_EQUAL_UINT16(OUTPUT_INTERVAL_MAX_MS, systemConfig.outputInterval[OUTPUT_SD]);
    TEST_ASSERT_EQUAL_UINT16(7,                      systemConfig.outputInterval[OUTPUT_ALARM]);
    resetSystemConfig();   // leave the shared global at factory values
}

static void test_save_writes_at_the_top_of_eeprom(void) {
    saveSystemConfig(false);
    SystemConfig raw;
    EEPROM.get((int)SYSTEM_CONFIG_ADDRESS, raw);
    TEST_ASSERT_EQUAL_MEMORY(&systemConfig, &raw, sizeof(SystemConfig));
    TEST_ASSERT_EQUAL(EEPROM_TOTAL_BYTES, SYSTEM_CONFIG_ADDRESS + sizeof(SystemConfig));
}

// ===========================================================================
// 3. load refuses a bad block and leaves the live config untouched
// ===========================================================================

// Sentinel so "untouched" is observable: differs from what's in EEPROM.
static void markLiveConfig(void) {
    systemConfig.sensorReadInterval = 0xBEEF;
}
static void assertLiveConfigUntouched(void) {
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, systemConfig.sensorReadInterval);
}

static void test_blank_eeprom_is_refused(void) {
    markLiveConfig();
    TEST_ASSERT_FALSE(loadSystemConfig());
    assertLiveConfigUntouched();
}

static void test_any_single_corrupted_byte_is_refused(void) {
    saveSystemConfig(false);
    markLiveConfig();
    for (size_t i = 0; i < sizeof(SystemConfig); i++) {
        const int a = eepromByte(i);
        const uint8_t orig = EEPROM.read(a);
        EEPROM.write(a, orig ^ 0x01);
        char what[32];
        snprintf(what, sizeof(what), "byte %u", (unsigned)i);
        TEST_ASSERT_FALSE_MESSAGE(loadSystemConfig(), what);
        assertLiveConfigUntouched();
        EEPROM.write(a, orig);
    }
    // Restored image still loads.
    TEST_ASSERT_TRUE(loadSystemConfig());
}

// A version bump must reset even when the old block's checksum is valid.
static void test_version_mismatch_is_refused_even_with_valid_checksum(void) {
    systemConfig.version = SYSTEM_CONFIG_VERSION - 1;
    saveSystemConfig(false);             // recomputes a checksum valid for v-1
    markLiveConfig();
    TEST_ASSERT_FALSE(loadSystemConfig());
    assertLiveConfigUntouched();
}

static void test_bad_magic_is_refused_even_with_valid_checksum(void) {
    systemConfig.magic = (uint16_t)~SYSTEM_CONFIG_MAGIC;
    saveSystemConfig(false);
    markLiveConfig();
    TEST_ASSERT_FALSE(loadSystemConfig());
    assertLiveConfigUntouched();
}

// initSystemConfig(): valid store -> loaded; invalid store -> defaults.
static void test_init_falls_back_to_defaults_on_corrupt_store(void) {
    SystemConfig defaults;
    memcpy(&defaults, &systemConfig, sizeof(defaults));

    systemConfig.sensorReadInterval = 777;
    saveSystemConfig(false);
    EEPROM.write(eepromByte(CKSUM_OFF), EEPROM.read(eepromByte(CKSUM_OFF)) ^ 0x80);

    markLiveConfig();
    initSystemConfig();
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &systemConfig, sizeof(SystemConfig));
}

static void test_init_loads_a_valid_store(void) {
    systemConfig.sensorReadInterval = 777;
    saveSystemConfig(false);

    markLiveConfig();
    initSystemConfig();
    TEST_ASSERT_EQUAL_UINT16(777, systemConfig.sensorReadInterval);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_of_all_zero_config_is_zero);
    RUN_TEST(test_checksum_matches_independent_xor_of_defaults);
    RUN_TEST(test_checksum_field_is_excluded);
    RUN_TEST(test_every_other_byte_contributes);
    RUN_TEST(test_reset_leaves_a_self_consistent_checksum);
    RUN_TEST(test_save_then_load_round_trips);
    RUN_TEST(test_load_repairs_out_of_range_output_intervals);
    RUN_TEST(test_save_writes_at_the_top_of_eeprom);
    RUN_TEST(test_blank_eeprom_is_refused);
    RUN_TEST(test_any_single_corrupted_byte_is_refused);
    RUN_TEST(test_version_mismatch_is_refused_even_with_valid_checksum);
    RUN_TEST(test_bad_magic_is_refused_even_with_valid_checksum);
    RUN_TEST(test_init_falls_back_to_defaults_on_corrupt_store);
    RUN_TEST(test_init_loads_a_valid_store);
    return UNITY_END();
}
