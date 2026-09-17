// firmware_stubs.h — the firmware symbols an input_manager/json_config host test
// needs from the rest of the image.
//
// DEFINITIONS, not declarations: include this from exactly one translation unit
// per test binary, AFTER the firmware .cpp files under test.
//
// What it supplies:
//   - a null-transport message router, so every msg.control/msg.debug print is a
//     no-op instead of a link error;
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

MessageRouter router;
MessageAPI msg;
SystemConfig systemConfig;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

static bool g_pinReserved[256];
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
    }
    return false;
}
int8_t analogIndexForPin(uint8_t) { return -1; }

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
float rulesRuleWarnEdge(const Rule&, bool) { return NAN; }
#endif

#define STUB_READ(fn) void fn(Input*) {}
STUB_READ(readMAX6675)          STUB_READ(readMAX31855)
STUB_READ(readThermistorSteinhart) STUB_READ(readThermistorLookup)
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

#endif // TEST_FIRMWARE_STUBS_H
