// Native unit test: the router never touches a hardware serial transport
// whose port was not started.
//
// On Teensy 4 a UART that never ran begin() has its clock gated, and reading
// its registers (HardwareSerial::available() reads WATER) is a hard fault.
// BUS SERIAL 1 DISABLE + SAVE on a FRAMED Serial1 left a board that faulted
// on every boot, whatever firmware it ran: update() pumped the framed
// transport every loop. This pins update() and getTransport() to the
// serial-port-active probe.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/core/message_router.cpp"

MessageAPI msg;
LogFilter::LogFilter() {}
void MessageRouter::loadConfig() {}
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// Counts every call the router makes into it.
class FakeTransport : public TransportInterface {
public:
    int touches = 0;
    size_t write(uint8_t) override { touches++; return 1; }
    size_t write(const uint8_t*, size_t n) override { touches++; return n; }
    int available() override { touches++; return 0; }
    int read() override { touches++; return -1; }
    int peek() override { touches++; return -1; }
    void flush() override { touches++; }
    const char* getName() const override { return "FAKE"; }
    uint8_t getCapabilities() const override { return CAP_HARDWARE_SERIAL; }
    TransportState getState() const override { return TRANSPORT_CONNECTED; }
    bool begin() override { return true; }
    void end() override {}
    void update() override { touches++; }
};

static bool g_portActive[9];
static bool probe(uint8_t port_id) { return port_id < 9 && g_portActive[port_id]; }

void setUp(void) {
    memset(g_portActive, 0, sizeof(g_portActive));
    setSerialPortActiveProbe(probe);
}
void tearDown(void) { setSerialPortActiveProbe(nullptr); }

static void test_update_skips_unstarted_serial_port(void) {
    MessageRouter r;
    FakeTransport s1;
    r.registerTransport(TRANSPORT_SERIAL1, &s1);
    r.setTransportUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);

    r.update();
    TEST_ASSERT_EQUAL_INT(0, s1.touches);
    TEST_ASSERT_NULL(r.getTransport(PLANE_CONTROL, true));

    g_portActive[1] = true;   // BUS SERIAL 1 ENABLE: begin() ran
    r.update();
    TEST_ASSERT_GREATER_THAN_INT(0, s1.touches);
    TEST_ASSERT_EQUAL_PTR(&s1, r.getTransport(PLANE_CONTROL, true));
}

static void test_routed_message_skips_unstarted_serial_port(void) {
    MessageRouter r;
    FakeTransport s2;
    r.registerTransport(TRANSPORT_SERIAL2, &s2);
    r.setTransportUnchecked(PLANE_DATA, TRANSPORT_SERIAL2, false);
    r.routeMessage(PLANE_DATA, "x");
    TEST_ASSERT_EQUAL_INT(0, s2.touches);
}

// A control plane whose only transport is a disabled serial port falls back
// to USB, so the CLI stays reachable; with a usable secondary it doesn't.
static void test_control_falls_back_to_usb(void) {
    MessageRouter r;
    FakeTransport usb, s1;
    r.registerTransport(TRANSPORT_USB_SERIAL, &usb);
    r.registerTransport(TRANSPORT_SERIAL1, &s1);
    r.setTransportUnchecked(PLANE_CONTROL, TRANSPORT_SERIAL1, false);
    r.setTransportUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);
    TEST_ASSERT_EQUAL_PTR(&usb, r.getTransport(PLANE_CONTROL, true));
    TEST_ASSERT_NULL(r.getTransport(PLANE_CONTROL, false));

    r.setTransportUnchecked(PLANE_CONTROL, TRANSPORT_USB_SERIAL, true);
    TEST_ASSERT_NULL(r.getTransport(PLANE_CONTROL, true));       // secondary carries it
    TEST_ASSERT_EQUAL_PTR(&usb, r.getTransport(PLANE_CONTROL, false));

    r.setTransportUnchecked(PLANE_CONTROL, TRANSPORT_NONE, true);
    g_portActive[1] = true;                                       // port started: no fallback
    TEST_ASSERT_EQUAL_PTR(&s1, r.getTransport(PLANE_CONTROL, true));

    // Data and debug planes never fall back.
    g_portActive[1] = false;
    r.setTransportUnchecked(PLANE_DATA, TRANSPORT_SERIAL1, false);
    TEST_ASSERT_NULL(r.getTransport(PLANE_DATA, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_update_skips_unstarted_serial_port);
    RUN_TEST(test_routed_message_skips_unstarted_serial_port);
    RUN_TEST(test_control_falls_back_to_usb);
    return UNITY_END();
}
