/*
 * watchdog_trace.h - Where was the firmware when the watchdog reset it? (#542)
 *
 * After a watchdog reset, VERSION used to say only "WATCHDOG", and finding
 * the hang meant reproducing it on the bench (#331, #351, #353, #361). Two
 * things now survive the reset in memory the next boot can read:
 *
 *   - the tag of the innermost WatchdogScope ("SD", "EEPROM", ...; "BOOT" in
 *     setup(), "LOOP" otherwise), updated whenever it changes;
 *   - on Teensy 4, the program counter (and LR) of the code that was running
 *     when the watchdog's pre-timeout interrupt fired, 0.5 s before the reset.
 *     A feed after that interrupt clears them, so a PC that reaches the next
 *     boot is where the firmware hung. A hang with interrupts masked (or in a
 *     higher-priority ISR) gets the tag but no PC.
 *
 * Teensy 4 keeps them in the core's CrashReport breadcrumb block (0x2027FFC0),
 * written through the static CrashReportClass::breadcrumb() so the
 * CrashReport object (and its ~2 KB of RAM1 strings) isn't linked. Other
 * platforms compile to no-ops for now (AVR's bootloader also hides the reset
 * cause; see #542).
 *
 * Map a PC to source with:
 *   ~/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin/arm-none-eabi-addr2line \
 *       -f -C -e .pio/build/teensy41/firmware.elf 0x<PC>
 */

#ifndef WATCHDOG_TRACE_H
#define WATCHDOG_TRACE_H

#include <stdint.h>

struct WatchdogTrace {
    bool     valid;      // the block held our record
    char     tag[9];     // innermost scope tag at the time, NUL-terminated
    bool     hasPc;      // the pre-timeout interrupt ran and nothing fed after it
    uint32_t pc;
    uint32_t lr;
    uint32_t uptimeMs;   // millis() when the pre-timeout interrupt ran
};

// Record the active tag. Called by core/watchdog.cpp when it changes.
void watchdogTraceTag(const char* tag);

// Called from the pre-timeout interrupt (Teensy 4): the interrupted PC/LR.
void watchdogTracePreTimeout(uint32_t pc, uint32_t lr);

// Called on every feed; cheap unless a pre-timeout interrupt fired since the
// last feed, in which case that was a near miss and its PC is cleared.
void watchdogTraceFed();

// Read what the previous run left, then start a fresh record. Call once at
// boot, before anything writes a tag.
void watchdogTraceCapture(WatchdogTrace& out);

#endif // WATCHDOG_TRACE_H
