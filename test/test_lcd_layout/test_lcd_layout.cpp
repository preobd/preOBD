// Native unit test: LCD input selection, paging (#480) and cell fitting (#581),
// against the shipped src/outputs/display/lcd_layout.h.
#include <unity.h>
#include <string.h>

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

// #579: no icon column. The left cell keeps one blank gap column before the
// right cell; the right cell runs to the screen edge.
static void test_cell_geometry_without_icons(void) {
    TEST_ASSERT_EQUAL_UINT8(0, lcdCellCol(0));
    TEST_ASSERT_EQUAL_UINT8(3, lcdCellRow(3));
    TEST_ASSERT_EQUAL_UINT8(10, lcdCellCol(4));
    TEST_ASSERT_EQUAL_UINT8(0, lcdCellRow(4));
    TEST_ASSERT_EQUAL_UINT8(3, lcdCellRow(7));
    TEST_ASSERT_EQUAL_UINT8(9, lcdCellTextWidth(0));
    TEST_ASSERT_EQUAL_UINT8(10, lcdCellTextWidth(4));
    for (uint8_t line = 0; line < LCD_CELLS; line++) {
        TEST_ASSERT_TRUE(lcdCellCol(line) + lcdCellTextWidth(line) <= LCD_COLS);
        if (line < LCD_ROWS)   // left text never touches the right column
            TEST_ASSERT_TRUE(lcdCellCol(line) + lcdCellTextWidth(line) < lcdCellCol(line + LCD_ROWS));
    }
}

// ---- #581: a value that doesn't fit never shows as a partial number --------

#define LEFT 9    // lcdCellTextWidth(0..3)
#define RIGHT 10  // lcdCellTextWidth(4..7)

static void expectValue(uint8_t width, const char* label, float v, uint8_t dec, const char* want) {
    char out[LCD_CELL_BUF];
    memset(out, 'Z', sizeof(out));
    const uint8_t n = lcdFormatValueCell(out, width, label, v, dec);
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_UINT8(strlen(want), n);
    TEST_ASSERT_TRUE(n <= width);
}

static void expectText(uint8_t width, const char* label, const char* text, const char* want) {
    char out[LCD_CELL_BUF];
    const uint8_t n = lcdFormatTextCell(out, width, label, text);
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_UINT8(strlen(want), n);
}

static void test_number_format(void) {
    char b[12];
    uint8_t n;
    n = lcdFormatNumber(b, 10, 11.44f, 1);  b[n] = 0; TEST_ASSERT_EQUAL_STRING("11.4", b);
    n = lcdFormatNumber(b, 10, 11.46f, 0);  b[n] = 0; TEST_ASSERT_EQUAL_STRING("11", b);
    n = lcdFormatNumber(b, 10, 0.05f, 2);   b[n] = 0; TEST_ASSERT_EQUAL_STRING("0.05", b);
    n = lcdFormatNumber(b, 10, 29.92f, 2);  b[n] = 0; TEST_ASSERT_EQUAL_STRING("29.92", b);
    n = lcdFormatNumber(b, 10, -0.4f, 1);   b[n] = 0; TEST_ASSERT_EQUAL_STRING("-0.4", b);
    n = lcdFormatNumber(b, 10, -0.04f, 1);  b[n] = 0; TEST_ASSERT_EQUAL_STRING("0.0", b);  // no "-0.0"
    n = lcdFormatNumber(b, 10, -40.0f, 0);  b[n] = 0; TEST_ASSERT_EQUAL_STRING("-40", b);
    n = lcdFormatNumber(b, 10, 0.0f, 0);    b[n] = 0; TEST_ASSERT_EQUAL_STRING("0", b);
    n = lcdFormatNumber(b, 10, 1650.0f, 0); b[n] = 0; TEST_ASSERT_EQUAL_STRING("1650", b);
    TEST_ASSERT_EQUAL_UINT8(0, lcdFormatNumber(b, 3, 1650.0f, 0));      // too long: nothing
    TEST_ASSERT_EQUAL_UINT8(0, lcdFormatNumber(b, 10, 1e12f, 0));
    TEST_ASSERT_EQUAL_UINT8(0, lcdFormatNumber(b, 10, 1.0f / 0.0f, 0)); // inf
    TEST_ASSERT_EQUAL_UINT8(0, lcdFormatNumber(b, 10, 0.0f / 0.0f, 0)); // NaN
}

// Everything fits: unchanged from before.
static void test_short_labels_unchanged(void) {
    expectValue(LEFT,  "CHT", 185.0f, 0, "CHT:185");
    expectValue(RIGHT, "RPM", 2450.0f, 0, "RPM:2450");
    expectValue(LEFT,  "BST", 0.8f, 1, "BST:0.8");
    expectValue(LEFT,  "BAT", 13.8f, 1, "BAT:13.8");
    expectValue(RIGHT, "BARO", 29.92f, 2, "BARO:29.92");
    expectValue(LEFT,  "OAT", -12.0f, 0, "OAT:-12");
    expectValue(LEFT,  "", 1650.0f, 0, ":1650");
}

// The #581 report: a long label used to leave "EGT11:16" for 1650 (the value
// was cut on the right). The label gives way instead.
static void test_long_label_never_cuts_digits(void) {
    expectValue(LEFT,  "EGT1",   1650.0f, 0, "EGT1:1650");
    expectValue(LEFT,  "EGT11",  1650.0f, 0, "EGT1:1650");    // was "EGT11:165"
    expectValue(RIGHT, "EGT11",  1650.0f, 0, "EGT11:1650");
    expectValue(LEFT,  "RUNTOT", 12345.0f, 0, "RUN:12345");   // was "RUNTOT:12"
    expectValue(RIGHT, "RUNTOT", 12345.0f, 0, "RUNT:12345");
    expectValue(LEFT,  "ABCDEFG", 1650.0f, 0, "ABCD:1650");   // 7 = longest ABBR
    expectValue(RIGHT, "ABCDEFGH", 1650.0f, 0, "ABCDE:1650"); // any label length
    expectValue(LEFT,  "RPM",    12000.0f, 0, "RPM:12000");
    expectValue(LEFT,  "ENGRPM", 12000.0f, 0, "ENG:12000");
}

// Decimals go before any of the label does.
static void test_decimals_dropped_first(void) {
    expectValue(LEFT,  "BOOST", 11.4f, 1, "BOOST:11");        // was "BOOST:11."
    expectValue(RIGHT, "BOOST", 11.4f, 1, "BOOST:11.4");
    expectValue(LEFT,  "BOOST", 11.6f, 1, "BOOST:12");        // rounded, not cut
    expectValue(LEFT,  "BARO",  29.92f, 2, "BARO:30");
    expectValue(LEFT,  "FUELP", 103.4f, 1, "FUELP:103");
    expectValue(LEFT,  "MAFAIR", 1234.5f, 1, "MAFA:1235");    // then the label
    expectValue(LEFT,  "VOLTS", -12.3f, 1, "VOLTS:-12");
}

static void test_negative_keeps_sign(void) {
    expectValue(LEFT,  "EGT11", -1650.0f, 0, "EGT:-1650");
    expectValue(RIGHT, "EGT11", -1650.0f, 0, "EGT1:-1650");
    expectValue(LEFT,  "OAT",   -40.0f, 0, "OAT:-40");
}

// The whole number doesn't fit even with a 2-character label: a marker,
// never leading digits.
static void test_overflow_marker(void) {
    expectValue(LEFT,  "ODO",    1234567.0f, 0, "ODO:#####");
    expectValue(RIGHT, "ODO",    12345678.0f, 0, "ODO:######");
    expectValue(RIGHT, "ODO",    1234567.0f, 0, "OD:1234567");  // right cell: 1 more
    expectValue(LEFT,  "ODO",    123456.0f, 0, "OD:123456");  // 2-char label still fits
    expectValue(LEFT,  "RUNTOT", 1234567.0f, 0, "RUNTO:###");
    expectValue(LEFT,  "ABCDEFG", 1e9f, 0,     "ABCDE:###");
    expectValue(LEFT,  "OAT",    -1234567.0f, 0, "OAT:-####");
    expectValue(LEFT,  "X",      1.0f / 0.0f, 0, "X:#######");  // inf
}

// ERR/CFG/--- used to be clipped to "E" or "CF" behind a long label.
static void test_placeholders_shorten_label(void) {
    expectText(LEFT,  "CHT",     "ERR", "CHT:ERR");
    expectText(LEFT,  "ABCDEFG", "ERR", "ABCDE:ERR");         // was "ABCDEFG:E"
    expectText(RIGHT, "ABCDEFG", "CFG", "ABCDEF:CFG");
    expectText(LEFT,  "RUNTOT",  "---", "RUNTO:---");
    expectText(RIGHT, "RUNTOT",  "---", "RUNTOT:---");
}

// Units come after the value, only whole (or a temperature's degree sign).
static void test_units_fit_whole_or_not_at_all(void) {
    char out[LCD_CELL_BUF];
    uint8_t n = lcdFormatValueCell(out, LEFT, "CHT", 185.0f, 0);       // "CHT:185"
    TEST_ASSERT_EQUAL_UINT8(2, lcdUnitChars(LEFT - n, 1, true));        // °C
    n = lcdFormatValueCell(out, LEFT, "EGT1", 1650.0f, 0);               // full
    TEST_ASSERT_EQUAL_UINT8(0, lcdUnitChars(LEFT - n, 1, true));
    n = lcdFormatValueCell(out, RIGHT, "EGT1", 1650.0f, 0);              // 1 left
    TEST_ASSERT_EQUAL_UINT8(1, lcdUnitChars(RIGHT - n, 1, true));       // ° alone
    n = lcdFormatValueCell(out, RIGHT, "RPM", 2450.0f, 0);               // 2 left
    TEST_ASSERT_EQUAL_UINT8(0, lcdUnitChars(RIGHT - n, 3, false));      // not "rp"
    n = lcdFormatValueCell(out, RIGHT, "OIL", 4.2f, 1);                  // 3 left
    TEST_ASSERT_EQUAL_UINT8(3, lcdUnitChars(RIGHT - n, 3, false));      // bar
    TEST_ASSERT_EQUAL_UINT8(0, lcdUnitChars(0, 0, false));
    // The unit never takes a digit: the value is fitted before it.
    n = lcdFormatValueCell(out, LEFT, "BOOST", 11.4f, 1);
    TEST_ASSERT_EQUAL_STRING("BOOST:11", out);
    TEST_ASSERT_EQUAL_UINT8(0, lcdUnitChars(LEFT - n, 3, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_and_unconfigured_inputs_take_no_cell);
    RUN_TEST(test_eight_or_fewer_is_one_static_page);
    RUN_TEST(test_nine_inputs_page_eight_then_one);
    RUN_TEST(test_forty_inputs_five_pages);
    RUN_TEST(test_nothing_shown);
    RUN_TEST(test_cell_geometry_without_icons);
    RUN_TEST(test_number_format);
    RUN_TEST(test_short_labels_unchanged);
    RUN_TEST(test_long_label_never_cuts_digits);
    RUN_TEST(test_decimals_dropped_first);
    RUN_TEST(test_negative_keeps_sign);
    RUN_TEST(test_overflow_marker);
    RUN_TEST(test_placeholders_shorten_label);
    RUN_TEST(test_units_fit_whole_or_not_at_all);
    return UNITY_END();
}
