/*
 * ram_stats.h - RAM high-water marks (STM32; the F405-vs-F767 RAM gate, #100)
 *
 * Static RAM is known at link time. What isn't is how far the heap and the
 * stack reach at run time, and on the STM32 they share the same 128 KB: the
 * heap grows up from the end of .bss, the stack down from the top. So
 * ramStatsPaint() fills the free space between them with a pattern at boot,
 * and ramStatsPrint() reports the deepest the stack has reached (the lowest
 * word no longer holding the pattern), the heap's peak, and the smallest gap
 * that has been left between them. Elsewhere both are no-ops.
 */

#ifndef RAM_STATS_H
#define RAM_STATS_H

#include <Arduino.h>

// First thing in setup(): paint the free RAM between heap and stack.
void ramStatsPaint();

// One "RAM: ..." line, for VERSION.
void ramStatsPrint(Print& out);

#endif
