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

// The 20x4 LCD: 2 columns x 4 rows = 8 cells, 10 characters each.
#define LCD_COLS 20
#define LCD_ROWS 4
#define LCD_COLUMNS_PER_SENSOR (LCD_COLS / 2)
#define LCD_CELLS (LCD_ROWS * 2)

// HD44780 A00 ROM (the common Japanese ROM) character for the degree sign.
// Built in, so no custom character is loaded into CGRAM (#579).
#define LCD_DEGREE_CHAR ((uint8_t)0xDF)

// Cells 0-3 are the left column top to bottom, 4-7 the right column.
static inline uint8_t lcdCellCol(uint8_t line) { return line >= LCD_ROWS ? LCD_COLUMNS_PER_SENSOR : 0; }
static inline uint8_t lcdCellRow(uint8_t line) { return line >= LCD_ROWS ? (uint8_t)(line - LCD_ROWS) : line; }

// Characters of text a cell may print. The per-input icon (#579) used to sit
// in each cell's first column and doubled as the gap between the two columns.
// Without it the left cell keeps its last column blank as that gap (or its
// value would run into the right cell's label); the right cell ends at the
// screen edge and gets all 10.
static inline uint8_t lcdCellTextWidth(uint8_t line) {
    return line >= LCD_ROWS ? LCD_COLUMNS_PER_SENSOR : (uint8_t)(LCD_COLUMNS_PER_SENSOR - 1);
}

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
