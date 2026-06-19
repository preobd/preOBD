// Native unit test: command routing (src/inputs/command_dispatch.cpp).
//
// This is the routing half of issue #192. The dispatch *mechanism* —
// case-folding, table lookup, the configModeOnly / runModeAllowed mode gates,
// unknown-token handling, and verbatim argc/argv/tokenIndex forwarding — is
// generic over whatever COMMANDS[] table and handlers are linked in. So rather
// than dragging the real command_table.cpp (and its ~30-header handler graph)
// onto the host, we compile command_dispatch.cpp against a *synthetic* command
// table with spy handlers and assert which path each input takes.
//
// The production table's contents (every entry well-formed, tokens uppercase,
// PROGMEM access intact) are validated separately by `SYSTEM DIAG INTEGRITY`
// (selftest). Routing + integrity together cover both axes.
//
// command_dispatch.cpp reaches user-facing error text through the four
// command_dispatch.h reporter hooks; we define capturing versions below, so a
// test can assert exactly which error path fired without parsing output text.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/inputs/command_table.h"
#include "../../src/inputs/command_dispatch.cpp"

// ---------------------------------------------------------------------------
// Lightweight stand-ins for symbols command_dispatch.cpp expects from the rest
// of the firmware (their real defining TUs are not compiled here).
// ---------------------------------------------------------------------------

// Case-insensitive compares — mirror command_helpers.cpp semantics exactly.
bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(*a) != toupper(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}
bool streq_P(const char* a, const char* b_P) {
    if (!a || !b_P) return false;
    while (*a) {
        if (toupper(*a) != toupper(*b_P)) return false;
        a++; b_P++;
    }
    return *b_P == '\0';
}

// Test-settable system mode (command_dispatch.cpp only calls isInRunMode()).
static bool g_runMode = false;
bool isInRunMode() { return g_runMode; }

// ---- Reporter-seam spies: record which error path fired + its arguments ----
static int  g_unknownCmdCount;
static char g_unknownCmdName[32];
static int  g_cfgRequiredCount;
static char g_cfgRequiredName[32];
static int  g_unknownSubCount;
static char g_unknownSubCmd[32];
static char g_unknownSubTok[32];
static int  g_subCfgRequiredCount;
static char g_subCfgCmd[32];
static char g_subCfgTok[32];

void dispatchReportUnknownCommand(const char* name) {
    g_unknownCmdCount++;
    strncpy(g_unknownCmdName, name, sizeof(g_unknownCmdName) - 1);
}
void dispatchReportConfigRequired(const char* cmdUpper) {
    g_cfgRequiredCount++;
    strncpy(g_cfgRequiredName, cmdUpper, sizeof(g_cfgRequiredName) - 1);
}
void dispatchReportUnknownSubcommand(const char* commandName, const char* token) {
    g_unknownSubCount++;
    strncpy(g_unknownSubCmd, commandName, sizeof(g_unknownSubCmd) - 1);
    strncpy(g_unknownSubTok, token, sizeof(g_unknownSubTok) - 1);
}
void dispatchReportSubcmdConfigRequired(const char* commandName, const char* token) {
    g_subCfgRequiredCount++;
    strncpy(g_subCfgCmd, commandName, sizeof(g_subCfgCmd) - 1);
    strncpy(g_subCfgTok, token, sizeof(g_subCfgTok) - 1);
}

// ---------------------------------------------------------------------------
// Synthetic top-level command table with spy handlers.
// Each handler returns a unique sentinel so a test can assert which one ran,
// and records the argc/argv it was handed to prove verbatim forwarding.
// ---------------------------------------------------------------------------
static int               g_lastArgc;
static const char* const* g_lastArgv;

#define RET_READ    100  // read-only command (configModeOnly=false)
#define RET_CONFIG  101  // config-only command (configModeOnly=true)
#define RET_MODE    102  // mode-switch command (always available)

static int handlerRead(int argc, const char* const* argv) {
    g_lastArgc = argc; g_lastArgv = argv; return RET_READ;
}
static int handlerConfig(int argc, const char* const* argv) {
    g_lastArgc = argc; g_lastArgv = argv; return RET_CONFIG;
}
static int handlerMode(int argc, const char* const* argv) {
    g_lastArgc = argc; g_lastArgv = argv; return RET_MODE;
}

// The routing functions reference the globals COMMANDS / NUM_COMMANDS; we own
// the synthetic definitions here. Order/contents are arbitrary — only the
// (name, handler, configModeOnly) tuple matters for routing.
const Command COMMANDS[] = {
    {"CONFIG", handlerMode,   "", false},
    {"LIST",   handlerRead,   "", false},
    {"SET",    handlerConfig, "", true},
};
const uint8_t NUM_COMMANDS = sizeof(COMMANDS) / sizeof(Command);

// ---------------------------------------------------------------------------
// Synthetic subcommand table (mirrors the SYSTEM shape: a read-only verb and a
// destructive verb under one parent).
// ---------------------------------------------------------------------------
static int               g_lastSubArgc;
static const char* const* g_lastSubArgv;
static int               g_lastTokenIndex;

#define RET_STATUS  200  // runModeAllowed=true  (read-only)
#define RET_RESET   201  // runModeAllowed=false (destructive)

static int subStatus(int argc, const char* const* argv, int tokenIndex) {
    g_lastSubArgc = argc; g_lastSubArgv = argv; g_lastTokenIndex = tokenIndex;
    return RET_STATUS;
}
static int subReset(int argc, const char* const* argv, int tokenIndex) {
    g_lastSubArgc = argc; g_lastSubArgv = argv; g_lastTokenIndex = tokenIndex;
    return RET_RESET;
}

static const Subcommand SUB_TABLE[] = {
    {"STATUS", subStatus, true,  ""},
    {"RESET",  subReset,  false, ""},
};
static const uint8_t SUB_TABLE_LEN = sizeof(SUB_TABLE) / sizeof(Subcommand);

// ---------------------------------------------------------------------------

void setUp(void) {
    g_runMode = false;
    g_unknownCmdCount = g_cfgRequiredCount = 0;
    g_unknownSubCount = g_subCfgRequiredCount = 0;
    g_unknownCmdName[0] = g_cfgRequiredName[0] = '\0';
    g_unknownSubCmd[0] = g_unknownSubTok[0] = '\0';
    g_subCfgCmd[0] = g_subCfgTok[0] = '\0';
    g_lastArgc = -1; g_lastArgv = nullptr;
    g_lastSubArgc = -1; g_lastSubArgv = nullptr; g_lastTokenIndex = -1;
}
void tearDown(void) {}

// ===================== dispatchCommand ====================================

// argc == 0 is a no-op success: no handler, no error path.
static void test_cmd_empty_is_noop(void) {
    const char* argv[] = {"ignored"};
    TEST_ASSERT_EQUAL_INT(0, dispatchCommand(0, argv));
    TEST_ASSERT_EQUAL_INT(0, g_unknownCmdCount);
    TEST_ASSERT_EQUAL_INT(-1, g_lastArgc);  // no handler ran
}

// Unknown command: returns 1, reports the name as typed, runs no handler.
static void test_cmd_unknown_reports_and_returns_1(void) {
    const char* argv[] = {"NOPE"};
    TEST_ASSERT_EQUAL_INT(1, dispatchCommand(1, argv));
    TEST_ASSERT_EQUAL_INT(1, g_unknownCmdCount);
    TEST_ASSERT_EQUAL_STRING("NOPE", g_unknownCmdName);
    TEST_ASSERT_EQUAL_INT(-1, g_lastArgc);
}

// Read-only command runs in RUN mode and the handler's return value passes
// straight through.
static void test_cmd_readonly_runs_in_run_mode(void) {
    g_runMode = true;
    const char* argv[] = {"LIST", "INPUTS"};
    TEST_ASSERT_EQUAL_INT(RET_READ, dispatchCommand(2, argv));
    TEST_ASSERT_EQUAL_INT(0, g_cfgRequiredCount);
}

// Config-only command is rejected in RUN mode: returns 1, fires the
// config-required report, never reaches the handler.
static void test_cmd_configonly_blocked_in_run_mode(void) {
    g_runMode = true;
    const char* argv[] = {"SET", "3", "APPLICATION", "CHT"};
    TEST_ASSERT_EQUAL_INT(1, dispatchCommand(4, argv));
    TEST_ASSERT_EQUAL_INT(1, g_cfgRequiredCount);
    TEST_ASSERT_EQUAL_STRING("SET", g_cfgRequiredName);
    TEST_ASSERT_EQUAL_INT(-1, g_lastArgc);  // handler not run
}

// Same config-only command is allowed in CONFIG mode and reaches its handler.
static void test_cmd_configonly_runs_in_config_mode(void) {
    g_runMode = false;
    const char* argv[] = {"SET", "3", "APPLICATION", "CHT"};
    TEST_ASSERT_EQUAL_INT(RET_CONFIG, dispatchCommand(4, argv));
    TEST_ASSERT_EQUAL_INT(0, g_cfgRequiredCount);
}

// Command name match is case-insensitive (input is upper-cased before lookup).
static void test_cmd_match_is_case_insensitive(void) {
    const char* argv[] = {"cOnFiG"};
    TEST_ASSERT_EQUAL_INT(RET_MODE, dispatchCommand(1, argv));
}

// The handler receives argc/argv verbatim (same pointer, same count).
static void test_cmd_forwards_argc_argv_verbatim(void) {
    const char* argv[] = {"LIST", "SENSORS", "TEMP"};
    TEST_ASSERT_EQUAL_INT(RET_READ, dispatchCommand(3, argv));
    TEST_ASSERT_EQUAL_INT(3, g_lastArgc);
    TEST_ASSERT_EQUAL_PTR(argv, g_lastArgv);
}

// ===================== dispatchSubcommand =================================

// Unknown subcommand token: returns 1, reports parent + token, runs nothing.
static void test_sub_unknown_reports_and_returns_1(void) {
    const char* argv[] = {"SYSTEM", "BOGUS"};
    int rc = dispatchSubcommand(SUB_TABLE, SUB_TABLE_LEN, "BOGUS", "SYSTEM",
                                2, argv, 1);
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_INT(1, g_unknownSubCount);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", g_unknownSubCmd);
    TEST_ASSERT_EQUAL_STRING("BOGUS", g_unknownSubTok);
    TEST_ASSERT_EQUAL_INT(-1, g_lastSubArgc);
}

// Read-only verb (runModeAllowed=true) runs even in RUN mode, with tokenIndex
// and argc/argv forwarded to the leaf verbatim.
static void test_sub_runmode_verb_runs_in_run_mode(void) {
    g_runMode = true;
    const char* argv[] = {"SYSTEM", "STATUS"};
    int rc = dispatchSubcommand(SUB_TABLE, SUB_TABLE_LEN, "STATUS", "SYSTEM",
                                2, argv, 1);
    TEST_ASSERT_EQUAL_INT(RET_STATUS, rc);
    TEST_ASSERT_EQUAL_INT(2, g_lastSubArgc);
    TEST_ASSERT_EQUAL_PTR(argv, g_lastSubArgv);
    TEST_ASSERT_EQUAL_INT(1, g_lastTokenIndex);
    TEST_ASSERT_EQUAL_INT(0, g_subCfgRequiredCount);
}

// Destructive verb (runModeAllowed=false) is blocked in RUN mode: returns 1,
// fires the subcommand config-required report, never reaches the leaf.
static void test_sub_destructive_verb_blocked_in_run_mode(void) {
    g_runMode = true;
    const char* argv[] = {"SYSTEM", "RESET", "CONFIRM"};
    int rc = dispatchSubcommand(SUB_TABLE, SUB_TABLE_LEN, "RESET", "SYSTEM",
                                3, argv, 1);
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_INT(1, g_subCfgRequiredCount);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", g_subCfgCmd);
    TEST_ASSERT_EQUAL_STRING("RESET", g_subCfgTok);
    TEST_ASSERT_EQUAL_INT(-1, g_lastSubArgc);  // leaf not run
}

// Same destructive verb runs in CONFIG mode.
static void test_sub_destructive_verb_runs_in_config_mode(void) {
    g_runMode = false;
    const char* argv[] = {"SYSTEM", "RESET", "CONFIRM"};
    int rc = dispatchSubcommand(SUB_TABLE, SUB_TABLE_LEN, "RESET", "SYSTEM",
                                3, argv, 1);
    TEST_ASSERT_EQUAL_INT(RET_RESET, rc);
    TEST_ASSERT_EQUAL_INT(0, g_subCfgRequiredCount);
}

// Subcommand token match is case-insensitive (streq_P).
static void test_sub_match_is_case_insensitive(void) {
    const char* argv[] = {"SYSTEM", "status"};
    int rc = dispatchSubcommand(SUB_TABLE, SUB_TABLE_LEN, "status", "SYSTEM",
                                2, argv, 1);
    TEST_ASSERT_EQUAL_INT(RET_STATUS, rc);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cmd_empty_is_noop);
    RUN_TEST(test_cmd_unknown_reports_and_returns_1);
    RUN_TEST(test_cmd_readonly_runs_in_run_mode);
    RUN_TEST(test_cmd_configonly_blocked_in_run_mode);
    RUN_TEST(test_cmd_configonly_runs_in_config_mode);
    RUN_TEST(test_cmd_match_is_case_insensitive);
    RUN_TEST(test_cmd_forwards_argc_argv_verbatim);
    RUN_TEST(test_sub_unknown_reports_and_returns_1);
    RUN_TEST(test_sub_runmode_verb_runs_in_run_mode);
    RUN_TEST(test_sub_destructive_verb_blocked_in_run_mode);
    RUN_TEST(test_sub_destructive_verb_runs_in_config_mode);
    RUN_TEST(test_sub_match_is_case_insensitive);
    return UNITY_END();
}
