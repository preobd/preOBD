// Native unit test: LCD input selection and paging (src/outputs/display/lcd_layout.h, #480).
#include <unity.h>

#include "../../src/outputs/display/lcd_layout.h"

void setUp(void) {}
void tearDown(void) {}

static void test_hidden_and_unconfigured_inputs_take_no_cell(void) {
    TEST_ASSERT_TRUE(lcdShowsInput(true, true, true, false));
    TEST_ASSERT_FALSE(lcdShowsInput(true, true, false, false));   // DISPLAY DISABLE
    TEST_ASSERT_FALSE(lcdShowsInput(false, true, true, false));   // no input on the slot
    TEST_ASSERT_FALSE(lcdShowsInput(true, false, true, false));   // RUN: disabled hidden
    TEST_ASSERT_TRUE(lcdShowsInput(true, false, true, true));     // CONFIG: shown as CFG
    TEST_ASSERT_FALSE(lcdShowsInput(true, false, false, true));   // hidden in CONFIG too
}

static void test_eight_or_fewer_is_one_static_page(void) {
    LcdPage p = lcdPageAt(8, 8, 123456, 4000);
    TEST_ASSERT_EQUAL_UINT8(1, p.pages);
    TEST_ASSERT_EQUAL_UINT8(0, p.index);
    TEST_ASSERT_EQUAL_UINT8(0, p.start);
    TEST_ASSERT_EQUAL_UINT8(8, p.count);
    p = lcdPageAt(3, 8, 999999, 4000);
    TEST_ASSERT_EQUAL_UINT8(3, p.count);
}

// The 9th input used to land on row 4 (off a 4-row LCD) or overwrite cell 8.
static void test_nine_inputs_page_eight_then_one(void) {
    LcdPage p = lcdPageAt(9, 8, 0, 4000);
    TEST_ASSERT_EQUAL_UINT8(2, p.pages);
    TEST_ASSERT_EQUAL_UINT8(0, p.start);
    TEST_ASSERT_EQUAL_UINT8(8, p.count);
    p = lcdPageAt(9, 8, 4000, 4000);
    TEST_ASSERT_EQUAL_UINT8(1, p.index);
    TEST_ASSERT_EQUAL_UINT8(8, p.start);
    TEST_ASSERT_EQUAL_UINT8(1, p.count);
    p = lcdPageAt(9, 8, 8000, 4000);                               // wraps
    TEST_ASSERT_EQUAL_UINT8(0, p.index);
}

static void test_forty_inputs_five_pages(void) {
    LcdPage p = lcdPageAt(40, 8, 4 * 4000 + 10, 4000);
    TEST_ASSERT_EQUAL_UINT8(5, p.pages);
    TEST_ASSERT_EQUAL_UINT8(4, p.index);
    TEST_ASSERT_EQUAL_UINT8(32, p.start);
    TEST_ASSERT_EQUAL_UINT8(8, p.count);
}

static void test_nothing_shown(void) {
    LcdPage p = lcdPageAt(0, 8, 5000, 4000);
    TEST_ASSERT_EQUAL_UINT8(0, p.pages);
    TEST_ASSERT_EQUAL_UINT8(0, p.count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_and_unconfigured_inputs_take_no_cell);
    RUN_TEST(test_eight_or_fewer_is_one_static_page);
    RUN_TEST(test_nine_inputs_page_eight_then_one);
    RUN_TEST(test_forty_inputs_five_pages);
    RUN_TEST(test_nothing_shown);
    return UNITY_END();
}
