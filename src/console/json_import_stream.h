/*
 * json_import_stream.h - JSON IMPORT: a configuration streamed in over a
 * console link (#322, bound to that link since #624)
 *
 * Two modes share one buffer, malloc'd only while an import runs:
 *   - counted (`JSON IMPORT <n>`): exactly n bytes, for a programmatic
 *     sender that knows its payload length (the app, the webapp, scripts).
 *     No echo. The sender must wait for "Ready for <n> bytes" first.
 *   - sentinel (`JSON IMPORT`): a human pastes JSON, then types `END JSON`
 *     on its own line. Echoed.
 *
 * An import belongs to the console source that started it: only that
 * source's bytes go to it (console_input.cpp) and its replies go to that
 * source. Other sources keep running commands meanwhile. The configuration it
 * applies never changes the serial settings of a link commands arrive on
 * (serialPortCarriesCommands in json_config.cpp), so it can't cut off its own.
 *
 * This module knows nothing about prompts or sources beyond the id it is
 * given; console_input.cpp prints the prompt after an import ends.
 */
#ifndef JSON_IMPORT_STREAM_H
#define JSON_IMPORT_STREAM_H

#include "../config.h"

#if SUPPORTS_JSON_IMPORT_STREAM
#include <stddef.h>
#include <stdint.h>

/**
 * Start an import from console source `source` (a TransportID). target 0 =
 * sentinel mode, else counted mode for exactly `target` bytes. Prints the
 * "Ready for" / "Paste JSON" line. Returns false if an import is already
 * running (the caller reports it, naming jsonImportSource()) or there is no
 * heap for the buffer (reported here).
 */
bool jsonImportBegin(uint8_t source, size_t target);

bool jsonImportActive();

/** The TransportID of the source an active import belongs to. */
uint8_t jsonImportSource();

/** One byte from the import's source. Completing the import applies it. */
void jsonImportFeed(char c);

/** No byte for 45 s: the sender gave up, or its link dropped unseen. */
bool jsonImportIdleExpired();

/** Abandon the import and free its buffer; the caller reports why. */
void jsonImportCancel();
#endif

#endif // JSON_IMPORT_STREAM_H
