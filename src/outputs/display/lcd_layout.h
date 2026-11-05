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

// ---------------------------------------------------------------------------
// Fitting a reading into a cell (#581)
//
// A cell is "label:value". When that was wider than the cell, the value used
// to be cut on the right, so 1650 could show as "16": a plausible, wrong
// number. A partial number is never shown now. In order:
//   1. label, colon and the value at its usual precision, if they fit;
//   2. else the value without decimals (11.4 -> 11), label whole;
//   3. else the label shortened (it's cosmetic) to make room for that whole
//      value, down to LCD_MIN_LABEL characters so the cell can still be told
//      apart from its neighbours;
//   4. else an overflow marker filling the room after the label, keeping the
//      minus sign of a negative value ("RPM:#####", "OAT:-####"). The label
//      keeps up to width-1-LCD_MIN_OVERFLOW characters.
// The unit is decided afterwards from the room left (lcdUnitChars), so it
// never costs the value a digit. Placeholders (ERR, CFG, ---) go through the
// same label shortening (lcdFormatTextCell) instead of being clipped.
// ---------------------------------------------------------------------------

#define LCD_MIN_LABEL 2       // shortest a label is cut to before overflowing
#define LCD_MIN_OVERFLOW 3    // shortest overflow marker ("###")
#define LCD_OVERFLOW_CHAR '#'
#define LCD_CELL_BUF (LCD_COLUMNS_PER_SENSOR + 1)   // out buffer for the helpers

// `v` with `decimals` places, rounded half away from zero, into out[0..cap).
// No float printf (AVR has none) and no dtostrf (not on the host). Returns
// the length, or 0 when it doesn't fit in `cap` or isn't a finite number.
// A value that rounds to zero prints without a sign ("0.0", not "-0.0").
// Not NUL-terminated.
static inline uint8_t lcdFormatNumber(char* out, uint8_t cap, float v, uint8_t decimals) {
    float scale = 1.0f;
    for (uint8_t i = 0; i < decimals; i++) scale *= 10.0f;
    const float a = (v < 0 ? -v : v) * scale + 0.5f;
    if (!(a < 4.0e9f)) return 0;                     // NaN, inf, or past uint32
    uint32_t n = (uint32_t)a;
    const bool neg = v < 0 && n != 0;
    char tmp[12];                                    // 10 digits + '.', reversed
    uint8_t t = 0;
    const uint8_t minLen = decimals ? (uint8_t)(decimals + 2) : 1;   // "0.0"
    do {
        tmp[t++] = (char)('0' + n % 10);
        n /= 10;
        if (decimals && t == decimals) tmp[t++] = '.';
    } while ((n || t < minLen) && t < sizeof(tmp) - 1);
    const uint8_t len = (uint8_t)(t + (neg ? 1 : 0));
    if (n || len > cap) return 0;
    uint8_t o = 0;
    if (neg) out[o++] = '-';
    while (t) out[o++] = tmp[--t];
    return len;
}

// Label length that lets `valLen` characters follow "label:" in `width`, or
// -1 when even LCD_MIN_LABEL characters leave too little room.
static inline int lcdFitLabel(uint8_t width, uint8_t labelLen, uint8_t valLen) {
    if (labelLen + 1 + valLen <= width) return labelLen;
    const int room = (int)width - 1 - (int)valLen;
    return room >= LCD_MIN_LABEL ? room : -1;
}

// out = first labelLen chars of label, ':', val, NUL. Returns the length.
static inline uint8_t lcdPutCell(char* out, const char* label, uint8_t labelLen,
                                 const char* val, uint8_t valLen) {
    uint8_t o = 0;
    for (uint8_t i = 0; i < labelLen; i++) out[o++] = label[i];
    out[o++] = ':';
    for (uint8_t i = 0; i < valLen; i++) out[o++] = val[i];
    out[o] = '\0';
    return o;
}

static inline uint8_t lcdTextLen(const char* s) {
    uint8_t n = 0;
    while (s && s[n] && n < LCD_COLUMNS_PER_SENSOR) n++;
    return n;
}

// "label:value" for a reading, per the rules above. `out` holds
// LCD_CELL_BUF chars; `width` is lcdCellTextWidth() (<= 10). Returns the
// characters written (never more than `width`).
static inline uint8_t lcdFormatValueCell(char* out, uint8_t width, const char* label,
                                         float value, uint8_t decimals) {
    if (width > LCD_COLUMNS_PER_SENSOR) width = LCD_COLUMNS_PER_SENSOR;
    const uint8_t labelLen = lcdTextLen(label);
    char num[LCD_CELL_BUF];
    uint8_t n;
    // 1. usual precision, whole label
    if (decimals) {
        n = lcdFormatNumber(num, width, value, decimals);
        if (n && labelLen + 1 + n <= width) return lcdPutCell(out, label, labelLen, num, n);
    }
    // 2./3. whole number, label whole or shortened
    n = lcdFormatNumber(num, width, value, 0);
    if (n) {
        const int l = lcdFitLabel(width, labelLen, n);
        if (l >= 0) return lcdPutCell(out, label, (uint8_t)l, num, n);
    }
    // 4. overflow marker, never leading digits
    uint8_t l = labelLen;
    if (l + 1 + LCD_MIN_OVERFLOW > width) l = (uint8_t)(width - 1 - LCD_MIN_OVERFLOW);
    const uint8_t room = (uint8_t)(width - 1 - l);
    for (uint8_t i = 0; i < room; i++) num[i] = LCD_OVERFLOW_CHAR;
    if (value < 0) num[0] = '-';
    return lcdPutCell(out, label, l, num, room);
}

// "label:text" for a placeholder (ERR, CFG, ---): the label is shortened
// rather than the placeholder clipped. Same buffer/width contract as above.
static inline uint8_t lcdFormatTextCell(char* out, uint8_t width, const char* label, const char* text) {
    if (width > LCD_COLUMNS_PER_SENSOR) width = LCD_COLUMNS_PER_SENSOR;
    uint8_t textLen = lcdTextLen(text);
    if (textLen + 1 > width) textLen = (uint8_t)(width - 1);
    int l = lcdFitLabel(width, lcdTextLen(label), textLen);
    if (l < 0) l = (int)width - 1 - (int)textLen;
    return lcdPutCell(out, label, (uint8_t)l, text, textLen);
}

// Unit characters to print with `room` columns left: the whole unit (degree
// sign + symbol for a temperature) if it fits, else a temperature's degree
// sign alone, else nothing — a clipped "rp" or "ba" reads as a different
// unit (#579).
static inline uint8_t lcdUnitChars(uint8_t room, uint8_t symLen, bool isTemp) {
    const uint8_t unitLen = (uint8_t)(symLen + (isTemp ? 1 : 0));
    if (unitLen > 0 && unitLen <= room) return unitLen;
    if (isTemp && room >= 1) return 1;
    return 0;
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
