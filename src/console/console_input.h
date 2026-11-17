/*
 * console_input.h - Where console commands come from (#624)
 *
 * The console has up to three sources: its primary transport, its secondary
 * (CONSOLE TRANSPORT ... SECONDARY), and every started framed port (the nRF
 * link is always a source, so the app can't be cut off, #612). Each source
 * has its own line (line_reader.h), so bytes from two links can never mix
 * into one command; this is the "one session per link, one shared command
 * table" shape RTOS shells use.
 *
 * Each loop consolePump() reads every source in turn. A byte that ends a line
 * runs it at once (cliTokenize + dispatchCommand), with that source marked as
 * the sender (consoleSetActive), so the reply and the next prompt go to it
 * alone. A JSON IMPORT belongs to the source that started it: that source's
 * bytes go to the import (json_import_stream.h) until it ends, while the other
 * sources keep running commands.
 *
 * Where text goes is console_routing.h.
 */
#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

#include <stdint.h>

/** Once, in setup(): the slots, the banner and the first prompt. */
void consoleInputInit();

/** Each loop, after transportsUpdate(): read and run every console source. */
void consolePump();

/**
 * Is transport `id` (a TransportID) a console source now: the primary (or USB
 * standing in for it, #558), the secondary, or a framed port? Being one is
 * about the configuration, not the link state: a USB cable's DTR blip
 * doesn't stop USB being a source.
 */
bool consoleIsSource(uint8_t id);

/** The TransportID of the source whose line is running; TRANSPORT_NONE outside one. */
uint8_t consoleActiveSourceId();

#endif // CONSOLE_INPUT_H
