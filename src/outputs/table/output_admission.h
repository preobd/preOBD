/*
 * output_admission.h - May this output run where it is asked to? (#612, §14.4)
 *
 * One validator for every door: OUTPUT ADD / <id> ENABLE / <id> TRANSPORT,
 * JSON IMPORT, and the commands that change a stream under an output
 * (CONSOLE TRANSPORT, LOG TRANSPORT, BUS SERIAL <n> DISABLE | FRAMED ENABLE).
 *
 * The rules:
 *   - the format/transport pair must be compatible (output_table.h);
 *   - one format per stream: CSV may share a stream with the console and the
 *     log; REALDASH and ELM327 need the stream to themselves; no two enabled
 *     outputs share a stream;
 *   - a framed port (the nRF link) carries the console and ELM327 over BLE,
 *     never a raw stream output (2c adds RealDash's BLE channel);
 *   - BLE (the nRF's data channel, the phone's FFE1) carries one enabled
 *     output: ELM327 or RealDash (#612 2c; a second channel, #620, is not planned);
 *   - one OBD output, one CSV on SD, one ELM327 on a serial port, one on BLE,
 *     one RealDash on BLE (the code that runs them exists once each).
 * Disabled outputs may coexist; they are checked when enabled.
 *
 * The checks read a SystemConfig (the live one, or JSON IMPORT's staged copy)
 * and the output table, and return the reason as a flash string, or nullptr
 * when the output is admitted.
 */

#ifndef OUTPUT_ADMISSION_H
#define OUTPUT_ADMISSION_H

#include <Arduino.h>
#include "output_table.h"

// May output `selfId` (OUT_ID_NONE for a new one) run as `format` on
// `transport`, enabled, beside the other enabled outputs in `table`?
const __FlashStringHelper* outputAdmitReason(uint8_t selfId, OutputFormat format, uint8_t transport,
                                            const SystemConfig& cfg, const OutputRecord* table);

// Is there room for another output of this format/kind (the singletons)?
const __FlashStringHelper* outputAddLimitReason(OutputFormat format, uint8_t transport,
                                               const OutputRecord* table);

// The enabled output that needs `transport` to itself (REALDASH, ELM327), or
// any enabled output on it when `anyOutput`; OUT_ID_NONE if none. For the doors
// that put the console or the log on a stream, or change a serial port.
uint8_t outputOnStream(uint8_t transport, bool anyOutput, const OutputRecord* table);

// The other output that holds the one-per-kind slot of <format, transport>
// (OBD; CSV on SD; ELM327 on serial; ELM327 on BLE), or OUT_ID_NONE when
// <selfId> holds it. OUT_ID_NONE as selfId (OUTPUT ADD): any record there.
// Where a table could hold two (EEPROM load, JSON IMPORT, start), the lowest
// enabled id keeps the slot, else the lowest.
uint8_t outputSingletonHolder(uint8_t selfId, OutputFormat format, uint8_t transport,
                              const OutputRecord* table);

#endif // OUTPUT_ADMISSION_H
