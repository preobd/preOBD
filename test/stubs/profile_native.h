// profile_native.h — synthetic board profile for the native (host) test build.
//
// Real firmware builds inject boards/<board>/board.h (+ boards/resolve.h) via
// `-include`; the native test env does the same with this file (see
// [env:native] build_flags). It stands in for BOTH, so it declares the
// *resolved* INCLUDE_* flags directly rather than the SUPPORTS_* capabilities
// resolve.h would derive them from.
// Values mirror a Teensy-class build; MAX_INPUTS is kept small to keep test
// arrays cheap.
#ifndef PROFILE_NATIVE_H
#define PROFILE_NATIVE_H

#define MAX_INPUTS              16
#define MAX_RELAYS              2
#define MAX_PID_ENTRIES         64
#define MAX_PIN_REGISTRY        64
#define NUM_SERIAL_PORTS        2

#define INCLUDE_ALARMS           1
#define INCLUDE_FRAMED_TRANSPORT 1
#define INCLUDE_RELAY_OUTPUT     1
#define INCLUDE_ELM327           1
#define INCLUDE_SELFTEST         1
#define SUPPORTS_JSON_CONFIG    1  // host round-trip of control_json leaf converters

#endif // PROFILE_NATIVE_H
