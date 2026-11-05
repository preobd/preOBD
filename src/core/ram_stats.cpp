/*
 * ram_stats.cpp - see ram_stats.h
 */

#include "ram_stats.h"

#if defined(ARDUINO_ARCH_STM32)

#include <unistd.h>   // sbrk
#include <malloc.h>   // mallinfo

extern "C" char _end;      // end of .bss: where the heap starts (linker script)
extern "C" char _estack;   // top of RAM: where the stack starts
extern "C" char _sdata;    // start of RAM .data

namespace {
constexpr uint32_t PAINT = 0xA5C3A5C3UL;
uint32_t* s_paintStart = nullptr;   // lowest painted word; null until painted

inline uint32_t* heapEnd() { return (uint32_t*)(((uintptr_t)sbrk(0) + 3) & ~(uintptr_t)3); }
}

void ramStatsPaint() {
    uint32_t* p = heapEnd() + 64;   // leave the heap a little room first
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    uint32_t* top = (uint32_t*)((sp & ~(uint32_t)3) - 256);   // stay clear of live frames
    s_paintStart = p;
    while (p < top) *p++ = PAINT;
}

void ramStatsPrint(Print& out) {
    const uint32_t ramTotal = (uint32_t)(&_estack - &_sdata);
    const uint32_t staticBytes = (uint32_t)(&_end - &_sdata);
    uint32_t* heap = heapEnd();
    const uint32_t heapPeak = (uint32_t)((char*)heap - &_end);   // newlib never gives memory back

    out.print(F("RAM: "));
    out.print(ramTotal);
    out.print(F(" B; static "));
    out.print(staticBytes);
    out.print(F(", heap peak "));
    out.print(heapPeak);
    out.print(F(" (in use "));
    out.print((uint32_t)mallinfo().uordblks);
    out.print(')');
    if (s_paintStart) {
        // Lowest word the stack has overwritten since boot. Below the painted
        // area is the heap's (unpainted) headroom, or the heap itself.
        uint32_t* p = heap > s_paintStart ? heap : s_paintStart;
        uint32_t* top = (uint32_t*)&_estack;
        while (p < top && *p == PAINT) p++;
        const uint32_t stackPeak = (uint32_t)((char*)top - (char*)p);
        const uint32_t gap = (uint32_t)((char*)p - (char*)heap);
        out.print(F(", stack peak "));
        out.print(stackPeak);
        out.print(F(", min free "));
        out.print(gap);
    }
    out.println(F(" B"));
}

#else

void ramStatsPaint() {}
void ramStatsPrint(Print&) {}

#endif
