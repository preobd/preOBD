// Native unit test: nRF status composer (nrf_firmware/src/status_compose.cpp).
//
// The host sends the 8-byte extension (0x07) just before each 32-byte status
// (0x03); the nRF joins them into the 40-byte characteristic value (#455).
// The extension is ONE-SHOT, so an older host that never sends 0x07 — or one
// flashed back to an old build while this nRF stays up — degrades to 32 B on
// the next snapshot instead of pinning a stale extension.
#include <unity.h>
#include <stdint.h>
#include <string.h>

#include "../../nrf_firmware/src/status_compose.cpp"
#include "../../src/comms/ble_gatt_defs.h"

static_assert(kStatusComposeBaseSize == PREOBD_STATUS_SIZE, "base size");
static_assert(kStatusComposeExtMinSize == PREOBD_STATUS_EXT_SIZE, "ext size");
static_assert(kStatusComposeMaxSize == PREOBD_STATUS_MAX_SIZE, "max size");

static StatusComposer c;
static uint8_t status[32];
static uint8_t ext[8];
static uint8_t out[kStatusComposeMaxSize];

void setUp(void) {
    statusComposeInit(c);
    for (int i = 0; i < 32; i++) status[i] = (uint8_t)i;
    for (int i = 0; i < 8; i++)  ext[i] = (uint8_t)(0xA0 + i);
    memset(out, 0, sizeof(out));
}
void tearDown(void) {}

void test_status_alone_is_32(void) {
    TEST_ASSERT_EQUAL(32, statusComposeOnStatus(c, status, 32, out));
    TEST_ASSERT_EQUAL_MEMORY(status, out, 32);
}

void test_ext_then_status_is_40(void) {
    statusComposeOnExt(c, ext, 8);
    TEST_ASSERT_EQUAL(40, statusComposeOnStatus(c, status, 32, out));
    TEST_ASSERT_EQUAL_MEMORY(status, out, 32);
    TEST_ASSERT_EQUAL_MEMORY(ext, out + 32, 8);
}

void test_ext_is_one_shot(void) {
    statusComposeOnExt(c, ext, 8);
    TEST_ASSERT_EQUAL(40, statusComposeOnStatus(c, status, 32, out));
    TEST_ASSERT_EQUAL(32, statusComposeOnStatus(c, status, 32, out));  // old host now
}

void test_later_ext_replaces_held(void) {
    statusComposeOnExt(c, ext, 8);
    uint8_t ext2[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    statusComposeOnExt(c, ext2, 8);
    TEST_ASSERT_EQUAL(40, statusComposeOnStatus(c, status, 32, out));
    TEST_ASSERT_EQUAL_MEMORY(ext2, out + 32, 8);
}

void test_bad_ext_discards_held(void) {
    statusComposeOnExt(c, ext, 8);
    statusComposeOnExt(c, ext, 7);                 // shorter than layout 1
    TEST_ASSERT_EQUAL(32, statusComposeOnStatus(c, status, 32, out));
    uint8_t big[kStatusComposeExtMaxSize + 1] = {};
    statusComposeOnExt(c, ext, 8);
    statusComposeOnExt(c, big, sizeof(big));       // past the ceiling
    TEST_ASSERT_EQUAL(32, statusComposeOnStatus(c, status, 32, out));
}

// A future host that APPENDS fields must pass through this nRF unread — the
// whole point of the one-shot extension is that neither side needs the
// other's version.
void test_appended_ext_passes_through(void) {
    uint8_t ext12[12];
    for (int i = 0; i < 12; i++) ext12[i] = (uint8_t)(0xB0 + i);
    statusComposeOnExt(c, ext12, 12);
    TEST_ASSERT_EQUAL(44, statusComposeOnStatus(c, status, 32, out));
    TEST_ASSERT_EQUAL_MEMORY(ext12, out + 32, 12);
    uint8_t extMax[kStatusComposeExtMaxSize] = {0x5A};
    statusComposeOnExt(c, extMax, sizeof(extMax));
    TEST_ASSERT_EQUAL(kStatusComposeMaxSize, statusComposeOnStatus(c, status, 32, out));
}

void test_bad_status_dropped_and_consumes_ext(void) {
    statusComposeOnExt(c, ext, 8);
    TEST_ASSERT_EQUAL(0, statusComposeOnStatus(c, status, 31, out));
    TEST_ASSERT_EQUAL(32, statusComposeOnStatus(c, status, 32, out));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_alone_is_32);
    RUN_TEST(test_ext_then_status_is_40);
    RUN_TEST(test_ext_is_one_shot);
    RUN_TEST(test_later_ext_replaces_held);
    RUN_TEST(test_bad_ext_discards_held);
    RUN_TEST(test_appended_ext_passes_through);
    RUN_TEST(test_bad_status_dropped_and_consumes_ext);
    return UNITY_END();
}
