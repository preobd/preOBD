// Native unit test: the console's line splitter (#624). It replaced
// embedded-cli's tokenizer; before that library was removed, a differential
// run compared the two over 5,282,812 strings (every string of up to 9 chars
// over space, quote, backslash, a, b, plus 200,000 random longer ones, at caps
// of 4 and 16) with no difference. These cases pin the rules it kept.
#include <unity.h>
#include <string.h>

#include "../../src/console/cli_tokenize.cpp"

static char line[128];
static char* argv[16];
static uint8_t argc;

static void split(const char* s, uint8_t cap = 16) {
    strncpy(line, s, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    argc = cliTokenize(line, argv, cap);
}

#define ASSERT_WORDS(...) do {                                         \
    const char* want[] = {__VA_ARGS__};                               \
    TEST_ASSERT_EQUAL_UINT8(sizeof(want) / sizeof(want[0]), argc);     \
    for (uint8_t i = 0; i < argc; i++) TEST_ASSERT_EQUAL_STRING(want[i], argv[i]); \
} while (0)

void setUp() {}
void tearDown() {}

void test_spaces_separate_words_and_runs_count_once() {
    split("SET  A0   NAME x");
    ASSERT_WORDS("SET", "A0", "NAME", "x");
}

void test_leading_and_trailing_spaces_make_no_word() {
    split("   VERSION   ");
    ASSERT_WORDS("VERSION");
}

void test_empty_and_blank_lines_have_no_words() {
    split("");
    TEST_ASSERT_EQUAL_UINT8(0, argc);
    split("     ");
    TEST_ASSERT_EQUAL_UINT8(0, argc);
}

void test_quotes_keep_spaces() {
    split("SET A0 NAME \"Oil Temp\"");
    ASSERT_WORDS("SET", "A0", "NAME", "Oil Temp");
}

void test_empty_quotes_are_no_word() {
    // cmd_action.cpp relies on this: NAME "" arrives with no value word
    split("ACTION 1 NAME \"\"");
    ASSERT_WORDS("ACTION", "1", "NAME");
}

void test_a_quote_ends_the_word() {
    split("a\"b c\"d");
    ASSERT_WORDS("a", "b c", "d");
    split("a\"\"b");
    ASSERT_WORDS("a", "b");
    split("\"a\"b");
    ASSERT_WORDS("a", "b");
}

void test_backslash_makes_the_next_char_literal() {
    split("x\\\"y");            // x\"y
    ASSERT_WORDS("x\"y");
    split("two\\ words");       // two\ words
    ASSERT_WORDS("two words");
    split("back\\\\slash");     // back\\slash
    ASSERT_WORDS("back\\slash");
}

void test_backslash_works_inside_quotes() {
    split("\"say \\\"hi\\\"\"");   // "say \"hi\""
    ASSERT_WORDS("say \"hi\"");
}

void test_trailing_backslash_is_dropped() {
    split("abc\\");
    ASSERT_WORDS("abc");
}

void test_unclosed_quote_runs_to_end_of_line() {
    split("NAME \"Oil Temp");
    ASSERT_WORDS("NAME", "Oil Temp");
}

void test_cap_exactly_full() {
    split("a b c d", 4);
    ASSERT_WORDS("a", "b", "c", "d");
}

void test_cap_ignores_the_rest() {
    split("a b c d e f", 4);
    ASSERT_WORDS("a", "b", "c", "d");
}

void test_cap_reached_mid_word_keeps_last_word_whole() {
    split("a b c dddd eeee", 4);
    ASSERT_WORDS("a", "b", "c", "dddd");
}

void test_the_command_name_is_split_like_any_word() {
    // The whole line is split, name included: a quote in the name only
    // matters for an unknown command, which this still is
    split("VER\"SION\" x");
    ASSERT_WORDS("VER", "SION", "x");
}

void test_words_fit_in_a_full_line_without_slack() {
    // 127 characters in a 128-byte buffer: nothing is written past the NUL
    char full[128];
    memset(full, 'x', 127);
    full[127] = '\0';
    char guard = 0x5A;
    char buf[129];
    memcpy(buf, full, 128);
    buf[128] = guard;
    char* words[4];
    TEST_ASSERT_EQUAL_UINT8(1, cliTokenize(buf, words, 4));
    TEST_ASSERT_EQUAL(127, (int)strlen(words[0]));
    TEST_ASSERT_EQUAL_HEX8(guard, buf[128]);
}

void test_null_line() {
    TEST_ASSERT_EQUAL_UINT8(0, cliTokenize(nullptr, argv, 16));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_spaces_separate_words_and_runs_count_once);
    RUN_TEST(test_leading_and_trailing_spaces_make_no_word);
    RUN_TEST(test_empty_and_blank_lines_have_no_words);
    RUN_TEST(test_quotes_keep_spaces);
    RUN_TEST(test_empty_quotes_are_no_word);
    RUN_TEST(test_a_quote_ends_the_word);
    RUN_TEST(test_backslash_makes_the_next_char_literal);
    RUN_TEST(test_backslash_works_inside_quotes);
    RUN_TEST(test_trailing_backslash_is_dropped);
    RUN_TEST(test_unclosed_quote_runs_to_end_of_line);
    RUN_TEST(test_cap_exactly_full);
    RUN_TEST(test_cap_ignores_the_rest);
    RUN_TEST(test_cap_reached_mid_word_keeps_last_word_whole);
    RUN_TEST(test_the_command_name_is_split_like_any_word);
    RUN_TEST(test_words_fit_in_a_full_line_without_slack);
    RUN_TEST(test_null_line);
    return UNITY_END();
}
