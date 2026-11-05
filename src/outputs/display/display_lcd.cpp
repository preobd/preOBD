/*
 * display_lcd.cpp - LCD display module
 */

#include "../../config.h"
#include "../../inputs/input.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/units_registry.h"
#include "../../core/message_api.h"
#include "../../core/log_tags.h"
#include "../../sensors/application_presets.h"
#include "../../sensors/sensor_library.h"

#if INCLUDE_LCD

#include <LiquidCrystal_PCF8574.h>

#include "../../drivers/bus_manager.h"
#include "../../storage/system_config.h"
#include "lcd_layout.h"

// LCD geometry (LCD_COLS, LCD_ROWS, cells) lives in lcd_layout.h.

// NOTE: the PCF8574 address is fixed at construction — the library has no setter,
// so `DISPLAY ADDRESS` remains cosmetic here exactly as it was with the previous
// library (which also hardcoded 0x27). Tracked separately, not part of #351.
#define LCD_I2C_ADDRESS 0x27
LiquidCrystal_PCF8574 lcd(LCD_I2C_ADDRESS);
byte currentLine = 0;

// The LCD is a *client* of the I2C bus, not its owner. Until acquireI2C() hands
// one over, every entry point below must no-op: driving an un-begun TwoWire is
// what used to leave the bus wedged (#351).
static bool lcdReady = false;

// PCF8574 backlight is on/off, not a brightness ramp; keep the two named levels
// in one place so enable/disable can't drift apart.
static const uint8_t LCD_BACKLIGHT_ON  = 255;
static const uint8_t LCD_BACKLIGHT_OFF = 0;

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
// Loaded in initLCD() after setBacklight():
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

void initLCD() {
    // Ask the bus manager for a bus rather than calling Wire.begin() ourselves.
    // It picks the bus (honoring `BUS I2C <n>`) and reserves SDA/SCL, so an input
    // can never be handed a pin the display is clocking.
    TwoWire* wire = acquireI2C();
    if (!wire) {
        msg.debug.error(TAG_DISPLAY, "LCD: no I2C bus");
        return;
    }

    lcd.begin(LCD_COLS, LCD_ROWS, *wire);

    // LiquidCrystal_PCF8574::begin() calls _i2cPort->begin() itself, and
    // TwoWire::begin() re-inits the peripheral at 100 kHz (WireIMXRT.cpp:24 on
    // Teensy 4.x; twi_init()'s TWI_FREQ on AVR). Restore the configured clock, or
    // the bus silently runs at 100 kHz while `BUS I2C` reports 400 — the exact
    // "status disagrees with the peripheral" failure this module was fixed for,
    // and a slowdown inflicted on every other client sharing the bus.
    wire->setClock((uint32_t)systemConfig.buses.i2c_clock * 1000UL);

    lcdReady = true;
    lcd.setBacklight(LCD_BACKLIGHT_ON);
    // No createChar(): nothing is loaded into CGRAM (#579, see above).
    lcd.clear();
    msg.debug.info(TAG_DISPLAY, "LCD initialized");
}

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
    if (!lcdReady) return;
    lcd.setCursor(lcdCellCol(line), lcdCellRow(line));

    // Text this cell may use. The left cell's last column stays blank as the
    // gap before the right cell (the icon column used to be that gap, #579).
    const int width = lcdCellTextWidth(line);
    int charsPrinted = 0;

    if (ptr->flags.display) {
        // Print sensor abbreviation
        if (charsPrinted < width) {
            int abbrLen = strlen(ptr->abbrName);
            int charsAvailable = width - charsPrinted;
            if (abbrLen > charsAvailable) abbrLen = charsAvailable;
            for (int i = 0; i < abbrLen; i++) {
                lcd.print(ptr->abbrName[i]);
            }
            charsPrinted += abbrLen;
        }

        // Print colon
        if (charsPrinted < width) {
            lcd.print(":");
            charsPrinted++;
        }

        // If sensor is not enabled, show "CFG"
        if (!ptr->flags.isEnabled) {
            if (charsPrinted < width) {
                int remaining = width - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("CFG");
                } else {
                    // Print as much as we can fit
                    const char* msg = "CFG";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
        }
        // Check for invalid values (NaN)
        else if (isnan(ptr->value)) {
            if (charsPrinted < width) {
                int remaining = width - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("ERR");
                } else {
                    const char* msg = "ERR";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
        }
        // Not read yet since it was configured: show "---". A reading of 0 is
        // real (0 bar, 0 RPM, a low float switch) and is displayed (#481).
        else if (!ptr->flags.hasReading) {
            if (charsPrinted < width) {
                int remaining = width - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("---");
                } else {
                    const char* msg = "---";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
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

            // Print value with appropriate precision (if space available)
            if (charsPrinted < width) {
                // Format the value to a string first to check length
                char valBuffer[12];
                dtostrf(displayValue, 1, decimals, valBuffer);
                int valLen = strlen(valBuffer);
                int charsAvailable = width - charsPrinted;

                // If value + unit symbols won't fit, truncate
                if (valLen > charsAvailable) {
                    valLen = charsAvailable;
                }

                for (int i = 0; i < valLen; i++) {
                    lcd.print(valBuffer[i]);
                    charsPrinted++;
                }
            }

            // Unit symbol from the registry, only if it fits whole: a clipped
            // "rp" or "ba" reads as a different unit. Temperatures get the
            // LCD's built-in degree sign first (°C / °F); with room for the
            // sign alone it's printed without the letter, as before (#579).
            const char* symPtr = unitInfo ? (const char*)pgm_read_ptr(&unitInfo->symbol) : nullptr;
            const int symLen = symPtr ? (int)strlen_P(symPtr) : 0;
            const bool isTemp = (measType == MEASURE_TEMPERATURE);
            const int unitLen = symLen + (isTemp ? 1 : 0);
            const int room = width - charsPrinted;
            if (unitLen > 0 && unitLen <= room) {
                if (isTemp) lcd.write(LCD_DEGREE_CHAR);
                for (int i = 0; i < symLen; i++) lcd.print((char)pgm_read_byte(symPtr + i));
                charsPrinted += unitLen;
            } else if (isTemp && room >= 1) {
                lcd.write(LCD_DEGREE_CHAR);
                charsPrinted++;
            }
        }
    }

    // Pad remaining space in the column to clear old data
    for (int i = charsPrinted; i < LCD_COLUMNS_PER_SENSOR; i++) {
        lcd.print(" ");
    }
}

// Blank one grid cell (a page with fewer than 8 inputs, or an input that went
// away, must not leave a stale reading behind).
static void clearCell(byte line) {
    lcd.setCursor(lcdCellCol(line), lcdCellRow(line));
    for (int i = 0; i < LCD_COLUMNS_PER_SENSOR; i++) lcd.print(" ");
}

void updateLCD(Input** inputs, int numInputs) {
    if (!lcdReady) return;
    currentLine = 0;

    static bool firstSensorShown = false;

    if (numInputs == 0) {
        // Readings were on screen and every input is now hidden (DISPLAY
        // DISABLE) or gone: blank the grid rather than freeze them there (#490
        // review). Before any input was shown, it's the CONFIG MODE message.
        if (firstSensorShown) {
            for (uint8_t line = 0; line < LCD_CELLS; line++) clearCell(line);
            return;
        }
        static bool configMsgShown = false;
        if (!configMsgShown) {
            showConfigModeMessage();
            configMsgShown = true;
        }
        return;
    }

    // Clear the CONFIG MODE message when first sensor is configured
    if (!firstSensorShown) {
        lcd.clear();
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

void clearLCD() {
    if (!lcdReady) return;
    lcd.clear();
}

void showConfigModeMessage() {
    if (!lcdReady) return;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CONFIG MODE");
    lcd.setCursor(0, 1);
    lcd.print("Use serial console");
}

void enableLCD() {
    if (!lcdReady) return;
    lcd.setBacklight(LCD_BACKLIGHT_ON);
}

void disableLCD() {
    if (!lcdReady) return;
    lcd.setBacklight(LCD_BACKLIGHT_OFF);
    lcd.clear();
}

#else

void initLCD() {}
void displaySensor(Input *ptr, byte line) {}
void updateLCD(Input** inputs, int numInputs) {}
void clearLCD() {}
void showConfigModeMessage() {}
void enableLCD() {}
void disableLCD() {}

#endif
