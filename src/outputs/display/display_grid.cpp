/*
 * display_grid.cpp - The 20x4 reading grid, drawn on the LCD or the OLED
 *
 * One cell renderer for both panels. The PCF8574-backpack LCD and the 128x64
 * SH1107 OLED (oled_sh1107.h) draw the same lcd_layout.h 20x4 grid; systemConfig.displayType picks which one is brought up at boot. The
 * "LCD" in the function names predates the OLED and means "the display".
 */

#include "../../config.h"
#include "../../inputs/input.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/units_registry.h"
#include "../../core/message_api.h"
#include "../../core/log_tags.h"
#include "../../sensors/application_presets.h"
#include "../../sensors/sensor_library.h"

#if INCLUDE_DISPLAY

#include "../../drivers/bus_manager.h"
#include "../../storage/system_config.h"
#include "lcd_layout.h"

#if INCLUDE_LCD
#include "lcd_pcf8574.h"
#endif
#if INCLUDE_OLED
#include "oled_sh1107.h"
#endif

// LCD geometry (LCD_COLS, LCD_ROWS, cells) lives in lcd_layout.h.

// NOTE: the LCD is always driven at LCD_I2C_ADDRESS (0x27); `DISPLAY ADDRESS`
// remains cosmetic, as it was with the LiquidCrystal libraries before the
// in-tree driver. Tracked separately, not part of #351.
#if INCLUDE_LCD
static LcdPcf8574 lcd;
#endif
#if INCLUDE_OLED
static OledSh1107 oled;
// The 20x4 grid on the 128x64 panel: 120 px wide, centered, and one text row
// every 16 px (each 8 px row centered in its band).
static const uint8_t OLED_GRID_X = (OLED_WIDTH - LCD_COLS * OLED_CHAR_WIDTH) / 2;
static const uint8_t OLED_GRID_PITCH = OLED_HEIGHT / LCD_ROWS;
static const uint8_t OLED_GRID_Y = (OLED_GRID_PITCH - OLED_CHAR_HEIGHT) / 2;
#endif
byte currentLine = 0;

// The display is a *client* of the I2C bus, not its owner. Until acquireI2C()
// hands one over, every entry point below must no-op: driving an un-begun
// TwoWire is what used to leave the bus wedged (#351). DISPLAY_NONE until
// initLCD()/initOLED() runs; then which panel the calls below drive.
static uint8_t activePanel = DISPLAY_NONE;
static inline bool panelReady() { return activePanel != DISPLAY_NONE; }

// Panel-neutral primitives. Both drivers are a Print, so text goes through
// screen(); the rest dispatch on activePanel.
static Print& screen() {
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) return oled;
#endif
#if INCLUDE_LCD
    return lcd;
#else
    return oled;
#endif
}

static void screenSetCursor(uint8_t col, uint8_t row) {
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) { oled.setCursor(col, row); return; }
#endif
#if INCLUDE_LCD
    lcd.setCursor(col, row);
#endif
}

static void screenClear() {
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) { oled.clear(); return; }
#endif
#if INCLUDE_LCD
    lcd.clear();
#endif
}

// The OLED draws into a framebuffer; this pushes what changed. The LCD
// writes through, so it has nothing to do.
static void screenFlush() {
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) oled.flush();
#endif
}

static void screenPower(bool on) {
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) { oled.setPower(on); return; }
#endif
#if INCLUDE_LCD
    lcd.setBacklight(on);
#endif
}

// Re-probe an offline panel, at most once per retry interval: false while it's
// away. `blanked` is set when the panel was just (re)initialized, blank.
static bool screenService(bool& blanked) {
    blanked = false;
#if INCLUDE_OLED
    if (activePanel == DISPLAY_OLED) {
        if (!oled.service()) return false;
        blanked = oled.takeReset();
        return true;
    }
#endif
#if INCLUDE_LCD
    if (activePanel == DISPLAY_LCD) {
        const bool wasOnline = lcd.online();
        if (!lcd.service()) return false;
        blanked = !wasOnline;            // init() cleared it
        return true;
    }
#endif
    return true;
}


// Per-input icons: REMOVED (#579), kept here commented out so they can be
// brought back. They were often wrong (getIconForApplication() guessed from the
// application name, so RPM could show a thermometer), each took one of a cell's
// ten columns, and on the Feather F405 the createChar() pass at boot left all
// eight custom characters garbled. The degree sign is now the LCD's built-in
// ROM character (LCD_DEGREE_CHAR), so no CGRAM is loaded at all.
//
// enum ICONS{
//     ICON_DEGREE,
//     ICON_THERMOMETER,
//     ICON_OIL_CAN,
//     ICON_TURBO,
//     ICON_BATTERY,
//     ICON_TACHOMETER,
//     ICON_COOLANT,
//     ICON_OIL
// };
//
// byte degree_icon[8] = {0xc,0x12,0x12,0xc,0x0,0x0,0x0,0x0};
// byte thermometer_icon[8] = {0x4,0xa,0xa,0xa,0x11,0x1f,0xe,0x0};
// byte oil_can_icon[8] = {0x02,0x05,0x0E,0x11,0x11,0x11,0x1F,0x00};
// //byte oil_icon[8] = {0x04,0x0E,0x1F,0x1D,0x1D,0x1B,0x0E,0x00}; //big drop
// byte oil_icon[8] = {0x00,0x04,0x0E,0x1D,0x1D,0x1B,0x0E,0x00}; //small drop
// byte turbo_icon[8] = {0x00,0x0F,0x1E,0x1B,0x15,0x1B,0x0E,0x00};
// byte battery_icon[8] = {0x00,0x00,0x0A,0x1F,0x11,0x11,0x1F,0x00};
// byte tachometer_icon[8] = {0x00,0x0E,0x11,0x11,0x15,0x11,0x0E,0x00};
// byte coolant_icon[8] = {0x04,0x07,0x04,0x07,0x04,0x0E,0x0E,0x04};
//
// Loaded in initLCD() after setBacklight(). The in-tree LCD driver
// (lcd_pcf8574.h, #586) has no createChar() yet: add one before restoring this.
//     lcd.createChar(ICON_DEGREE, degree_icon);
//     lcd.createChar(ICON_THERMOMETER, thermometer_icon);
//     lcd.createChar(ICON_OIL_CAN, oil_can_icon);
//     lcd.createChar(ICON_TURBO, turbo_icon);
//     lcd.createChar(ICON_BATTERY, battery_icon);
//     lcd.createChar(ICON_TACHOMETER, tachometer_icon);
//     lcd.createChar(ICON_COOLANT, coolant_icon);
//     lcd.createChar(ICON_OIL, oil_icon);
//
// Printed first in displaySensor() (one column off the cell's text width):
//     lcd.write(getIconForApplication(ptr->applicationIndex));

// Forward declarations
void showConfigModeMessage();

#if INCLUDE_LCD
void initLCD() {
    // Ask the bus manager for a bus rather than calling Wire.begin() ourselves.
    // It picks the bus (honoring `BUS I2C <n>`) and reserves SDA/SCL, so an input
    // can never be handed a pin the display is clocking.
    TwoWire* wire = acquireI2C();
    if (!wire) {
        msg.debug.error(TAG_DISPLAY, "LCD: no I2C bus");
        return;
    }

    // The panel drives the grid from here on even if it doesn't answer: a
    // redraw re-probes it (lcd.service()), so an LCD fitted or reseated after
    // boot comes up without a reboot. Nothing is written to it until it
    // answers, and a failed write takes it offline: on the STM32 an
    // unanswered LCD cost 100 ms per write, ~90 writes a redraw (#586).
    // No createChar(): nothing is loaded into CGRAM (#579, see above).
    activePanel = DISPLAY_LCD;
    if (lcd.begin(wire, LCD_I2C_ADDRESS)) {
        msg.debug.info(TAG_DISPLAY, "LCD initialized");
    } else {
        msg.debug.warn(TAG_DISPLAY, "LCD: no answer, retrying");
    }
}
#else
void initLCD() {}
#endif

#if INCLUDE_OLED
void initOLED() {
    // Same bus contract as the LCD: the bus manager picks and owns the bus.
    TwoWire* wire = acquireI2C();
    if (!wire) {
        msg.debug.error(TAG_DISPLAY, "OLED: no I2C bus");
        return;
    }

    // The panel drives the grid from here on even if it doesn't answer yet:
    // a redraw re-probes it (oled.service()), so a panel fitted or reseated
    // after boot comes up without a reboot.
    activePanel = DISPLAY_OLED;
    oled.setTextGrid(OLED_GRID_X, OLED_GRID_Y, OLED_GRID_PITCH);
    if (oled.begin(wire, OLED_I2C_ADDRESS)) {
        msg.debug.info(TAG_DISPLAY, "OLED initialized");
    } else {
        msg.debug.warn(TAG_DISPLAY, "OLED: no answer at 0x3C, retrying every 5 s");
    }
}

// For DISPLAY STATUS / SYSTEM STATUS.
const __FlashStringHelper* oledPanelState() {
    if (activePanel != DISPLAY_OLED) return F("not started (DISPLAY TYPE takes effect at boot; BUS I2C shows the bus)");
    if (oled.online()) return F("responding");
    return F("not responding (checked every 5 s while the display is enabled)");
}
#else
void initOLED() {}
const __FlashStringHelper* oledPanelState() { return F("not in this firmware"); }
#endif

// Icon lookup: REMOVED (#579), kept for possible resurrection (see the ICONS
// note above).
//
// /**
//  * Get icon for application using registry-based pattern matching
//  *
//  * Uses application name patterns and measurement types to determine
//  * the appropriate icon. This is data-driven - no code changes needed
//  * when adding new applications.
//  *
//  * @param appIndex Index into APPLICATION_PRESETS array
//  * @return Icon character code (custom LCD characters)
//  */
// byte getIconForApplication(uint8_t appIndex) {
//     if (appIndex >= NUM_APPLICATION_PRESETS) return 32; // space
//
//     // Get application info from registry
//     const ApplicationPreset* preset = getApplicationByIndex(appIndex);
//     if (!preset) return 32;
//
//     // Read name from PROGMEM into RAM buffer
//     const char* namePtr = (const char*)pgm_read_ptr(&preset->name);
//     char nameBuf[32];
//     strncpy_P(nameBuf, namePtr, sizeof(nameBuf) - 1);
//     nameBuf[sizeof(nameBuf) - 1] = '\0';
//
//     MeasurementType measType = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
//
//     // Pattern matching on name (now in RAM)
//     // Check specific patterns first, then fall back to measurement type
//
//     if (strstr(nameBuf, "COOLANT") && strstr(nameBuf, "TEMP")) {
//         return ICON_COOLANT;
//     }
//     if (strstr(nameBuf, "OIL")) {
//         return ICON_OIL;
//     }
//     if (strstr(nameBuf, "FUEL")) {
//         return ICON_OIL_CAN;
//     }
//     if (strstr(nameBuf, "BOOST")) {
//         return ICON_TURBO;
//     }
//     if (strstr(nameBuf, "BATTERY")) {
//         return ICON_BATTERY;
//     }
//     if (strstr(nameBuf, "RPM")) {
//         return ICON_TACHOMETER;
//     }
//
//     // Fallback to measurement type
//     switch (measType) {
//         case MEASURE_TEMPERATURE:
//             return ICON_THERMOMETER;
//         case MEASURE_PRESSURE:
//             return 32;  // Generic pressure icon
//         case MEASURE_VOLTAGE:
//             return ICON_BATTERY;
//         case MEASURE_RPM:
//             return ICON_TACHOMETER;
//         default:
//             return 32; // space
//     }
// }

void displaySensor(Input *ptr, byte line) {
    if (!panelReady()) return;
    screenSetCursor(lcdCellCol(line), lcdCellRow(line));

    // Text this cell may use. The left cell's last column stays blank as the
    // gap before the right cell (the icon column used to be that gap, #579).
    const int width = lcdCellTextWidth(line);
    int charsPrinted = 0;

    if (ptr->flags.display) {
        // "label:value" never shows a partial number (#581): decimals go
        // first, then the label is shortened, then an overflow marker. The
        // rules live in lcd_layout.h (lcdFormatValueCell) and are unit-tested.
        char cell[LCD_CELL_BUF];

        if (!ptr->flags.isEnabled) {
            // Configured but disabled (CONFIG mode)
            charsPrinted = lcdFormatTextCell(cell, width, ptr->abbrName, "CFG");
            screen().print(cell);
        } else if (isnan(ptr->value)) {
            // Invalid reading
            charsPrinted = lcdFormatTextCell(cell, width, ptr->abbrName, "ERR");
            screen().print(cell);
        }
        // Not read yet since it was configured: show "---". A reading of 0 is
        // real (0 bar, 0 RPM, a low float switch) and is displayed (#481).
        else if (!ptr->flags.hasReading) {
            charsPrinted = lcdFormatTextCell(cell, width, ptr->abbrName, "---");
            screen().print(cell);
        } else {
            // Convert to display units
            float displayValue = convertFromBaseUnits(ptr->value, ptr->unitsIndex);

            // Get unit info from registry
            const UnitsInfo* unitInfo = getUnitsByIndex(ptr->unitsIndex);
            MeasurementType measType = MEASURE_TEMPERATURE;
            if (unitInfo) {
                measType = (MeasurementType)pgm_read_byte(&unitInfo->measurementType);
            }

            // Determine decimal precision based on measurement type
            int decimals = 1;  // Default: 1 decimal place
            switch (measType) {
                case MEASURE_TEMPERATURE:
                case MEASURE_HUMIDITY:
                case MEASURE_PERCENT:
                case MEASURE_ELEVATION:
                case MEASURE_RPM:
                case MEASURE_SPEED:
                case MEASURE_DIGITAL:
                case MEASURE_DISTANCE:
                    decimals = 0;  // No decimals for these
                    break;
                case MEASURE_TIME:
                    // s and min whole; h keeps one (1.5 h, not 2) (#509)
                    decimals = (ptr->unitsIndex == getUnitsIndexByName("HOURS")) ? 1 : 0;
                    break;
                case MEASURE_MASS_FLOW:
                case MEASURE_FUEL_RATE:
                    decimals = 1;
                    break;
                case MEASURE_PRESSURE:
                    // inHg uses 2 decimals, others use 1
                    decimals = (ptr->unitsIndex == getUnitsIndexByName("INHG")) ? 2 : 1;
                    break;
                case MEASURE_VOLTAGE:
                    decimals = 1;  // 1 decimal for voltage
                    break;
            }

            charsPrinted = lcdFormatValueCell(cell, width, ptr->abbrName, displayValue, (uint8_t)decimals);
            screen().print(cell);

            // Unit symbol from the registry, only if it fits whole: a clipped
            // "rp" or "ba" reads as a different unit. Temperatures get the
            // LCD's built-in degree sign first (°C / °F); with room for the
            // sign alone it's printed without the letter (#579).
            const char* symPtr = unitInfo ? (const char*)pgm_read_ptr(&unitInfo->symbol) : nullptr;
            const uint8_t symLen = symPtr ? (uint8_t)strlen_P(symPtr) : 0;
            const bool isTemp = (measType == MEASURE_TEMPERATURE);
            const uint8_t unitChars = lcdUnitChars((uint8_t)(width - charsPrinted), symLen, isTemp);
            uint8_t u = 0;
            if (isTemp && unitChars > 0) { screen().write(LCD_DEGREE_CHAR); u = 1; }
            for (uint8_t i = 0; u < unitChars; i++, u++) screen().print((char)pgm_read_byte(symPtr + i));
            charsPrinted += unitChars;
        }
    }

    // Pad remaining space in the column to clear old data
    for (int i = charsPrinted; i < LCD_COLUMNS_PER_SENSOR; i++) {
        screen().print(' ');
    }
}

// Blank one grid cell (a page with fewer than 8 inputs, or an input that went
// away, must not leave a stale reading behind).
static void clearCell(byte line) {
    screenSetCursor(lcdCellCol(line), lcdCellRow(line));
    for (int i = 0; i < LCD_COLUMNS_PER_SENSOR; i++) screen().print(' ');
}

// Whether the panel holds readings / the CONFIG MODE message. File scope so
// a panel that went away and came back (blank) gets them drawn again.
static bool firstSensorShown = false;
static bool configMsgShown = false;

static void drawLCD(Input** inputs, int numInputs) {
    currentLine = 0;

    bool blanked = false;
    if (!screenService(blanked)) return;      // offline: one probe per 5 s at most
    if (blanked) {
        // The panel was (re)initialized and is already blank: draw everything
        // again, without clearing it a second time.
        configMsgShown = false;
        firstSensorShown = (numInputs > 0);
    }

    if (numInputs == 0) {
        // Readings were on screen and every input is now hidden (DISPLAY
        // DISABLE) or gone: blank the grid rather than freeze them there (#490
        // review). Before any input was shown, it's the CONFIG MODE message.
        if (firstSensorShown) {
            for (uint8_t line = 0; line < LCD_CELLS; line++) clearCell(line);
            return;
        }
        if (!configMsgShown) {
            showConfigModeMessage();
            configMsgShown = true;
        }
        return;
    }

    // Clear the CONFIG MODE message when first sensor is configured
    if (!firstSensorShown) {
        screenClear();
        firstSensorShown = true;
    }

    // The grid holds 8 cells; more than that pages through groups of 8 (#480)
    // instead of drawing a 9th input off row 3. Unused cells are blanked.
    const uint8_t perPage = LCD_CELLS;
    const LcdPage page = lcdPageAt((uint8_t)numInputs, perPage, millis(), LCD_PAGE_INTERVAL_MS);
    for (uint8_t i = 0; i < page.count; i++) {
        displaySensor(inputs[page.start + i], currentLine);
        currentLine++;
    }
    for (uint8_t line = page.count; line < perPage; line++) clearCell(line);
}

void updateLCD(Input** inputs, int numInputs) {
    if (!panelReady()) return;
    drawLCD(inputs, numInputs);
    screenFlush();
}

void clearLCD() {
    if (!panelReady()) return;
    screenClear();
    screenFlush();
}

void showConfigModeMessage() {
    if (!panelReady()) return;
    screenClear();
    screenSetCursor(0, 0);
    screen().print(F("CONFIG MODE"));
    screenSetCursor(0, 1);
    screen().print(F("Use serial console"));
    screenFlush();
}

void enableLCD() {
    if (!panelReady()) return;
    screenPower(true);
}

void disableLCD() {
    if (!panelReady()) return;
    screenPower(false);
    screenClear();
    screenFlush();
}

#else

void initLCD() {}
void initOLED() {}
const __FlashStringHelper* oledPanelState() { return F("not in this firmware"); }
void displaySensor(Input *ptr, byte line) {}
void updateLCD(Input** inputs, int numInputs) {}
void clearLCD() {}
void showConfigModeMessage() {}
void enableLCD() {}
void disableLCD() {}

#endif
