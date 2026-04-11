/*
 * output_elm327.h — ELM327 AT command emulator output module
 *
 * Implements a software ELM327 over any hardware serial port, allowing
 * OBD-II apps (Torque Pro, OBD Fusion, Car Scanner) to connect directly
 * to preOBD via a BLE UART module (e.g. HM-10) without an ELM327 adapter.
 *
 * Architecture: output module parallel to output_can.cpp.
 *   output_can.cpp  — owns a CAN bus, responds to OBD-II frames
 *   output_elm327.cpp — owns a serial port, responds to ELM327 ASCII queries
 *
 * Configuration (mirrors BUS CAN OUTPUT pattern):
 *   BUS SERIAL 2 ENABLE 115200       # bring up the port
 *   BUS SERIAL 2 ELM327 ENABLE       # assign ELM327 role (exclusive)
 *   SAVE
 *
 * The port is exclusive to ELM327 — it is not available for
 * MessageRouter CONTROL/DATA/DEBUG plane routing while assigned.
 */

#ifndef OUTPUT_ELM327_H
#define OUTPUT_ELM327_H

#ifdef ENABLE_ELM327_OUTPUT

#include <Arduino.h>
#include "../inputs/input.h"

#define ELM327_RX_BUF_SIZE  32
#define ELM327_VERSION_STR  "ELM327 v1.5"

class ELM327Output {
public:
    ELM327Output();

    // Assign serial port. Called by BUS SERIAL n ELM327 ENABLE command
    // and by initELM327() on boot. Pass nullptr to detach.
    void setSerial(Stream* serial);

    void begin();   // Start listening
    void end();     // Stop and detach

    // Called every loop() by the output manager
    void update();

    // No-op: ELM327 is pull-based (responds to phone queries, not push)
    void send(Input* /*ptr*/) {}

    bool isActive() const { return _active; }

private:
    Stream*  _serial;
    bool     _active;
    bool     _connected;    // true after first AT command received

    char     _rxBuf[ELM327_RX_BUF_SIZE];
    uint8_t  _rxLen;

    // Session state — reset by ATZ / ATWS
    bool _echoOn;       // ATE0/ATE1, default on
    bool _headersOn;    // ATH0/ATH1, default off
    bool _linefeedsOn;  // ATL0/ATL1, default off
    bool _spacesOn;     // ATS0/ATS1, default off
                        // (off by default: HM-10 20-byte MTU is tight with spaces)

    void _resetState();
    void _processLine(const char* line);
    void _handleATCommand(const char* cmd);   // cmd: full "ATxxx" uppercased, no spaces
    void _handleOBDQuery(const char* hex);    // hex: e.g. "010C" or "0100"
    void _sendString(const char* str);
    void _sendPrompt();
    void _formatDataResponse(const uint8_t* bytes, uint8_t len, char* out, size_t outSize);
};

extern ELM327Output elm327Output;

#endif // ENABLE_ELM327_OUTPUT
#endif // OUTPUT_ELM327_H
