/*
 * reset_reason.h - Why did the MCU last reset? (#454)
 *
 * The hardware records the cause of the most recent reset in a status
 * register, but nothing read it, so an unexplained reboot (watchdog, fault,
 * brown-out, software reboot) could not be told apart after the fact.
 *
 * resetReasonCapture() snapshots the cause once, as the first thing in
 * setup(), and clears the sticky hardware state so the next boot reports only
 * its own reset. printResetReason() renders the snapshot as one line for the
 * boot (abnormal resets only), VERSION and SYSTEM STATUS.
 *
 * Reported on Teensy 4.x, Teensy 3.x and ESP32. AVR prints "unknown": the
 * Mega's bootloader clears MCUSR before the sketch can read it.
 */

#ifndef RESET_REASON_H
#define RESET_REASON_H

#include <Arduino.h>

// Snapshot and clear the hardware reset cause. Call once, first in setup().
void resetReasonCapture();

// Print "Last reset: <reason>" and a newline to `out`. `detail` appends the
// raw registers and any fault PC/CFSR (VERSION); without it the line is just
// the label (SYSTEM STATUS, boot).
void printResetReason(Print& out, bool detail = false);

// True for a reset nobody asked for (watchdog, fault, brown-out, clock loss,
// over-temperature). Power-on, upload and SYSTEM REBOOT are not, so the boot
// output only carries the line when there's something to look into.
bool resetReasonAbnormal();

#endif
