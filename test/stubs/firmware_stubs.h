// firmware_stubs.h — the firmware symbols an input_manager/json_config host test
// needs from the rest of the image.
//
// DEFINITIONS, not declarations: include this from exactly one translation unit
// per test binary, AFTER the firmware .cpp files under test.
//
// What it supplies:
//   - a null msg sink (messageSink returns nullptr), so every msg.control/msg.debug print is a
//     no-op instead of a link error; a test may point g_testControlSink at a
//     capturing transport to check what the console was told;
//   - a test-settable isPinAvailable() (g_pinReserved[]) — the lever the
//     "reserved by a bus" refusal hangs off;
//   - a matching pinsConflict() stub (pin_owner.h): input_manager.cpp routes
//     its registry check through the one predicate (#384), but this test
//     binary doesn't pull in the real pin registry/ranking, so this stand-in
//     answers off the same g_pinReserved[] lever, ignoring rank;
//   - no-op bodies for the sensor read/init functions the SENSOR_LIBRARY tables
//     point at (referenced as data, so -dead_strip can't drop them).

#ifndef TEST_FIRMWARE_STUBS_H
#define TEST_FIRMWARE_STUBS_H

MessageAPI msg;
SystemConfig systemConfig;
LogFilter::LogFilter() {}
TransportInterface* g_testControlSink = nullptr;
TransportInterface* messageSink(MessagePlane plane, bool secondary) {
    return (plane == PLANE_CONTROL && !secondary) ? g_testControlSink : nullptr;
}
// JSON IMPORT applies an imported logFilter to the live filter (#561); system_config.cpp
// is not in these binaries, so count the calls instead.
int g_applyLogFilterCalls = 0;
void applyLogFilterConfig() { g_applyLogFilterCalls++; }
// ...and the live outputs (#610); output_manager.cpp is not in them either.
int g_syncOutputsCalls = 0;
void syncOutputsToConfig() { g_syncOutputsCalls++; }
// OUTPUT LIST JSON's "running" (#632); nothing runs in these binaries
bool outputLiveById(uint8_t) { return false; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

static bool g_pinReserved[256];
static bool g_pinAction[256];   // registry entries that are Action targets (#567)
bool isPinAvailable(uint8_t pin) { return !g_pinReserved[pin]; }
// validateInputConfig() (#385) routes its WHOLE conflict check — registry AND
// other configured inputs — through pinsConflict(). This stand-in reproduces
// both halves rather than just the registry one: g_pinReserved[] stands in for
// the registry/bus half, and inputHoldsPin() is the REAL production probe
// (input_manager.cpp is fully compiled into this test binary), self-exclusion
// included — so the squatter tests exercise the actual #385 fix, not a mock of
// it.
bool pinsConflict(const uint8_t* pins, uint8_t count, uint8_t /*rank*/, const char* /*owner*/) {
    for (uint8_t i = 0; i < count; i++) {
        if (pins[i] == 0xFF) continue;
        if (inputHoldsPin(pins[i])) return true;
        if (g_pinReserved[pins[i]]) return true;
        if (g_pinAction[pins[i]]) return true;   // an Action target's claim (#567)
    }
    return false;
}
// The registry walk a JSON import's pin check makes (#567): each reserved pin
// is an entry named "TEST BUS"; g_pinAction[] marks one as an Action target's.
static PinUsage g_stubUsage;
uint8_t getPinRegistrySize() {
    uint8_t n = 0;
    for (int p = 0; p < 256; p++) n += (g_pinReserved[p] || g_pinAction[p]) ? 1 : 0;
    return n;
}
const PinUsage* getPinUsageByIndex(uint8_t index) {
    for (int p = 0; p < 256; p++) {
        if (!(g_pinReserved[p] || g_pinAction[p])) continue;
        if (index-- != 0) continue;
        g_stubUsage.pin = (uint8_t)p;
        g_stubUsage.type = g_pinAction[p] ? PIN_OUTPUT : PIN_RESERVED;
        g_stubUsage.description = g_pinAction[p] ? "Action output" : "TEST BUS";
        return &g_stubUsage;
    }
    return nullptr;
}
// A staged bus selection (bus_manager.cpp is not in these binaries): I2C bus 0
// holds pins 18/19, as on a Teensy.
bool busConfigHoldsPin(const BusConfig& buses, uint8_t pin, const char*, const char** descOut, bool) {
    if (buses.active_i2c == 0 && (pin == 18 || pin == 19)) {
        if (descOut) *descOut = "I2C0";
        return true;
    }
    return false;
}
// storedBusHoldsPin(): the current selection, unless the test marks it refused
// this boot (#564), as bus_manager.cpp skips one
static bool g_stubBusRefused = false;
bool storedBusHoldsPin(uint8_t pin, const char* excludeOwner, const char** descOut) {
    if (g_stubBusRefused) return false;
    return busConfigHoldsPin(systemConfig.buses, pin, excludeOwner, descOut, true);
}
static uint8_t g_stubRegistryCapacity = 64;
uint8_t getPinRegistryCapacity() { return g_stubRegistryCapacity; }
#if INCLUDE_RULES_ENGINE
bool rulesIsActionPinEntry(const PinUsage* u) { return u != nullptr && g_pinAction[u->pin]; }
// rulesActionsQuiesce(): switches off and releases every action pin before a
// table replacement (#648). The stub counts calls and frees the stub action pins.
int g_quiesceCalls = 0;
void rulesActionsQuiesce() {
    g_quiesceCalls++;
    memset(g_pinAction, 0, sizeof(g_pinAction));
}
#endif

// Board pins and name (buses/pinctrl/board_pins.cpp, #570): a stand-in board named
// NATIVE. By default no pin is analog and every pin exists; a test sets these
// to stand in for a real board (JSON IMPORT's board check, #567).
static uint8_t g_stubDigitalPinMax = 0xFF;  // highest digital pin
static uint8_t g_stubAnalogFirst = 0xFF;    // A0's pin; 0xFF = no analog pins
static uint8_t g_stubAnalogCount = 0;
int8_t analogIndexForPin(uint8_t pin) {
    if (g_stubAnalogFirst == 0xFF || pin < g_stubAnalogFirst ||
        pin >= g_stubAnalogFirst + g_stubAnalogCount) return -1;
    return (int8_t)(pin - g_stubAnalogFirst);
}
bool boardHasPin(uint8_t pin) { return pin <= g_stubDigitalPinMax || analogIndexForPin(pin) >= 0; }
const char* boardName() { return "NATIVE"; }

// requireInput() is no longer stubbed: it moved into input_manager.cpp (#411),
// which every TU including this header already amalgamates, so the tests now
// exercise the real implementation (error prints included).

// setInputSensor() (#377) checks getActive{I2C,SPI}() and, if a needed bus is
// down, calls reportNo{I2C,SPI}Bus() to print guidance. Neither test here
// exercises bus acquisition (see test_bus_wiring for that), so "no bus is ever
// up, and reporting it is a no-op" is the right stand-in — bus_manager.cpp
// itself is not part of this test binary.
TwoWire* getActiveI2C() { return nullptr; }
SPIClass* getActiveSPI() { return nullptr; }
bool reportNoI2CBus() { return false; }
bool reportNoSPIBus() { return false; }

// Control engine: the JSON exporter reads an input's alarm Rule back out of the
// live table. Empty table + "no rule for this input" keeps that path inert.
#if INCLUDE_RULES_ENGINE
Rule rules[MAX_RULES];
int rulesAlarmRuleSlotForInput(uint8_t) { return -1; }
// json_config.cpp's alarm-facade export (#425, slice B) reads the warn tier's
// effective edge via the real engine function; rulesAlarmRuleSlotForInput()
// above always answers "no rule", so this is never actually invoked at
// runtime, but the call still needs a link-time definition.
#ifndef TEST_LINKS_RULES_ENGINE   // a test that compiles rules_engine.cpp has the real one
float rulesRuleWarnEdge(const Rule&, bool) { return NAN; }
#endif
#endif

#define STUB_READ(fn) void fn(Input*) {}
STUB_READ(readMAX6675)          STUB_READ(readMAX31855)
STUB_READ(readThermistorSteinhart) STUB_READ(readThermistorLookup) STUB_READ(readThermistorBeta)
STUB_READ(readLinearSensor)     STUB_READ(readPressureTable)
STUB_READ(readPressureTableDescending) STUB_READ(readPressurePolynomial)
STUB_READ(readVoltageDivider)   STUB_READ(readLevelTable)
STUB_READ(readDigitalFloatSwitch) STUB_READ(readHallSpeed)
STUB_READ(readWPhaseRPM)        STUB_READ(readBME280Temp)
STUB_READ(readBME280Pressure)   STUB_READ(readBME280Humidity)
STUB_READ(readBME280Elevation)
STUB_READ(initThermocoupleCS)   STUB_READ(initFloatSwitch)
STUB_READ(initHallSpeed)        STUB_READ(initWPhaseRPM)
STUB_READ(initBME280)
#undef STUB_READ

// releaseSensorPin (#652): the real one is in sensor_read.cpp beside every
// driver. This records which pins let go, in order, for a test to check.
static uint8_t g_releasedPins[32];
static uint8_t g_releasedCount = 0;
void releaseSensorPin(Input* in) {
    if (g_releasedCount < sizeof(g_releasedPins)) g_releasedPins[g_releasedCount++] = in->pin;
}

#endif // TEST_FIRMWARE_STUBS_H
