// Native unit test: where console commands come from (src/console/console_input.*,
// #624). Each console source -- the primary, the secondary and a framed port --
// has its own line, so two links can't mix into one command, and a JSON IMPORT
// belongs to the source that started it. Runs the real pump, line reader,
// tokenizer and import stream against fake transports; dispatchCommand is a
// spy that records each command and the source it ran for.
#define SUPPORTS_JSON_IMPORT_STREAM 1
#define JSON_IMPORT_MAX_BYTES       256
#define CLI_CMD_BUFFER_SIZE         128
#define CLI_MAX_ARGS                16

#include <unity.h>
#include <Arduino.h>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>

#include "../../src/transports/transport_registry.cpp"
#include "../../src/console/console_routing.cpp"
#include "../../src/console/console_input.cpp"
#include "../../src/console/line_reader.cpp"
#include "../../src/console/cli_tokenize.cpp"
#include "../../src/console/json_import_stream.cpp"
#include "../../src/core/message_api.cpp"
#include "../../src/core/log_filter.cpp"

SystemConfig systemConfig;
void applyLogFilterConfig() {}
void printSaveReminder() {}

// loadConfigFromJSON: records what an import applied
static std::string g_applied;
static int g_loads = 0;
bool loadConfigFromJSON(const char* json) {
    g_applied = json; g_loads++;
    return true;
}

// A link: bytes queued to read, everything written captured
class FakeLink : public TransportInterface {
public:
    std::string in, out;
    const char* name = "FAKE";
    uint8_t caps = CAP_HARDWARE_SERIAL;
    TransportState state = TRANSPORT_CONNECTED;
    size_t write(uint8_t c) override { out += (char)c; return 1; }
    size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
    int available() override { return (int)in.size(); }
    int read() override { if (in.empty()) return -1; char c = in[0]; in.erase(0, 1); return (uint8_t)c; }
    int peek() override { return in.empty() ? -1 : (uint8_t)in[0]; }
    void flush() override {}
    const char* getName() const override { return name; }
    uint8_t getCapabilities() const override { return caps; }
    TransportState getState() const override { return state; }
    bool begin() override { return true; }
    void end() override {}
    void update() override {}
};

// The command table, as a spy. "JSON IMPORT <n>" starts an import like
// cmd_json; "OFF" turns Serial1 off from inside a command (BUS SERIAL 1 DISABLE)
struct Ran { std::string words; uint8_t source; };
static std::vector<Ran> g_ran;
static bool g_portActive[9];
int dispatchCommand(int argc, const char* const* argv) {
    std::string w;
    for (int i = 0; i < argc; i++) { if (i) w += '|'; w += argv[i]; }
    g_ran.push_back({w, consoleActiveSourceId()});
    if (strcmp(argv[0], "JSON") == 0 && argc >= 3) {
        jsonImportBegin(consoleActiveSourceId(), (size_t)atoi(argv[2]));
        return 0;
    }
    if (strcmp(argv[0], "OFF") == 0) g_portActive[1] = false;
    msg.control.print("R");
    return 0;
}

static bool probe(uint8_t port_id) { return port_id < 9 && g_portActive[port_id]; }

static FakeLink usb, s1, s2;
static const char* P = "preOBD> ";

void setUp(void) {
    memset(g_portActive, 0, sizeof(g_portActive));
    setSerialPortActiveProbe(probe);
    for (uint8_t i = 0; i < NUM_TRANSPORTS; i++) s_transports[i] = nullptr;
    usb = FakeLink(); s1 = FakeLink(); s2 = FakeLink();
    usb.name = "USB_SERIAL"; s1.name = "SERIAL1"; s2.name = "SERIAL2";
    transportRegister(TRANSPORT_USB_SERIAL, &usb);
    transportRegister(TRANSPORT_SERIAL1, &s1);
    transportRegister(TRANSPORT_SERIAL2, &s2);
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_USB_SERIAL, false);
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_USB_SERIAL, false);
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_NONE, true);
    setMessageSinkResolver(PLANE_CONTROL, consoleSink);
    setMessageSinkResolver(PLANE_DEBUG, logSink);
    if (jsonImportActive()) jsonImportCancel();
    consoleInputInit();
    usb.out.clear();
    g_ran.clear(); g_applied.clear(); g_loads = 0;
    g_test_now = 1000;
}
void tearDown(void) { setSerialPortActiveProbe(nullptr); }

// The nRF link: Serial1, started, framed
static void framedSerial1() { g_portActive[1] = true; s1.caps |= CAP_FRAMED; }

static void test_banner_and_first_prompt_go_to_the_console(void) {
    consoleInputInit();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, usb.out.find("Type 'HELP' for commands"));
    TEST_ASSERT_EQUAL_STRING(P, usb.out.substr(usb.out.size() - 8).c_str());
}

static void test_a_line_runs_with_its_echo_reply_and_prompt_on_its_source(void) {
    framedSerial1();
    s1.in = "VERSION\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("VERSION", g_ran[0].words.c_str());
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, g_ran[0].source);
    TEST_ASSERT_EQUAL_STRING("VERSION\r\nRpreOBD> ", s1.out.c_str());
    TEST_ASSERT_EQUAL_STRING("", usb.out.c_str());
    TEST_ASSERT_NULL(consoleActiveSource());           // cleared after the pump
    TEST_ASSERT_EQUAL(TRANSPORT_NONE, consoleActiveSourceId());
}

// The bug this module exists for: a half-typed USB line and a BLE command
static void test_two_sources_never_mix_into_one_line(void) {
    framedSerial1();
    usb.in = "VERS";
    s1.in = "LIST INPUTS\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("LIST|INPUTS", g_ran[0].words.c_str());
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, g_ran[0].source);

    usb.in = "ION\r";                                   // USB finishes its own line
    consolePump();
    TEST_ASSERT_EQUAL(2, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("VERSION", g_ran[1].words.c_str());
    TEST_ASSERT_EQUAL(TRANSPORT_USB_SERIAL, g_ran[1].source);
    TEST_ASSERT_EQUAL(std::string::npos, s1.out.find("VERS"  "ION"));   // nothing of USB's on BLE
}

static void test_the_name_is_upper_cased_and_the_arguments_keep_case_and_quotes(void) {
    usb.in = "set A0 name \"Oil Temp\"\r";
    consolePump();
    TEST_ASSERT_EQUAL_STRING("SET|A0|name|Oil Temp", g_ran[0].words.c_str());
}

static void test_a_blank_line_runs_nothing_and_prompts(void) {
    usb.in = "   \r";
    consolePump();
    TEST_ASSERT_EQUAL(0, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("   \r\npreOBD> ", usb.out.c_str());
}

static void test_a_127_char_line_runs_whole(void) {
    usb.in = std::string(127, 'A') + "\r";
    consolePump();
    TEST_ASSERT_EQUAL(127, (int)g_ran[0].words.size());
    usb.in = std::string(130, 'B') + "\r";              // the rest past 127 is dropped
    consolePump();
    TEST_ASSERT_EQUAL(127, (int)g_ran[1].words.size());
}

// A transport that stops being a source gives its slot back; its half-typed
// line is dropped, not finished later by anyone
static void test_a_source_that_goes_away_loses_its_half_line(void) {
    g_portActive[2] = true;
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL2, true);   // secondary
    s2.in = "HALF";
    consolePump();
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);      // no longer a source
    consolePump();
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL2, true);   // back again
    s2.in = "LINE\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("LINE", g_ran[0].words.c_str());
}

// The console's primary is a port that is off and there is no secondary:
// USB stands in for it (#558) and is read
static void test_usb_is_read_when_it_stands_in_for_the_primary(void) {
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL2, false);  // Serial2 not started
    usb.in = "VERSION\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL(TRANSPORT_USB_SERIAL, g_ran[0].source);
    TEST_ASSERT_TRUE(consoleIsSource(TRANSPORT_USB_SERIAL));
}

// The console's own transports are read only while connected; a framed port
// whatever its state says (the mux is always CONNECTED, but don't rely on it)
static void test_a_disconnected_console_port_waits_a_framed_port_does_not(void) {
    framedSerial1();
    usb.state = TRANSPORT_DISCONNECTED;
    s1.state = TRANSPORT_DISCONNECTED;
    usb.in = "A\r";
    s1.in = "B\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("B", g_ran[0].words.c_str());
    TEST_ASSERT_EQUAL_STRING("A\r", usb.in.c_str());            // left for when it connects
}

// A command that turns its own port off: the port is not read again (#558)
static void test_a_port_turned_off_by_its_own_command_is_not_read_again(void) {
    framedSerial1();
    s1.in = "OFF\rMORE\r";
    consolePump();
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("MORE\r", s1.in.c_str());   // left unread
}

// The list of sources changes under the pump: the primary (Serial1) turns
// itself off, USB stands in for it (#558), and USB's waiting command still
// runs in the same pump rather than being skipped to the next loop
static void test_a_source_that_appears_mid_pump_is_still_read(void) {
    g_portActive[1] = true;
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);
    s1.in = "OFF\r";
    usb.in = "VERSION\r";
    consolePump();
    TEST_ASSERT_EQUAL(2, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("VERSION", g_ran[1].words.c_str());
    TEST_ASSERT_EQUAL(TRANSPORT_USB_SERIAL, g_ran[1].source);
}

static void test_an_unstarted_framed_port_is_not_read(void) {
    s1.caps |= CAP_FRAMED;                               // framed but not started
    s1.in = "VERSION\r";
    consolePump();
    TEST_ASSERT_EQUAL(0, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("VERSION\r", s1.in.c_str());
}

static void test_is_source_follows_the_configuration(void) {
    TEST_ASSERT_TRUE(consoleIsSource(TRANSPORT_USB_SERIAL));
    TEST_ASSERT_FALSE(consoleIsSource(TRANSPORT_SERIAL1));
    framedSerial1();
    TEST_ASSERT_TRUE(consoleIsSource(TRANSPORT_SERIAL1));
    TEST_ASSERT_FALSE(consoleIsSource(TRANSPORT_SERIAL2));
}

// ---- JSON IMPORT ----

// The command and its payload arrive in one read: the payload must reach the
// import, not run as commands (embedded-cli queued it all first)
static void test_import_command_and_payload_in_one_read(void) {
    framedSerial1();
    s1.in = "JSON IMPORT 7\r{\"a\":1}";
    consolePump();
    TEST_ASSERT_EQUAL(1, g_loads);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", g_applied.c_str());
    TEST_ASSERT_EQUAL(1, (int)g_ran.size());             // only JSON IMPORT ran
    TEST_ASSERT_EQUAL_STRING("JSON IMPORT 7\r\nReady for 7 bytes\r\npreOBD> Applied\r\n\r\npreOBD> ",
                             s1.out.c_str());
}

// Other sources keep running commands, and their bytes never enter the import
static void test_other_sources_run_commands_during_an_import(void) {
    framedSerial1();
    usb.in = "JSON IMPORT 5\rab";
    consolePump();
    s1.in = "VERSION\r";
    consolePump();
    usb.in = "cde";
    consolePump();
    TEST_ASSERT_EQUAL_STRING("abcde", g_applied.c_str());
    TEST_ASSERT_EQUAL(2, (int)g_ran.size());
    TEST_ASSERT_EQUAL_STRING("VERSION", g_ran[1].words.c_str());
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, g_ran[1].source);
}

static void test_only_one_import_at_a_time(void) {
    TEST_ASSERT_TRUE(jsonImportBegin(TRANSPORT_USB_SERIAL, 10));
    TEST_ASSERT_FALSE(jsonImportBegin(TRANSPORT_SERIAL1, 10));
    TEST_ASSERT_EQUAL(TRANSPORT_USB_SERIAL, jsonImportSource());
}

// The result goes to the importer only, as "Applied", a blank line and the
// prompt: none of another source's half-typed line rides along
static void test_the_import_result_goes_to_the_importer_alone(void) {
    framedSerial1();
    usb.in = "VER";                                      // USB half-typed
    s1.in = "JSON IMPORT 2\r{}";
    consolePump();
    const std::string tail = "Applied\r\n\r\npreOBD> ";
    TEST_ASSERT_EQUAL_STRING(tail.c_str(), s1.out.substr(s1.out.size() - tail.size()).c_str());
    TEST_ASSERT_EQUAL(std::string::npos, s1.out.find("VER\r"));
    TEST_ASSERT_EQUAL_STRING("VER", usb.out.c_str());    // USB only ever saw its own echo
}

static void test_a_quiet_import_times_out_on_its_own_link(void) {
    framedSerial1();
    s1.in = "JSON IMPORT 9\r{";
    consolePump();
    s1.out.clear();
    g_test_now += 45000;
    consolePump();
    TEST_ASSERT_FALSE(jsonImportActive());
    TEST_ASSERT_EQUAL_STRING("ERROR: JSON import timed out (no data received)\r\npreOBD> ", s1.out.c_str());
    TEST_ASSERT_EQUAL_STRING("", usb.out.c_str());
    TEST_ASSERT_EQUAL(0, g_loads);
}

// The importer stops being a source (its port turned off): cancelled at once,
// and the console is told, since the importer can't be
static void test_an_import_whose_link_goes_away_is_cancelled(void) {
    framedSerial1();
    s1.in = "JSON IMPORT 9\r{";
    consolePump();
    g_portActive[1] = false;                             // BUS SERIAL 1 DISABLE from USB
    consolePump();
    TEST_ASSERT_FALSE(jsonImportActive());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        usb.out.find("ERROR: JSON import cancelled: SERIAL1 is no longer a console link\r\npreOBD> "));
    TEST_ASSERT_EQUAL(0, g_loads);
}

static void test_a_sentinel_import_echoes_and_ends_at_end_json(void) {
    usb.in = "JSON IMPORT 0\r";                           // atoi("0") = sentinel mode
    consolePump();
    usb.out.clear();
    usb.in = "{\"a\":1}\nend json\n";
    consolePump();
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}\n", g_applied.c_str());
    TEST_ASSERT_EQUAL(0, (int)usb.out.find("{\"a\":1}\r\nend json\r\nApplied"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_banner_and_first_prompt_go_to_the_console);
    RUN_TEST(test_a_line_runs_with_its_echo_reply_and_prompt_on_its_source);
    RUN_TEST(test_two_sources_never_mix_into_one_line);
    RUN_TEST(test_the_name_is_upper_cased_and_the_arguments_keep_case_and_quotes);
    RUN_TEST(test_a_blank_line_runs_nothing_and_prompts);
    RUN_TEST(test_a_127_char_line_runs_whole);
    RUN_TEST(test_a_source_that_goes_away_loses_its_half_line);
    RUN_TEST(test_usb_is_read_when_it_stands_in_for_the_primary);
    RUN_TEST(test_a_disconnected_console_port_waits_a_framed_port_does_not);
    RUN_TEST(test_a_port_turned_off_by_its_own_command_is_not_read_again);
    RUN_TEST(test_a_source_that_appears_mid_pump_is_still_read);
    RUN_TEST(test_an_unstarted_framed_port_is_not_read);
    RUN_TEST(test_is_source_follows_the_configuration);
    RUN_TEST(test_import_command_and_payload_in_one_read);
    RUN_TEST(test_other_sources_run_commands_during_an_import);
    RUN_TEST(test_only_one_import_at_a_time);
    RUN_TEST(test_the_import_result_goes_to_the_importer_alone);
    RUN_TEST(test_a_quiet_import_times_out_on_its_own_link);
    RUN_TEST(test_an_import_whose_link_goes_away_is_cancelled);
    RUN_TEST(test_a_sentinel_import_echoes_and_ends_at_end_json);
    return UNITY_END();
}
