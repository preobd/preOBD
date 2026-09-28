/*
 * lcd_layout.h - Which inputs the character LCD shows, and which page (#480)
 *
 * Pure, header-only: no Arduino/LCD deps, so the native test build exercises
 * it directly (test/test_lcd_layout). The LCD draws a grid of 2 columns x 4
 * rows = 8 cells; with more visible inputs than that it pages through groups
 * of 8 on a timer instead of drawing past row 3.
 */

#ifndef LCD_LAYOUT_H
#define LCD_LAYOUT_H

#include <stdint.h>

// Cells on the 20x4 LCD grid: 2 columns x 4 rows.
#define LCD_CELLS 8

// Whether an input takes an LCD cell. Hidden inputs (SET <pin> DISPLAY
// DISABLE) no longer do. RUN shows enabled inputs; CONFIG also shows
// configured-but-disabled ones (drawn as "CFG") so they can be seen while
// being set up.
static inline bool lcdShowsInput(bool configured, bool enabled, bool displayFlag, bool configMode) {
    return configured && displayFlag && (enabled || configMode);
}

struct LcdPage {
    uint8_t index;   // page being shown, 0-based
    uint8_t pages;   // total pages (0 when nothing is shown)
    uint8_t start;   // first visible input on this page
    uint8_t count;   // inputs on this page (<= perPage)
};

// The page shown at `nowMs`, rotating every `periodMs`. One page (or none)
// never rotates.
static inline LcdPage lcdPageAt(uint8_t total, uint8_t perPage, uint32_t nowMs, uint32_t periodMs) {
    LcdPage p{0, 0, 0, 0};
    if (total == 0 || perPage == 0) return p;
    p.pages = (uint8_t)((total + perPage - 1) / perPage);
    p.index = (p.pages > 1 && periodMs > 0) ? (uint8_t)((nowMs / periodMs) % p.pages) : 0;
    p.start = (uint8_t)(p.index * perPage);
    const uint8_t left = (uint8_t)(total - p.start);
    p.count = left < perPage ? left : perPage;
    return p;
}

#endif // LCD_LAYOUT_H
