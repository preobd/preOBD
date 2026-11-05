/*
 * watchdog_trace.cpp - see watchdog_trace.h
 */

#include <Arduino.h>
#include <string.h>
#include "watchdog_trace.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif

#if defined(__IMXRT1062__)

// Breadcrumb slots (CrashReportClass::breadcrumb takes 1..6).
//   1, 2  tag, 8 ASCII bytes
//   3     PC at the pre-timeout interrupt (0 = none)
//   4     LR at the pre-timeout interrupt
//   5     millis() at the pre-timeout interrupt
//   6     magic: the block holds a preOBD watchdog record
#define WDT_BC_TAG0   1
#define WDT_BC_TAG1   2
#define WDT_BC_PC     3
#define WDT_BC_LR     4
#define WDT_BC_TIME   5
#define WDT_BC_MAGIC  6
#define WDT_TRACE_MAGIC 0x57445447UL   // "WDTG"

static volatile bool s_preFired = false;

// Loop-side writes run with interrupts masked: the pre-timeout ISR also writes
// the block, and two writers interleaving the checksum would leave a record
// the next boot rejects. Saves and restores PRIMASK, so it nests.
struct IrqGuard {
    uint32_t primask;
    IrqGuard()  { __asm__ volatile("mrs %0, primask" : "=r"(primask)); __disable_irq(); }
    ~IrqGuard() { if (!(primask & 1)) __enable_irq(); }
};
static const char*   s_lastTag = nullptr;

static void putTag(const char* tag) {
    char buf[8] = {0};
    if (tag) strncpy(buf, tag, sizeof(buf));
    uint32_t w0, w1;
    memcpy(&w0, buf, 4);
    memcpy(&w1, buf + 4, 4);
    CrashReportClass::breadcrumb(WDT_BC_TAG0, w0);
    CrashReportClass::breadcrumb(WDT_BC_TAG1, w1);
}

void watchdogTraceTag(const char* tag) {
    if (tag == s_lastTag) return;   // tags are literals: same pointer, same text
    s_lastTag = tag;
    IrqGuard g;
    putTag(tag);
}

void watchdogTracePreTimeout(uint32_t pc, uint32_t lr) {
    CrashReportClass::breadcrumb(WDT_BC_PC, pc);
    CrashReportClass::breadcrumb(WDT_BC_LR, lr);
    CrashReportClass::breadcrumb(WDT_BC_TIME, millis());
    s_preFired = true;
}

void watchdogTraceFed() {
    if (!s_preFired) return;
    // The loop came back: a near miss, not the hang that resets the board.
    IrqGuard g;
    s_preFired = false;
    CrashReportClass::breadcrumb(WDT_BC_PC, 0);
}

FLASHMEM void watchdogTraceCapture(WatchdogTrace& out) {
    memset(&out, 0, sizeof(out));
    volatile struct crashreport_breadcrumbs_struct* bc =
        (struct crashreport_breadcrumbs_struct*)0x2027FFC0;
    arm_dcache_delete((void*)bc, sizeof(*bc));   // read RAM, not a stale cache line
    const uint32_t need = (1u << (WDT_BC_MAGIC - 1)) | (1u << (WDT_BC_TAG0 - 1)) |
                          (1u << (WDT_BC_TAG1 - 1));
    if ((bc->bitmask & need) == need &&
        bc->checksum == CrashReportClass::checksum(bc, 28) &&
        bc->value[WDT_BC_MAGIC - 1] == WDT_TRACE_MAGIC) {
        out.valid = true;
        uint32_t w0 = bc->value[WDT_BC_TAG0 - 1], w1 = bc->value[WDT_BC_TAG1 - 1];
        memcpy(out.tag, &w0, 4);
        memcpy(out.tag + 4, &w1, 4);
        out.tag[8] = '\0';
        for (uint8_t i = 0; i < 8 && out.tag[i]; i++) {
            if (out.tag[i] < 0x20 || out.tag[i] > 0x7E) { out.valid = false; break; }
        }
        out.pc = bc->value[WDT_BC_PC - 1];
        out.hasPc = out.valid && (bc->bitmask & (1u << (WDT_BC_PC - 1))) && out.pc != 0;
        out.lr = bc->value[WDT_BC_LR - 1];
        out.uptimeMs = bc->value[WDT_BC_TIME - 1];
    }
    // Fresh record for this run. The tag is written now, not at the first
    // apply(): a fault before watchdogStart() must not report the previous
    // run's tag.
    putTag("BOOT");
    CrashReportClass::breadcrumb(WDT_BC_PC, 0);
    CrashReportClass::breadcrumb(WDT_BC_MAGIC, WDT_TRACE_MAGIC);
    s_lastTag = nullptr;
    s_preFired = false;
}

#else

void watchdogTraceTag(const char*) {}
void watchdogTracePreTimeout(uint32_t, uint32_t) {}
void watchdogTraceFed() {}
void watchdogTraceCapture(WatchdogTrace& out) { memset(&out, 0, sizeof(out)); }

#endif
