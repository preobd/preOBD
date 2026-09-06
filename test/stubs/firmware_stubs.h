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
int8_t analogIndexForPin(uint8_t) { return -1; }

Input* requireInput(uint8_t pin) { return getInputByPin(pin); }

// Control engine: the JSON exporter reads an input's alarm Rule back out of the
// live table. Empty table + "no rule for this input" keeps that path inert.
#if INCLUDE_CONTROL_ENGINE
Rule rules[MAX_RULES];
int controlAlarmRuleSlotForInput(uint8_t) { return -1; }
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
