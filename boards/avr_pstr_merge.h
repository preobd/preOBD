/*
 * boards/avr_pstr_merge.h — let the linker fold duplicate PSTR()/F() strings (#523)
 *
 * `-include`d by AVR envs only (boards/mega2560/board.ini).
 *
 * AVR reads flash data with near pgm_read_* (LPM), which reaches only the
 * first 64 KB. Every PROGMEM object must end below 0x10000, and the post-link
 * guard (scripts/avr_progmem_guard.py) fails the build if one doesn't. In
 * avr-libc, each PSTR() is its own `static const char __c[] PROGMEM` in
 * `.progmem.data`, which isn't a mergeable section, so the same text used in
 * 22 places is stored 22 times (8.3 KB of duplicates on mega2560 when #523
 * was filed).
 *
 * This puts each literal in a mergeable string section ("aMS", entsize 1)
 * instead. ld then keeps one copy of every distinct string and also shares
 * suffixes. The flags are passed through the section name: GCC writes the
 * name into `.section <name>,"a",@progbits`, and the trailing `;` (the AVR assembler's comment character) turns GCC's
 * own flags into an assembler comment. The output section is unchanged: the
 * avr6 linker script places `*(.progmem*)` in the low-64 KB data block.
 *
 * Merging can give two call sites the same pointer. Nothing in the firmware
 * compares PSTR pointers for identity, only contents (strcmp_P / streq_P).
 *
 * Named PROGMEM arrays (`static const char PSTR_X[] PROGMEM = ...`) are not
 * affected and are not merged.
 *
 * Two limits, both from GCC putting a template's or an inline function's
 * static local in a COMDAT group (found in the #526 review, avr-gcc 7.3):
 *   - In a function template, GCC drops the custom section and the string
 *     lands in .rodata, which is RAM on AVR: F() would print garbage.
 *     scripts/avr_progmem_guard.py fails the build if any PSTR/F() string
 *     (`__c`) ends up in RAM. (Adding `__progmem__` to the attribute fixes
 *     templates but overrides the section everywhere, so nothing merges.)
 *   - In an inline function or in-class member, F()/PSTR in a TU that also
 *     has an ordinary F() fails to compile ("section type conflict"). Move
 *     the string into a .cpp or a named PROGMEM array.
 */
#pragma once

#if defined(__AVR__) && !defined(__ASSEMBLER__)
#include <avr/pgmspace.h>   // define the stock PSTR first; its include guard keeps ours
#undef PSTR
#define PSTR(s) (__extension__({ \
    static const char __c[] \
        __attribute__((section(".progmem.pstr,\"aMS\",@progbits,1 ;"))) = (s); \
    &__c[0]; }))
#endif
