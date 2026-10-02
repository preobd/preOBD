// Native unit test: the plain CLI line reader (src/cli/line_reader.*, #147).
//
// It replaces embedded-cli's editor on mega2560. Clients (webapp, app) match
// replies on the echo, the "\r\n" and the prompt, so these pin each behaviour
// it copies from embedded_cli.c: printable-only input, CRLF/LFCR counted once,
// the backspace escape sequence, ignored ESC [ sequences, the 2-byte slack.
#include <unity.h>
#include <string>

#include "../../src/cli/line_reader.cpp"

static std::string echoed;
static void capture(char c) { echoed += c; }

static char buf[16];
static LineReader r;

void setUp(void) {
    echoed.clear();
    lineReaderInit(r, buf, sizeof(buf));
}
void tearDown(void) {}

// Feed a string; return how many line completions it produced. The last
// completed line is left in `last`.
static std::string last;
static int feed(const char* s, size_t n) {
    int lines = 0;
    for (size_t i = 0; i < n; i++) {
        if (lineReaderFeed(r, s[i], capture)) {
            last.assign(r.buf, r.len);
            lineReaderClear(r);
            lines++;
        }
    }
    return lines;
}
static int feed(const char* s) { return feed(s, strlen(s)); }

static void test_line_is_echoed_and_returned(void) {
    TEST_ASSERT_EQUAL_INT(1, feed("set a0\r"));
    TEST_ASSERT_EQUAL_STRING("set a0", last.c_str());
    TEST_ASSERT_EQUAL_STRING("set a0\r\n", echoed.c_str());
}

static void test_crlf_and_lfcr_end_one_line(void) {
    TEST_ASSERT_EQUAL_INT(1, feed("A\r\n"));
    TEST_ASSERT_EQUAL_INT(1, feed("B\n\r"));
    TEST_ASSERT_EQUAL_STRING("B", last.c_str());
    TEST_ASSERT_EQUAL_STRING("A\r\nB\r\n", echoed.c_str());
}

// A blank Enter still completes (the caller prints the prompt again)
static void test_empty_enter_completes(void) {
    TEST_ASSERT_EQUAL_INT(2, feed("\r\r"));
    TEST_ASSERT_EQUAL_UINT16(0, r.len);
    TEST_ASSERT_EQUAL_STRING("", last.c_str());
}

static void test_backspace_deletes_with_embedded_cli_sequence(void) {
    feed("AB\b");
    TEST_ASSERT_EQUAL_STRING("AB\x1B[D\x1B[P", echoed.c_str());
    feed("\x7F\x7F\x7F");          // only one char left: one deletion echoed
    TEST_ASSERT_EQUAL_UINT16(0, r.len);
    feed("C\n");
    TEST_ASSERT_EQUAL_STRING("C", last.c_str());
}

// Arrow keys, Home and Delete would be history/cursor moves in embedded-cli
static void test_escape_sequences_are_ignored(void) {
    feed("X\x1B[A\x1B[D\x1B[1~\x1B[3~Y\r");
    TEST_ASSERT_EQUAL_STRING("XY", last.c_str());
    TEST_ASSERT_EQUAL_STRING("XY\r\n", echoed.c_str());
}

// Non-ASCII (UTF-8 "°") and tab are dropped, not stored or echoed
static void test_non_printable_bytes_are_dropped(void) {
    feed("T\xC2\xB0\tF\r");
    TEST_ASSERT_EQUAL_STRING("TF", last.c_str());
    TEST_ASSERT_EQUAL_STRING("TF\r\n", echoed.c_str());
}

// embedded-cli's onCharInput rule (len + 2 >= cap rejects): in 16 bytes, 14
// chars + NUL + the one extra NUL embeddedCliTokenizeArgs writes past it
static void test_overlong_input_is_cut_with_tokenizer_slack(void) {
    feed("ABCDEFGHIJKLMNOPQRSTUVWXYZ\r");
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMN", last.c_str());
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMN\r\n", echoed.c_str());
}

static void test_quotes_and_spaces_are_kept_for_the_tokenizer(void) {
    feed("NAME \"a  b\"\r");
    TEST_ASSERT_EQUAL_STRING("NAME \"a  b\"", last.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_is_echoed_and_returned);
    RUN_TEST(test_crlf_and_lfcr_end_one_line);
    RUN_TEST(test_empty_enter_completes);
    RUN_TEST(test_backspace_deletes_with_embedded_cli_sequence);
    RUN_TEST(test_escape_sequences_are_ignored);
    RUN_TEST(test_non_printable_bytes_are_dropped);
    RUN_TEST(test_overlong_input_is_cut_with_tokenizer_slack);
    RUN_TEST(test_quotes_and_spaces_are_kept_for_the_tokenizer);
    return UNITY_END();
}
