/*
 * output_format.h - Shared CSV field formatting for the recording outputs
 *
 * output_serial.cpp (serial CSV) and output_sdlog.cpp (SD CSV) render the same
 * two fields — a sensor reading and its unit symbol — and had drifted apart:
 * serial printed "ERROR" for a faulted sensor while SD dropped the row entirely,
 * and serial cast the PROGMEM unit pointer to __FlashStringHelper while SD passed
 * it as a raw const char* (garbage on a harvard-architecture target; latent only
 * because mega2560 vetoes SD logging).
 *
 * Both helpers are pure — no Input, no File, no msg — so they link and test on
 * the host, and there is exactly one definition of "how a reading looks in CSV".
 *
 * Callers convert to display units first:
 *     char v[VALUE_FIELD_LEN];
 *     formatValueField(v, sizeof(v), convertFromBaseUnits(in->value, in->unitsIndex));
 */

#ifndef OUTPUT_FORMAT_H
#define OUTPUT_FORMAT_H

#include <Arduino.h>
#include <math.h>
#include <string.h>

// Buffer sizes callers should use. VALUE holds "-99999999.99" + NUL with room
// to spare; UNIT holds the longest symbol in UNITS_REGISTRY ("inHg", "km/h").
#define VALUE_FIELD_LEN 16
#define UNIT_FIELD_LEN  8

// Readings outside this magnitude are not physically meaningful for any sensor
// in the library, and dtostrf() has no bounds check — a float near FLT_MAX
// would need 40+ digits and run off the end of the caller's buffer.
#define OUTPUT_FORMAT_VALUE_LIMIT 99999999.0f

// ===== SD log layout =====
//
// WIDE is one row per sample with a column per logged input; LONG is the
// original one-row-per-reading layout, kept selectable so existing scripts and
// spreadsheets built on it keep working. The two also suit different jobs: WIDE
// graphs without a pivot, LONG absorbs a changing input set without needing a
// new header.
//
// The enum lives here rather than in system_config.h because this header is
// deliberately dependency-free (no Input, no File, no msg), which keeps the
// parser host-testable; system_config.h drags EEPROM.h and bus_config.h.
// SystemConfig stores the value as a bare uint8_t.
//
// WIDE must be 0: the field reuses a formerly-reserved byte, so every existing
// EEPROM image already reads back as zero and must land on the default.
enum SdLogFormat {
    SDLOG_FORMAT_WIDE = 0,
    SDLOG_FORMAT_LONG = 1
};

// The legacy header, byte for byte. This string is the compatibility contract —
// a parser keys off these column names, so it must not drift.
#define SDLOG_LONG_HEADER "Time,Sensor,Value,Units"

/**
 * Parse an SD log format name (case-insensitive, like the rest of the CLI).
 *
 * @param token  "LONG" or "WIDE" in any case
 * @param out    Written only on success, so a rejected token cannot corrupt
 *               the caller's current setting
 * @return       false if the token is null or unrecognized
 */
inline bool parseSdLogFormat(const char* token, SdLogFormat& out) {
    if (!token) return false;
    // Plain RAM literals, not PSTR: Teensy's PSTR macro expands to a static
    // PROGMEM array, which triggers a section type conflict when it appears in
    // an inline function in a header. The cost is confined to boards that
    // actually log to SD — every caller is behind ENABLE_SD_LOGGING, so the
    // SRAM-starved mega drops these entirely.
    if (strcasecmp(token, "WIDE") == 0) { out = SDLOG_FORMAT_WIDE; return true; }
    if (strcasecmp(token, "LONG") == 0) { out = SDLOG_FORMAT_LONG; return true; }
    return false;
}

/**
 * Display name for a stored format byte. Tolerates an out-of-range value —
 * the byte comes from EEPROM, which a firmware downgrade or a corrupt image
 * could leave holding anything.
 */
inline const char* sdLogFormatName(uint8_t format) {
    return (format == SDLOG_FORMAT_LONG) ? "LONG" : "WIDE";
}

/**
 * Render a display-unit reading as its CSV value field.
 *
 * Non-finite readings (a disconnected sensor reads NAN; a divide-by-zero
 * calibration can produce INF) become "ERROR" rather than "nan"/"inf" or a
 * dropped field, so a fault is visible in the log instead of looking like a
 * gap. Gated on isfinite() rather than !isnan() so INF can't slip through as a
 * plausible reading — same rule the BLE live-value broadcaster uses.
 *
 * @param buf  Destination, at least VALUE_FIELD_LEN bytes
 * @param len  sizeof(buf)
 * @param displayValue  Reading already converted to display units
 */
inline void formatValueField(char* buf, size_t len, float displayValue) {
    if (!buf || len == 0) return;

    if (!isfinite(displayValue)) {
        strncpy(buf, "ERROR", len);
        buf[len - 1] = '\0';
        return;
    }
    // Also "ERROR", not a separate OVF token: a magnitude this large means a
    // broken calibration, which is a fault like any other, and one sentinel
    // keeps the value column trivial to parse. The guard itself still matters —
    // dtostrf has no bounds check and a float near FLT_MAX needs 40+ chars.
    if (fabsf(displayValue) > OUTPUT_FORMAT_VALUE_LIMIT) {
        strncpy(buf, "ERROR", len);
        buf[len - 1] = '\0';
        return;
    }

    // dtostrf rather than snprintf("%.2f"): AVR's snprintf omits float support
    // unless the float-printf linker stubs are pulled in, and output_serial
    // runs on mega2560. Matches transport_interface.h's float idiom.
    dtostrf(displayValue, 0, 2, buf);
    buf[len - 1] = '\0';
}

/**
 * Copy a unit symbol out of PROGMEM into a RAM buffer.
 *
 * getUnitStringByIndex() returns a flash pointer; dereferencing it directly is
 * only correct on flat-address-space targets. Always empty-terminates, so a
 * caller can test buf[0] to decide whether to emit a separator.
 *
 * @param buf  Destination, at least UNIT_FIELD_LEN bytes
 * @param len  sizeof(buf)
 * @param progmemSymbol  Result of getUnitStringByIndex()
 */
inline void formatUnitSymbol(char* buf, size_t len, const char* progmemSymbol) {
    if (!buf || len == 0) return;
    if (!progmemSymbol) {
        buf[0] = '\0';
        return;
    }
    strncpy_P(buf, progmemSymbol, len);
    buf[len - 1] = '\0';
}

#endif // OUTPUT_FORMAT_H
