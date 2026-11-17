/*
 * output_elm327.h — ELM327 AT command emulator output module
 *
 * Implements a software ELM327 over any hardware serial port, allowing
 * OBD-II apps (Torque Pro, OBD Fusion, Car Scanner) to connect directly
 * to preOBD via a BLE UART module (e.g. HM-10) without an ELM327 adapter.
 *
 * Architecture: output module parallel to output_can.cpp.
 *   output_can.cpp  — attached to a CAN bus, responds to OBD-II frames
 *   output_elm327.cpp — bound to a serial port, responds to ELM327 ASCII queries
 *
 * Configuration (output id 4, #612):
 *   BUS SERIAL 2 ENABLE 115200       # bring up the port
 *   OUTPUT ADD ELM327 SERIAL2        # bind ELM327 to it (exclusive), at once
 *   SAVE
 * ELM327 over BLE (output id 5) is a second instance in elm327_framed.cpp.
 *
 * The port is exclusive to ELM327 — it is not available for
 * the console and the log while assigned.
 */

#ifndef OUTPUT_ELM327_H
#define OUTPUT_ELM327_H

#if INCLUDE_ELM327

#include <Arduino.h>
#include "../../inputs/input.h"

#define ELM327_RX_BUF_SIZE  32
#define ELM327_VERSION_STR  "ELM327 v1.5"

class ELM327Output {
public:
    // routeId: whose route bit decides what it answers (obd_query.h, #612):
    // its output's id, set when the output starts (ids are assigned by OUTPUT ADD)
    explicit ELM327Output(uint8_t routeId);
    void setRouteId(uint8_t id) { _routeId = id; }

    // Assign the stream it answers on: initELM327() for the serial port,
    // Elm327Framed::init() for BLE. Pass nullptr to detach.
    void setSerial(Stream* serial);

    void begin();   // Start listening
    void end();     // Stop and detach

    // Called every loop() by the output manager
    void update();

    // No-op: ELM327 is pull-based (responds to phone queries, not push)
    void send(Input* /*ptr*/) {}

    bool isActive() const { return _active; }

private:
    uint8_t  _routeId;
    Stream*  _serial;
    bool     _active;
    bool     _connected;    // true after first AT command received

    char     _rxBuf[ELM327_RX_BUF_SIZE];
    uint8_t  _rxLen;

    // Session state — reset by ATZ / ATWS
    bool _echoOn;       // ATE0/ATE1, default on
    bool _headersOn;    // ATH0/ATH1, default off
    bool _linefeedsOn;  // ATL0/ATL1, default off
    bool _spacesOn;     // ATS0/ATS1, default on like a real ELM327
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

// Unbind from its serial port: the output disabled at runtime (#610)
void stopELM327();
// After an enable, print why ELM327 output <id> isn't answering yet (port not up)
void noteELM327PortState(uint8_t id);

#endif // INCLUDE_ELM327
#endif // OUTPUT_ELM327_H
