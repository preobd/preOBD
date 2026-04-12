/*
 * output_elm327.cpp — ELM327 AT command emulator output module
 *
 * See output_elm327.h for architecture notes and configuration commands.
 */

#include "../config.h"

#ifdef ENABLE_ELM327

#include "output_elm327.h"
#include "../lib/obd_query.h"
#include "../lib/system_config.h"
#include "../lib/serial_manager.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../lib/generated/registry_enums.h"  // APP_PRIMARY_BATTERY, APP_AUXILIARY_BATTERY

// ===== SINGLETON =====

ELM327Output elm327Output;

// ===== OUTPUT MODULE FREE FUNCTIONS (called by output_manager) =====

void initELM327() {
    uint8_t port = systemConfig.buses.elm327_serial_port;
    if (port == 0xFF) return;

    Stream* ser = getSerialPort(port);
    if (ser && isSerialPortActive(port)) {
        elm327Output.setSerial(ser);
        elm327Output.begin();
        msg.debug.info(TAG_ELM327, "ELM327 emulator active on Serial%d", port);
    } else {
        msg.debug.warn(TAG_ELM327, "ELM327: Serial%d not active at boot", port);
    }
}

void sendELM327(Input* /*ptr*/) {
    // ELM327 is pull-based — phone sends queries, we respond.
    // No push logic needed here.
}

void updateELM327() {
    elm327Output.update();
}

// ===== ELM327Output IMPLEMENTATION =====

ELM327Output::ELM327Output()
    : _serial(nullptr), _active(false), _connected(false), _rxLen(0)
{
    _resetState();
}

void ELM327Output::setSerial(Stream* serial) {
    _serial = serial;
}

void ELM327Output::begin() {
    if (!_serial) return;
    _active = true;
    _rxLen = 0;
    _resetState();
    _connected = false;
}

void ELM327Output::end() {
    _active = false;
    _serial = nullptr;
}

void ELM327Output::_resetState() {
    _echoOn      = true;
    _headersOn   = false;
    _linefeedsOn = false;
    _spacesOn    = false;
}

// ===== MAIN LOOP =====

void ELM327Output::update() {
    if (!_active || !_serial) return;

    while (_serial->available()) {
        char c = (char)_serial->read();

        if (c == '\r') {
            _rxBuf[_rxLen] = '\0';
            _processLine(_rxBuf);
            _rxLen = 0;
        } else if (c == '\n') {
            // Discard — ELM327 uses CR line endings
        } else if (_rxLen < ELM327_RX_BUF_SIZE - 1) {
            _rxBuf[_rxLen++] = c;
        }
        // Silently drop bytes when buffer is full (oversized input)
    }
}

// ===== LINE PROCESSING =====

// Strip leading/trailing whitespace in place; returns pointer to first non-space.
static const char* trimLine(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Return true if every character in s is a hex digit or space.
static bool isHexQuery(const char* s) {
    if (!*s) return false;
    for (const char* p = s; *p; p++) {
        char c = *p;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F') || c == ' ')) {
            return false;
        }
    }
    return true;
}

// Uppercase a string in place.
static void toUpperInPlace(char* s) {
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
    }
}

// Copy src to dst stripping spaces, then uppercase.
static void stripSpacesUpper(const char* src, char* dst, size_t dstSize) {
    size_t n = 0;
    for (; *src && n < dstSize - 1; src++) {
        if (*src != ' ' && *src != '\t') dst[n++] = *src;
    }
    dst[n] = '\0';
    toUpperInPlace(dst);
}

void ELM327Output::_processLine(const char* line) {
    line = trimLine(line);

    if (line[0] == '\0') {
        // Empty line: send prompt if we've seen at least one command
        if (_connected) _sendPrompt();
        return;
    }

    // Check for AT command (case-insensitive)
    bool isAT = ((line[0] == 'A' || line[0] == 'a') &&
                 (line[1] == 'T' || line[1] == 't'));

    if (isAT) {
        _connected = true;
        char cmd[ELM327_RX_BUF_SIZE];
        stripSpacesUpper(line, cmd, sizeof(cmd));

        // Echo the original received line before responding
        // Note: real ELM327 echoes char-by-char; we do line-level echo.
        // Phone apps send ATE0 immediately after reset so the divergence
        // is almost never observable.
        if (_echoOn) {
            _serial->print(line);
            _serial->print('\r');
        }

        _handleATCommand(cmd);
        return;
    }

    // Check for hex OBD query
    if (isHexQuery(line)) {
        _connected = true;
        char upper[ELM327_RX_BUF_SIZE];
        // Strip spaces, uppercase
        size_t n = 0;
        for (const char* p = line; *p && n < sizeof(upper) - 1; p++) {
            if (*p != ' ') upper[n++] = *p;
        }
        upper[n] = '\0';
        toUpperInPlace(upper);

        if (_echoOn) {
            _serial->print(line);
            _serial->print('\r');
        }

        _handleOBDQuery(upper);
        return;
    }

    // Unknown input
    _sendString("?\r\r>");
}

// ===== AT COMMAND HANDLER =====

void ELM327Output::_handleATCommand(const char* cmd) {
    // cmd is space-stripped, uppercased, starts with "AT"
    const char* a = cmd + 2;  // skip "AT"

    // Reset commands
    if (strcmp(a, "Z") == 0 || strcmp(a, "WS") == 0) {
        _resetState();
        _sendString("\r\r" ELM327_VERSION_STR "\r\r>");
        return;
    }

    // Echo
    if (strcmp(a, "E0") == 0) { _echoOn = false; _sendString("OK\r\r>"); return; }
    if (strcmp(a, "E1") == 0) { _echoOn = true;  _sendString("OK\r\r>"); return; }

    // Headers
    if (strcmp(a, "H0") == 0) { _headersOn = false; _sendString("OK\r\r>"); return; }
    if (strcmp(a, "H1") == 0) { _headersOn = true;  _sendString("OK\r\r>"); return; }

    // Linefeeds
    if (strcmp(a, "L0") == 0) { _linefeedsOn = false; _sendString("OK\r\r>"); return; }
    if (strcmp(a, "L1") == 0) { _linefeedsOn = true;  _sendString("OK\r\r>"); return; }

    // Spaces
    if (strcmp(a, "S0") == 0) { _spacesOn = false; _sendString("OK\r\r>"); return; }
    if (strcmp(a, "S1") == 0) { _spacesOn = true;  _sendString("OK\r\r>"); return; }

    // Set protocol (accept any, ignore — we're always CAN ISO 15765-4)
    if (strncmp(a, "SP", 2) == 0) { _sendString("OK\r\r>"); return; }

    // Describe protocol — check ATDPN before ATDP (prefix match order)
    if (strcmp(a, "DPN") == 0) { _sendString("A6\r\r>"); return; }
    if (strcmp(a, "DP")  == 0) { _sendString("AUTO, ISO 15765-4 (CAN 11/500)\r\r>"); return; }

    // Version / identity
    if (strcmp(a, "I")   == 0) { _sendString(ELM327_VERSION_STR "\r\r>"); return; }
    if (strcmp(a, "@1")  == 0) { _sendString("preOBD\r\r>"); return; }
    if (strcmp(a, "@2")  == 0) { _sendString("OK\r\r>"); return; }

    // Supply voltage — live from PRIMARY_BATTERY input if available, else fixed
    if (strcmp(a, "RV") == 0) {
        char vbuf[16];
        bool found = false;
        for (uint8_t i = 0; i < MAX_INPUTS && !found; i++) {
            if (inputs[i].flags.isEnabled &&
                (inputs[i].applicationIndex == APP_PRIMARY_BATTERY ||
                 inputs[i].applicationIndex == APP_AUXILIARY_BATTERY) &&
                !isnan(inputs[i].value))
            {
                // Format as "12.3V"
                int whole = (int)inputs[i].value;
                int frac  = (int)((inputs[i].value - whole) * 10 + 0.5f);
                if (frac >= 10) { whole++; frac = 0; }
                snprintf(vbuf, sizeof(vbuf), "%d.%dV\r\r>", whole, frac);
                found = true;
            }
        }
        if (!found) snprintf(vbuf, sizeof(vbuf), "12.0V\r\r>");
        _sendString(vbuf);
        return;
    }

    // Timeout / adaptive timing (accept and ignore)
    if (strncmp(a, "ST", 2) == 0) { _sendString("OK\r\r>"); return; }
    if (strncmp(a, "AT", 2) == 0 && a[2] >= '0' && a[2] <= '2') {
        // ATAT0, ATAT1, ATAT2
        _sendString("OK\r\r>"); return;
    }

    // Miscellaneous no-ops
    if (strcmp(a, "AR") == 0) { _sendString("OK\r\r>"); return; }  // Allow long messages
    if (strcmp(a, "PC") == 0) { _sendString("OK\r\r>"); return; }  // Protocol close

    // Monitor all — not supported
    if (strcmp(a, "MA") == 0) { _sendString("?\r\r>"); return; }

    // Unknown AT command
    _sendString("?\r\r>");
}

// ===== OBD QUERY HANDLER =====

static uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void ELM327Output::_handleOBDQuery(const char* hex) {
    // Expect at least 4 hex chars: mode (2) + pid (2)
    size_t len = strlen(hex);
    if (len < 4) { _sendString("?\r\r>"); return; }

    uint8_t mode = (hexNibble(hex[0]) << 4) | hexNibble(hex[1]);
    uint8_t pid  = (hexNibble(hex[2]) << 4) | hexNibble(hex[3]);

    uint8_t responseBytes[7];
    uint8_t responseLen = 0;

    if (!obdQuery_resolve(mode, pid, responseBytes, &responseLen)) {
        _sendString("NO DATA\r\r>");
        return;
    }

    // Format response bytes as ASCII hex
    // Buffer: headers (up to 8) + spaces + data + \r\r> + NUL
    char out[48];
    _formatDataResponse(responseBytes, responseLen, out, sizeof(out));
    _sendString(out);
}

// ===== RESPONSE FORMATTING =====

void ELM327Output::_formatDataResponse(const uint8_t* bytes, uint8_t len,
                                        char* out, size_t outSize)
{
    size_t pos = 0;

    auto appendHex = [&](uint8_t b) {
        static const char hex[] = "0123456789ABCDEF";
        if (pos + 2 < outSize) {
            out[pos++] = hex[b >> 4];
            out[pos++] = hex[b & 0x0F];
        }
    };
    auto appendChar = [&](char c) {
        if (pos + 1 < outSize) out[pos++] = c;
    };
    auto appendStr = [&](const char* s) {
        while (*s && pos + 1 < outSize) out[pos++] = *s++;
    };

    if (_headersOn) {
        // Fixed ECU response header 7E8, followed by length byte
        if (_spacesOn) {
            appendStr("7E8 ");
            appendHex(len);
            appendChar(' ');
        } else {
            appendStr("7E8");
            appendHex(len);
        }
    }

    for (uint8_t i = 0; i < len; i++) {
        if (_spacesOn && i > 0) appendChar(' ');
        appendHex(bytes[i]);
    }

    appendStr("\r\r>");
    out[pos] = '\0';
}

// ===== OUTPUT HELPERS =====

void ELM327Output::_sendString(const char* str) {
    if (!_serial) return;
    if (_linefeedsOn) {
        // Insert \n after every \r
        for (; *str; str++) {
            _serial->print(*str);
            if (*str == '\r') _serial->print('\n');
        }
    } else {
        _serial->print(str);
    }
}

void ELM327Output::_sendPrompt() {
    _sendString(">");
}

#endif // ENABLE_ELM327
