// Native unit test: the transport registry and msg's sinks (#612 phase 2b-2,
// which split the old MessageRouter up). Where commands come from -- the
// console pump, a line per source -- is test_console_input (#624). This pins:
//   - a hardware serial transport whose port was never started is never
//     touched (#558: on Teensy 4 reading an unclocked UART is a hard fault;
//     BUS SERIAL 1 DISABLE + SAVE on a FRAMED port boot-looped the board);
//   - the console falls back to USB when neither of its transports is usable,
//     the log never does;
//   - a reply goes to the source of the command only (consoleSetActive),
//     unsolicited output to the console's primary and secondary;
//   - msg with no sink registered writes nowhere; a disconnected primary
//     skips the secondary (today's behaviour, kept);
//   - LIST TRANSPORTS prints exactly what the router printed.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/transports/transport_registry.cpp"
#include "../../src/console/console_routing.cpp"
#include "../../src/core/message_api.cpp"
#include "../../src/core/log_filter.cpp"

SystemConfig systemConfig;
void applyLogFilterConfig() {}

// A transport that records what is written to it and counts every touch
class FakeTransport : public TransportInterface {
public:
    int touches = 0;
    int pending = 0;                 // bytes available to read ('x' each)
    uint8_t caps = CAP_HARDWARE_SERIAL;
    TransportState state = TRANSPORT_CONNECTED;
    char out[256] = {0};
    size_t outLen = 0;
    size_t write(uint8_t c) override { touches++; if (outLen < sizeof(out) - 1) out[outLen++] = (char)c; return 1; }
    size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
    int available() override { touches++; return pending; }
    int read() override { touches++; if (pending > 0) { pending--; return 'x'; } return -1; }
    int peek() override { touches++; return pending ? 'x' : -1; }
    void flush() override { touches++; }
    const char* getName() const override { return "FAKE"; }
    uint8_t getCapabilities() const override { return caps; }
    TransportState getState() const override { return state; }
    bool begin() override { return true; }
    void end() override {}
    void update() override { touches++; }
    void clearOut() { outLen = 0; out[0] = '\0'; }
};

static bool g_portActive[9];
static bool probe(uint8_t port_id) { return port_id < 9 && g_portActive[port_id]; }

static FakeTransport usb, s1, s2;

static void reset() {
    memset(g_portActive, 0, sizeof(g_portActive));
    setSerialPortActiveProbe(probe);
    for (uint8_t i = 0; i < NUM_TRANSPORTS; i++) s_transports[i] = nullptr;
    usb = FakeTransport(); s1 = FakeTransport(); s2 = FakeTransport();
    transportRegister(TRANSPORT_USB_SERIAL, &usb);
    transportRegister(TRANSPORT_SERIAL1, &s1);
    transportRegister(TRANSPORT_SERIAL2, &s2);
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_USB_SERIAL, false);
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_USB_SERIAL, false);
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_NONE, true);
    setMessageSinkResolver(PLANE_CONTROL, consoleSink);
    setMessageSinkResolver(PLANE_DEBUG, logSink);
}

void setUp(void) { reset(); }
void tearDown(void) { setSerialPortActiveProbe(nullptr); }

static void test_an_unstarted_serial_port_is_never_touched(void) {
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);
    transportsUpdate();
    TEST_ASSERT_EQUAL_INT(0, s1.touches);
    TEST_ASSERT_NULL(transportGet(TRANSPORT_SERIAL1));
    msg.debug.print("x");                          // the log on an unstarted port: nothing
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_SERIAL2, false);
    msg.debug.print("x");
    TEST_ASSERT_EQUAL_INT(0, s2.touches);

    g_portActive[1] = true;                        // BUS SERIAL 1 ENABLE: begin() ran
    transportsUpdate();
    TEST_ASSERT_GREATER_THAN_INT(0, s1.touches);
    TEST_ASSERT_EQUAL_PTR(&s1, transportGet(TRANSPORT_SERIAL1));
}

static void test_the_console_falls_back_to_usb_and_the_log_never_does(void) {
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);   // a port that is off
    TEST_ASSERT_EQUAL_PTR(&usb, consoleSink(false));
    TEST_ASSERT_NULL(consoleSink(true));
    TEST_ASSERT_TRUE(consoleOnUsbFallback());

    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_USB_SERIAL, true);  // a usable secondary carries it
    TEST_ASSERT_NULL(consoleSink(false));
    TEST_ASSERT_EQUAL_PTR(&usb, consoleSink(true));
    TEST_ASSERT_FALSE(consoleOnUsbFallback());

    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);
    g_portActive[1] = true;                                        // started: no fallback
    TEST_ASSERT_EQUAL_PTR(&s1, consoleSink(false));

    g_portActive[1] = false;
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_SERIAL1, false);
    TEST_ASSERT_NULL(logSink(false));
}

static void test_a_reply_goes_to_its_source_and_unsolicited_output_to_both(void) {
    g_portActive[1] = true;
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, true);     // console USB + SERIAL1
    consoleSetActive(&s1);                         // a command from SERIAL1 is running
    msg.control.print("R");
    TEST_ASSERT_EQUAL_STRING("R", s1.out);         // answered on the sender
    TEST_ASSERT_EQUAL_STRING("", usb.out);         // not on the other console port
    consoleSetActive(nullptr);
    TEST_ASSERT_NULL(consoleActiveSource());

    s1.clearOut();
    msg.control.print("A");                        // unsolicited: primary and secondary
    TEST_ASSERT_EQUAL_STRING("A", usb.out);
    TEST_ASSERT_EQUAL_STRING("A", s1.out);
}

static void test_console_transport_is_what_the_console_reads(void) {
    TEST_ASSERT_EQUAL_PTR(&usb, consoleTransport(false));
    TEST_ASSERT_NULL(consoleTransport(true));
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);   // a port that is off
    TEST_ASSERT_EQUAL_PTR(&usb, consoleTransport(false));         // USB stands in (#558)
    g_portActive[1] = true;
    TEST_ASSERT_EQUAL_PTR(&s1, consoleTransport(false));
}

static void test_a_transport_can_be_registered_and_replaced(void) {
    FakeTransport other;
    TEST_ASSERT_EQUAL_PTR(&usb, transportRegistered(TRANSPORT_USB_SERIAL));
    transportRegister(TRANSPORT_USB_SERIAL, &other);
    TEST_ASSERT_EQUAL_PTR(&other, transportGet(TRANSPORT_USB_SERIAL));
    transportRegister(TRANSPORT_NONE, &other);                      // ignored
    transportRegister(TRANSPORT_SERIAL3, nullptr);                  // ignored
    TEST_ASSERT_NULL(transportRegistered(TRANSPORT_NONE));
    TEST_ASSERT_NULL(transportRegistered(TRANSPORT_SERIAL3));
}

static void test_the_log_multicasts_to_its_secondary(void) {
    g_portActive[2] = true;
    planeSetUnchecked(PLANE_DEBUG, TRANSPORT_SERIAL2, true);
    msg.debug.print("L");
    TEST_ASSERT_EQUAL_STRING("L", usb.out);
    TEST_ASSERT_EQUAL_STRING("L", s2.out);
    TEST_ASSERT_EQUAL_STRING("", s1.out);
}

static void test_the_setters_refuse_an_unstarted_port_and_the_apply_path_does_not(void) {
    TEST_ASSERT_FALSE(planeSetTransport(PLANE_CONTROL, TRANSPORT_SERIAL1, false));
    TEST_ASSERT_FALSE(planeSetTransport(PLANE_DEBUG, TRANSPORT_SERIAL7, false));   // not registered
    TEST_ASSERT_EQUAL(TRANSPORT_USB_SERIAL, planeTransportId(PLANE_CONTROL, false));
    g_portActive[1] = true;
    TEST_ASSERT_TRUE(planeSetTransport(PLANE_CONTROL, TRANSPORT_SERIAL1, true));
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, planeTransportId(PLANE_CONTROL, true));

    // A stored choice survives its port being down at load time
    g_portActive[1] = false;
    systemConfig.console.primary = TRANSPORT_SERIAL1;
    systemConfig.console.secondary = TRANSPORT_NONE;
    systemConfig.log.primary = TRANSPORT_SERIAL2;
    systemConfig.log.secondary = TRANSPORT_NONE;
    routingApplyConfig();
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, planeTransportId(PLANE_CONTROL, false));
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL2, planeTransportId(PLANE_DEBUG, false));
    systemConfig.console.primary = 0;
    routingCaptureConfig();
    TEST_ASSERT_EQUAL(TRANSPORT_SERIAL1, systemConfig.console.primary);
}

static void test_msg_with_no_sink_writes_nowhere(void) {
    setMessageSinkResolver(PLANE_CONTROL, nullptr);
    setMessageSinkResolver(PLANE_DEBUG, nullptr);
    TEST_ASSERT_EQUAL(0, msg.control.print("boot"));
    msg.debug.println("boot");
    TEST_ASSERT_EQUAL_INT(0, usb.touches);
}

static void test_a_disconnected_primary_skips_the_secondary(void) {
    g_portActive[1] = true;
    planeSetUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, true);
    usb.state = TRANSPORT_DISCONNECTED;
    msg.control.print("A");
    TEST_ASSERT_EQUAL_STRING("", usb.out);         // not written to the disconnected primary
    TEST_ASSERT_EQUAL_STRING("", s1.out);          // nor the secondary: today's behaviour, kept
}

static void test_list_transports_prints_what_the_router_did(void) {
    g_portActive[1] = true;                        // Serial2 stays off: not listed
    s1.state = TRANSPORT_CONNECTING;
    transportListAvailable();
    TEST_ASSERT_EQUAL_STRING("=== Available Transports ===\r\n  USB_SERIAL - Connected\r\n  SERIAL1 - Connecting\r\n", usb.out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_an_unstarted_serial_port_is_never_touched);
    RUN_TEST(test_the_console_falls_back_to_usb_and_the_log_never_does);
    RUN_TEST(test_a_reply_goes_to_its_source_and_unsolicited_output_to_both);
    RUN_TEST(test_console_transport_is_what_the_console_reads);
    RUN_TEST(test_a_transport_can_be_registered_and_replaced);
    RUN_TEST(test_the_log_multicasts_to_its_secondary);
    RUN_TEST(test_the_setters_refuse_an_unstarted_port_and_the_apply_path_does_not);
    RUN_TEST(test_msg_with_no_sink_writes_nowhere);
    RUN_TEST(test_a_disconnected_primary_skips_the_secondary);
    RUN_TEST(test_list_transports_prints_what_the_router_did);
    return UNITY_END();
}
