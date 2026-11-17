/*
 * cmd_bus.cpp - BUS command (I2C/SPI/CAN/SERIAL configuration)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../inputs/input_manager.h"
#include "../inputs/input.h"
#include "serial_config.h"
#include "../version.h"
#include "../core/system_mode.h"
#include "../config/system_config.h"
#include "../config/json_config.h"
#include "../core/message_router.h"
#include "../core/message_api.h"
#include "../core/log_filter.h"
#include "../core/log_tags.h"
#include "../sensors/units_registry.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../core/platform.h"
#include "../config/json_registry.h"
#include "../buses/bus_manager.h"
#include "../buses/can_bus.h"
#include "../hal/bus_defaults.h"
#include "../buses/serial_manager.h"
#if INCLUDE_FRAMED_TRANSPORT
#include "../transports/transport_mux.h"
extern MuxTransport hwSerial1;   // main.cpp -- the nRF coprocessor link
#endif
#include "../buses/pinctrl/pin_registry.h"
#include "../outputs/table/output_base.h"
#include "../outputs/table/output_table.h"
#include "../outputs/formats/obd_query.h"
#include "../outputs/display/display_manager.h"
#if INCLUDE_RELAY_OUTPUT
#endif
#if INCLUDE_ELM327
#include "../outputs/formats/output_elm327.h"
#endif
#if INCLUDE_TEST_MODE
#include "../sensors/simulate/test_mode.h"
#endif
#if INCLUDE_CAN
#include "../sensors/can/can_scan.h"
#include "../sensors/can/input_can.h"
#include "../sensors/can_sensor_library/standard_pids.h"
#if INCLUDE_CAN && SUPPORTS_JSON_EXPORT
#include "../sensors/can/input_can.h"
#include "../sensors/can/can_status_json.h"
#include "../sensors/can/can_frame_cache.h"
#include "../buses/pinctrl/pin_label.h"
#include "../core/watchdog.h"
#endif
#endif
#include <string.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

#include <ctype.h>

// BUS I2C subcommand handler
static FLASHMEM int bus_i2c(int argc, const char* const* argv, int /*tokenIndex*/) {
    // BUS I2C (no arguments) - display I2C status
    if (argc == 2) {
        displayI2CStatus();
        return 0;
    }

    // BUS I2C CLOCK <kHz>
    if (streq_P(argv[2], PSTR("CLOCK"))) {
        if (argc < 4) {
            msg.control.println(F("ERROR: CLOCK requires a speed in kHz"));
            msg.control.println(F("  Usage: BUS I2C CLOCK <100|400|1000>"));
            return 1;
        }

        uint16_t clock = atoi(argv[3]);
        if (clock != 100 && clock != 400 && clock != 1000) {
            msg.control.println(F("ERROR: I2C clock must be 100, 400, or 1000 kHz"));
            return 1;
        }

        systemConfig.buses.i2c_clock = clock;
        msg.control.print(F("I2C clock set to "));
        msg.control.print(clock);
        msg.control.println(F("kHz"));
        msg.control.println(F("Note: Takes effect on next reboot"));
        printSaveReminder();
        return 0;
    }

    // BUS I2C NONE - Disable I2C bus
    if (streq_P(argv[2], PSTR("NONE"))) {
        systemConfig.buses.active_i2c = 0xFF;
        msg.control.println(F("I2C bus set to NONE"));
        msg.control.println(F("I2C pins will be free on next reboot"));
        printSaveReminder();
        return 0;
    }

    // BUS I2C <0|1|2> - Select bus
    uint8_t bus_id = atoi(argv[2]);
    if (bus_id >= NUM_I2C_BUSES) {
        msg.control.print(F("ERROR: I2C bus "));
        msg.control.print(bus_id);
        msg.control.print(F(" not available (0-"));
        msg.control.print(NUM_I2C_BUSES - 1);
        msg.control.println(F(")"));
        return 1;
    }

    // Same guard as DISPLAY TYPE LCD: selecting a bus whose SDA/SCL an input or
    // Action already occupies used to be accepted silently, leaving the clash to
    // surface as a dropped input at the next boot.
    if (i2cBusPinsConflict(bus_id)) return 1;

    systemConfig.buses.active_i2c = bus_id;
    busSelectionAdmitted(0, bus_id);
    msg.control.print(F("I2C bus set to "));
    msg.control.print(getI2CBusName(bus_id));
    msg.control.print(F(" (SDA="));
    msg.control.print(getDefaultI2CSDA(bus_id));
    msg.control.print(F(", SCL="));
    msg.control.print(getDefaultI2CSCL(bus_id));
    msg.control.println(F(")"));
    msg.control.println(F("Note: Takes effect on next reboot"));
    printSaveReminder();
    return 0;
}

// BUS SPI subcommand handler
static FLASHMEM int bus_spi(int argc, const char* const* argv, int /*tokenIndex*/) {
    // BUS SPI (no arguments) - display SPI status
    if (argc == 2) {
        displaySPIStatus();
        return 0;
    }

    // BUS SPI CLOCK <Hz>
    if (streq_P(argv[2], PSTR("CLOCK"))) {
        if (argc < 4) {
            msg.control.println(F("ERROR: CLOCK requires a speed in Hz"));
            msg.control.println(F("  Usage: BUS SPI CLOCK <Hz>"));
            msg.control.println(F("  Example: BUS SPI CLOCK 4000000  (4MHz)"));
            return 1;
        }

        uint32_t clock = atol(argv[3]);
        if (clock < 100000 || clock > 50000000) {
            msg.control.println(F("ERROR: SPI clock must be 100000-50000000 Hz"));
            return 1;
        }

        systemConfig.buses.spi_clock = clock;
        msg.control.print(F("SPI clock set to "));
        msg.control.print(clock / 1000000.0, 1);
        msg.control.println(F("MHz"));
        msg.control.println(F("Note: Takes effect on next transaction"));
        printSaveReminder();
        return 0;
    }

    // BUS SPI NONE - Disable SPI bus
    if (streq_P(argv[2], PSTR("NONE"))) {
        systemConfig.buses.active_spi = 0xFF;
        msg.control.println(F("SPI bus set to NONE"));
        msg.control.println(F("SPI pins will be free on next reboot"));
        printSaveReminder();
        return 0;
    }

    // BUS SPI <0|1|2> - Select bus
    uint8_t bus_id = atoi(argv[2]);
    if (bus_id >= NUM_SPI_BUSES) {
        msg.control.print(F("ERROR: SPI bus "));
        msg.control.print(bus_id);
        msg.control.print(F(" not available (0-"));
        msg.control.print(NUM_SPI_BUSES - 1);
        msg.control.println(F(")"));
        return 1;
    }

    // Same guard as BUS I2C: an input or Action already on MOSI/MISO/SCK would
    // otherwise be accepted silently and surface as a dropped input at the next
    // boot, now that a stored thermocouple brings SPI up on its own (#358).
    if (spiBusPinsConflict(bus_id)) return 1;

    systemConfig.buses.active_spi = bus_id;
    busSelectionAdmitted(1, bus_id);
    msg.control.print(F("SPI bus set to "));
    msg.control.print(getSPIBusName(bus_id));
    msg.control.print(F(" (MOSI="));
    msg.control.print(getDefaultSPIMOSI(bus_id));
    msg.control.print(F(", MISO="));
    msg.control.print(getDefaultSPIMISO(bus_id));
    msg.control.print(F(", SCK="));
    msg.control.print(getDefaultSPISCK(bus_id));
    msg.control.println(F(")"));
    msg.control.println(F("Note: Takes effect on next reboot"));
    printSaveReminder();
    return 0;
}

#if INCLUDE_CAN
// CAN1/CAN2/CAN3 -> 0..2; 0xFF otherwise
static uint8_t parseCanBusToken(const char* t) {
    if (strncasecmp(t, "CAN", 3) != 0 || t[3] < '1' || t[3] > '3' || t[4] != '\0') return 0xFF;
    return (uint8_t)(t[3] - '1');
}

// What the bus manager did with a bus, after a change
static FLASHMEM void printCanApply(uint8_t bus, CanAttach r) {
    msg.control.print(getCANBusName(bus));
    if (r == CAN_ATTACH_WAITING)      msg.control.println(F(": did not start; BUS CAN shows it waiting"));
    else if (r == CAN_ATTACH_NO_PINS) msg.control.println(F(": its pins are taken; see BUS CAN"));
    else if (canBusUp(bus))           msg.control.println(F(": running"));
    else                              msg.control.println(F(": starts when CAN input or OBD attaches"));
}

// The OBD output sends on this bus (it is enabled with this bus as transport)
static bool obdUsesBus(uint8_t bus) {
    return systemConfig.buses.obdBus == bus && outputIsEnabled(OUT_ID_OBD);
}
#endif

// BUS CAN <CAN1|CAN2|CAN3> ENABLE|LISTEN|DISABLE | BAUDRATE <bps> | INPUT ENABLE|DISABLE
// One set of settings per bus (#612 phase 2a, ARCHITECTURE §14.7). Changes
// apply at once: a running bus restarts in its new rate or mode.
static FLASHMEM int bus_can(int argc, const char* const* argv, int /*tokenIndex*/) {
#if NUM_CAN_BUSES == 0 || !INCLUDE_CAN
    (void)argc; (void)argv;
    msg.control.println(F("ERROR: No CAN buses available on this platform"));
    return 1;
#else
    if (argc == 2 || (argc == 3 && streq_P(argv[2], PSTR("STATUS")))) {
        displayCANStatus();
        return 0;
    }
    const uint8_t bus = parseCanBusToken(argv[2]);
    if (bus == 0xFF || argc < 4) {
        msg.control.println(F("ERROR: Usage: BUS CAN <CAN1|CAN2|CAN3> ENABLE|LISTEN|DISABLE|BAUDRATE <bps>|INPUT ENABLE|DISABLE"));
        return 1;
    }
    if (bus >= NUM_CAN_BUSES) {
        msg.control.print(F("ERROR: "));
        msg.control.print(argv[2]);
        msg.control.println(F(" not available on this platform"));
        return 1;
    }
    CanBusSettings& cs = systemConfig.buses.can[bus];
    const char* verb = argv[3];

    if (streq_P(verb, PSTR("ENABLE")) || streq_P(verb, PSTR("LISTEN"))) {
        const uint8_t mode = streq_P(verb, PSTR("LISTEN")) ? CAN_BUS_LISTEN : CAN_BUS_NORMAL;
        if (mode == CAN_BUS_LISTEN && obdUsesBus(bus)) {
            msg.control.println(F("ERROR: the OBD output sends on this bus, and LISTEN never transmits"));
            msg.control.println(F("  OUTPUT 0 DISABLE first, or keep the bus at ENABLE"));
            return 1;
        }
        // Pin ownership last, so a refusal leaves systemConfig untouched (#390, #563)
        if (cs.mode == CAN_BUS_OFF && canBusPinsConflict(bus)) return 1;
        cs.mode = mode;
        busSelectionAdmitted(2, bus);
        printCanApply(bus, canBusApply(bus));
        // Something set to use this bus but not attached (it was off, or its
        // start failed) attaches now
        if (systemConfig.buses.canInputBus == bus && canInputAttachedBus() != bus) initCANInput();
        if (obdUsesBus(bus) && canOutputAttachedBus() != bus) outputSetEnabledById(OUT_ID_OBD, true);
        printSaveReminder();
        return 0;
    }

    if (streq_P(verb, PSTR("DISABLE"))) {
        if (systemConfig.buses.canInputBus == bus || obdUsesBus(bus)) {
            const bool in = systemConfig.buses.canInputBus == bus;
            msg.control.print(F("ERROR: in use by"));
            if (in) msg.control.print(F(" CAN input"));
            if (in && obdUsesBus(bus)) msg.control.print(F(" and"));
            if (obdUsesBus(bus)) msg.control.print(F(" the OBD output"));
            msg.control.println();
            msg.control.println(F("  BUS CAN <bus> INPUT DISABLE / OUTPUT 0 DISABLE first"));
            return 1;
        }
        cs.mode = CAN_BUS_OFF;
        canBusApply(bus);
        msg.control.print(getCANBusName(bus));
        msg.control.println(F(" off (its pins are free after a reboot)"));
        printSaveReminder();
        return 0;
    }

    if (streq_P(verb, PSTR("BAUDRATE"))) {
        const uint8_t idx = argc >= 5 ? canBaudIndexOf((uint32_t)atol(argv[4])) : 0xFF;
        if (idx == 0xFF) {
            msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
            return 1;
        }
        cs.baudIndex = idx;
        if (cs.mode == CAN_BUS_OFF) {
            msg.control.print(getCANBusName(bus));
            msg.control.println(F(" is off; the rate applies once it is enabled"));
        } else {
            printCanApply(bus, canBusApply(bus));
        }
        printSaveReminder();
        return 0;
    }

    if (streq_P(verb, PSTR("INPUT")) && argc >= 5) {
        if (streq_P(argv[4], PSTR("ENABLE"))) {
            if (cs.mode == CAN_BUS_OFF) {
                msg.control.print(F("ERROR: "));
                msg.control.print(getCANBusName(bus));
                msg.control.print(F(" is off: BUS CAN "));
                msg.control.print(getCANBusName(bus));
                msg.control.println(F(" ENABLE (or LISTEN) first"));
                return 1;
            }
            systemConfig.buses.canInputBus = bus;   // one bus; moving it detaches the old one
            initCANInput();
            msg.control.print(F("CAN input on "));
            printCanApply(bus, canBusUp(bus) ? CAN_ATTACH_OK : (CanAttach)(canBusState(bus) == CAN_STATE_NO_PINS
                                                                  ? CAN_ATTACH_NO_PINS : CAN_ATTACH_WAITING));
            printSaveReminder();
            return 0;
        }
        if (streq_P(argv[4], PSTR("DISABLE"))) {
            if (systemConfig.buses.canInputBus != bus) {
                msg.control.print(F("ERROR: CAN input is not on "));
                msg.control.println(getCANBusName(bus));
                return 1;
            }
            systemConfig.buses.canInputBus = 0xFF;
            shutdownCANInput();
            msg.control.println(F("CAN input off"));
            printSaveReminder();
            return 0;
        }
    }

    msg.control.println(F("ERROR: Usage: BUS CAN <CAN1|CAN2|CAN3> ENABLE|LISTEN|DISABLE|BAUDRATE <bps>|INPUT ENABLE|DISABLE"));
    return 1;
#endif
}

// BUS SERIAL subcommand handler
static FLASHMEM int bus_serial(int argc, const char* const* argv, int /*tokenIndex*/) {
#if NUM_SERIAL_PORTS == 0
    msg.control.println(F("ERROR: No serial ports available on this platform"));
    return 1;
#else
    // BUS SERIAL (no arguments) - display all serial port status
    if (argc == 2) {
        displaySerialStatus();
        return 0;
    }

    // Parse port number
    uint8_t port_id = atoi(argv[2]);

    // Check if it's a valid port number (1-8)
    if (port_id >= 1 && port_id <= 8) {
        // Validate port exists on this platform
        if (port_id > NUM_SERIAL_PORTS) {
            msg.control.print(F("ERROR: Serial"));
            msg.control.print(port_id);
            msg.control.print(F(" not available (1-"));
            msg.control.print(NUM_SERIAL_PORTS);
            msg.control.println(F(")"));
            return 1;
        }

        // BUS SERIAL <port> (no subcommand) - show port status
        if (argc == 3) {
            displaySerialPortStatus(port_id);
#if INCLUDE_FRAMED_TRANSPORT
            // Framed RX health (#449): text the nRF delivered that the CLI
            // never saw because the RX ring was full. Non-zero after a bulk
            // import means the payload had holes -- the import is what to
            // fix, not the parser.
            if (port_id == 1 && hwSerial1.isFramedEnabled()) {
                msg.control.print(F("  Framed RX dropped: "));
                msg.control.print(hwSerial1.framedRxDropped());
                // Cumulative since boot -- there is no reset short of one,
                // so say so rather than let a stale count from an earlier
                // bad transfer read as a fresh one.
                msg.control.println(F(" B (since boot)"));
            }
#endif
            return 0;
        }

        // BUS SERIAL <port> ENABLE [baudrate]
        if (streq_P(argv[3], PSTR("ENABLE"))) {
            // Use saved baud rate from config, or 115200 if not set
            uint8_t baud_idx = systemConfig.serial.baudrate_index[port_id - 1];

            if (argc >= 5) {
                uint32_t baudrate = atol(argv[4]);
                baud_idx = getBaudRateIndex(baudrate);
                if (getBaudRateFromIndex(baud_idx) != baudrate) {
                    msg.control.print(F("WARNING: Baud rate "));
                    msg.control.print(baudrate);
                    msg.control.print(F(" not supported, using "));
                    msg.control.println(getBaudRateFromIndex(baud_idx));
                }
            }

            if (enableSerialPort(port_id, baud_idx)) {
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.print(F(" enabled @ "));
                msg.control.print(getBaudRateString(baud_idx));
                msg.control.print(F(" baud (RX="));
                msg.control.print(getDefaultSerialRX(port_id));
                msg.control.print(F(", TX="));
                msg.control.print(getDefaultSerialTX(port_id));
                msg.control.println(F(")"));
                printSaveReminder();
            } else {
                msg.control.print(F("ERROR: Failed to enable Serial"));
                msg.control.println(port_id);
            }
            return 0;
        }

        // BUS SERIAL <port> DISABLE
        if (streq_P(argv[3], PSTR("DISABLE"))) {
            if (disableSerialPort(port_id)) {
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.println(F(" disabled"));
                printSaveReminder();
            }
            return 0;
        }

        // BUS SERIAL <port> BAUDRATE <rate>
        if (streq_P(argv[3], PSTR("BAUDRATE"))) {
            if (argc < 5) {
                msg.control.println(F("ERROR: BAUDRATE requires a speed"));
                msg.control.println(F("  Usage: BUS SERIAL <port> BAUDRATE <rate>"));
                msg.control.println(F("  Valid: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600"));
                return 1;
            }

            uint32_t baudrate = atol(argv[4]);
            uint8_t baud_idx = getBaudRateIndex(baudrate);

            if (getBaudRateFromIndex(baud_idx) != baudrate) {
                msg.control.print(F("WARNING: Baud rate "));
                msg.control.print(baudrate);
                msg.control.print(F(" not supported, using "));
                msg.control.println(getBaudRateFromIndex(baud_idx));
            }

            systemConfig.serial.baudrate_index[port_id - 1] = baud_idx;
            msg.control.print(F("Serial"));
            msg.control.print(port_id);
            msg.control.print(F(" baudrate set to "));
            msg.control.println(getBaudRateString(baud_idx));
            msg.control.println(F("Note: Takes effect on next reboot"));
            printSaveReminder();
            return 0;
        }

        // BUS SERIAL <port> FRAMED ENABLE|DISABLE
        if (streq_P(argv[3], PSTR("FRAMED"))) {
#if !INCLUDE_FRAMED_TRANSPORT
            msg.control.println(F("ERROR: framed mode not supported on this build"));
            return 1;
#else
            if (argc < 5) {
                msg.control.println(F("ERROR: FRAMED requires ENABLE or DISABLE"));
                msg.control.println(F("  Usage: BUS SERIAL <port> FRAMED ENABLE|DISABLE"));
                msg.control.println(F("  Enables the binary framing protocol for the nRF52840"));
                msg.control.println(F("  coprocessor link. Leave disabled for HM-10 / stock"));
                msg.control.println(F("  Bluefruit / any non-preOBD-firmware BLE module."));
                return 1;
            }
            const uint8_t port_bit = (1 << (port_id - 1));
            if (streq_P(argv[4], PSTR("ENABLE"))) {
                systemConfig.serial.framed_enabled_mask |= port_bit;
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.println(F(" framed mode ENABLED"));
                msg.control.println(F("Note: Takes effect on next reboot"));
                printSaveReminder();
                return 0;
            }
            if (streq_P(argv[4], PSTR("DISABLE"))) {
                systemConfig.serial.framed_enabled_mask &= ~port_bit;
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.println(F(" framed mode DISABLED"));
                msg.control.println(F("Note: Takes effect on next reboot"));
                printSaveReminder();
                return 0;
            }
            msg.control.println(F("ERROR: expected ENABLE or DISABLE"));
            return 1;
#endif // INCLUDE_FRAMED_TRANSPORT
        }

        // Unknown subcommand for port
        msg.control.print(F("ERROR: Unknown command '"));
        msg.control.print(argv[3]);
        msg.control.println(F("'"));
        msg.control.println(F("  Valid: ENABLE, DISABLE, BAUDRATE"));
        return 1;
    }

    // Not a port number - unknown subcommand
    msg.control.print(F("ERROR: Unknown serial command '"));
    msg.control.print(argv[2]);
    msg.control.println(F("'"));
    msg.control.println(F("  Usage: BUS SERIAL [1-8] [ENABLE|DISABLE|BAUDRATE <rate>]"));
    return 1;
#endif
}


// BUS subcommand tokens (PROGMEM)
static const char PSTR_BUS_I2C[]    PROGMEM = "I2C";
static const char PSTR_BUS_SPI[]    PROGMEM = "SPI";
static const char PSTR_BUS_CAN[]    PROGMEM = "CAN";
static const char PSTR_BUS_SERIAL[] PROGMEM = "SERIAL";

// runModeAllowed=false across the board, and since #510 these flags ARE the
// gate: BUS is configModeOnly=false at the COMMANDS[] level so that the
// read-only BUS CAN JSON (caught in cmd_bus() before this table) answers in
// RUN. dispatchSubcommand() refuses every leaf here in RUN and bumps
// configGeneration for it in CONFIG, exactly as the parent flag did.
// Each help block is printed verbatim by printSubcommandTable, so it bakes in
// the "BUS <leaf>" prefix and lists every supported sub-form. Adding a new
// sub-form is a one-string edit and HELP BUS / bare BUS stay structurally in
// sync (issue #189).
static const char PSTR_HELP_BUS_I2C[] PROGMEM =
    "  BUS I2C                       Show all I2C bus status\n"
    "  BUS I2C [0|1|2]               Select I2C bus (Wire/Wire1/Wire2)\n"
    "  BUS I2C NONE                  Disable I2C bus (frees pins on next reboot)\n"
    "  BUS I2C CLOCK <kHz>           Set I2C clock (100, 400, 1000)";
static const char PSTR_HELP_BUS_SPI[] PROGMEM =
    "  BUS SPI                       Show all SPI bus status\n"
    "  BUS SPI [0|1|2]               Select SPI bus (SPI/SPI1/SPI2)\n"
    "  BUS SPI NONE                  Disable SPI bus (frees pins on next reboot)\n"
    "  BUS SPI CLOCK <Hz>            Set SPI clock (e.g. 4000000)";
static const char PSTR_HELP_BUS_CAN[] PROGMEM =
    "  BUS CAN [STATUS]              Show each CAN bus (any mode)\n"
    "  BUS CAN JSON                  Live CAN status as JSON (any mode)\n"
    "  BUS CAN <bus> ENABLE|LISTEN|DISABLE  Bus on (LISTEN: never transmits) or off\n"
    "  BUS CAN <bus> BAUDRATE <bps>  125000, 250000, 500000 or 1000000\n"
    "  BUS CAN <bus> INPUT ENABLE|DISABLE   CAN input reads this bus\n"
    "  (<bus> = CAN1|CAN2|CAN3; OBD's bus: OUTPUT ADD OBD CAN<n>)";
static const char PSTR_HELP_BUS_SERIAL[] PROGMEM =
    "  BUS SERIAL                    Show all serial port status\n"
    "  BUS SERIAL <1-8>              Show specific port status\n"
    "  BUS SERIAL <1-8> ENABLE [baud]   Enable serial port\n"
    "  BUS SERIAL <1-8> DISABLE      Disable serial port\n"
    "  BUS SERIAL <1-8> BAUDRATE <rate> Set baud rate";

static const Subcommand BUS_SUBCOMMANDS[] PROGMEM = {
    { PSTR_BUS_I2C,    bus_i2c,    false, PSTR_HELP_BUS_I2C    },
    { PSTR_BUS_SPI,    bus_spi,    false, PSTR_HELP_BUS_SPI    },
    { PSTR_BUS_CAN,    bus_can,    false, PSTR_HELP_BUS_CAN    },
    { PSTR_BUS_SERIAL, bus_serial, false, PSTR_HELP_BUS_SERIAL },
};
static const uint8_t NUM_BUS_SUBCOMMANDS = sizeof(BUS_SUBCOMMANDS) / sizeof(Subcommand);

FLASHMEM void printHelpBus() {
    msg.control.println();
    msg.control.println(F("=== BUS Commands ==="));
    msg.control.println(F("Configure I2C, SPI, CAN buses and Serial ports"));
    msg.control.println();
    printSubcommandTable(BUS_SUBCOMMANDS, NUM_BUS_SUBCOMMANDS);
    msg.control.println();
}

#if INCLUDE_CAN && SUPPORTS_JSON_EXPORT
// BUS CAN JSON (#510): configured + live CAN state for the webapp/app.
static FLASHMEM void emitBusCanJson() {
    const uint32_t now = millis();
    CanBusSnapshot s;
    memset(&s, 0, sizeof(s));
    s.buses = NUM_CAN_BUSES;
    s.maxScanResults = MAX_SCAN_RESULTS;
    for (uint8_t b = 0; b < NUM_CAN_BUSES && b < 3; b++) {
        s.bus[b].mode = systemConfig.buses.can[b].mode;
        s.bus[b].baudrate = canBaudFromIndex(systemConfig.buses.can[b].baudIndex);
        s.bus[b].state = canBusState(b);
        s.bus[b].input = (canBusConsumers(b) & (1u << CANC_INPUT)) != 0;
        s.bus[b].obd = (canBusConsumers(b) & (1u << CANC_OBD)) != 0;
    }
    s.inBus = systemConfig.buses.canInputBus;
    getCANInputStatus(s, now);
    // poll.active only when something is actually being requested
    bool anyPolled = false;
    for (uint8_t i = 0; i < MAX_INPUTS && !anyPolled; i++) {
        if (inputs[i].pin != 0xFF && canInputIsPolled(inputs[i])) anyPolled = true;
    }
    s.pollActive = s.pollActive && anyPolled;
    s.pollBackoff = s.pollBackoff && anyPolled;

    JsonDocument doc;
    JsonArray imports = canBusStatusToJson(s, doc.to<JsonObject>());
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        const Input& in = inputs[i];
        if (in.pin < PIN_CAN_BASE || in.pin > PIN_CAN_MAX) continue;
        if (in.calibrationType != CAL_CAN_IMPORT || !in.flags.useCustomCalibration) continue;
        char label[8];
        formatPinLabel(in.pin, label, sizeof(label));
        CanImportSnapshot imp;
        imp.pin = in.pin;
        imp.pinLabel = label;
        imp.sourceCanId = in.customCalibration.can.source_can_id;
        imp.sourcePid = in.customCalibration.can.source_pid;
        imp.dataOffset = in.customCalibration.can.data_offset;
        imp.polled = canInputIsPolled(in);
        CANFrameEntry* e = getCANCacheEntry(imp.sourceCanId, imp.sourcePid);
        imp.haveFrame = e != nullptr && e->valid;
        imp.ageMs = imp.haveFrame ? now - e->timestamp_ms : 0;
        canImportToJson(imp, imports.add<JsonObject>());
    }
    msg.control.println();
    WatchdogKickingPrint out(msg.control);
    serializeJson(doc, out);
    msg.control.println();
}
#endif

FLASHMEM int cmd_bus(int argc, const char* const* argv) {
    // BUS CAN and BUS CAN JSON: read-only, answer in RUN and never bump
    // configGeneration (every other BUS form is gated by BUS_SUBCOMMANDS below)
    if (argc >= 2 && streq_P(argv[1], PSTR("CAN")) && (argc == 2 || (argc == 3 && streq_P(argv[2], PSTR("STATUS"))))) {
        displayCANStatus();
        return 0;
    }
    if (argc == 3 && streq_P(argv[1], PSTR("CAN")) && streq_P(argv[2], PSTR("JSON"))) {
#if INCLUDE_CAN && SUPPORTS_JSON_EXPORT
        emitBusCanJson();
        return 0;
#else
        msg.control.println(F("ERROR: JSON export not supported on this platform"));
        return 1;
#endif
    }

    if (argc < 2) {
        // Bare "BUS" — delegate to the same printer HELP BUS uses, so the
        // two stay structurally in sync (issue #189: BUS help drift).
        printHelpBus();
        return 0;
    }

    const char* busType = argv[1];

    // Dispatch on bus type. BUS_SUBCOMMANDS lives in PROGMEM on AVR.
    return dispatchSubcommand(BUS_SUBCOMMANDS, NUM_BUS_SUBCOMMANDS,
                              busType, "BUS", argc, argv, 1);
}

// ===== LOG COMMAND =====

#if INCLUDE_SELFTEST
FLASHMEM int selftestBusTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_BUS_SUBCOMMANDS; i++) {
        const char*        tok = (const char*)pgm_read_ptr(&BUS_SUBCOMMANDS[i].token);
        SubcommandHandler  h   = (SubcommandHandler)pgm_read_ptr(&BUS_SUBCOMMANDS[i].handler);
        failures += validateDispatchEntry(F("BUS"), i, tok, (const void*)h, true);
    }
    return failures;
}
#endif
