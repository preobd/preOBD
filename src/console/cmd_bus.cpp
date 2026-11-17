/*
 * cmd_bus.cpp - BUS command (I2C/SPI/CAN/SERIAL configuration)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../inputs/input_manager.h"
#include "../inputs/input.h"
#include "../version.h"
#include "../core/system_mode.h"
#include "../config/system_config.h"
#include "../config/json_config.h"
#include "../core/message_api.h"
#include "console_routing.h"
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
#include "../outputs/table/output_admission.h"
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
#include "../sensors/can/can_poll.h"   // BUS CAN POLL range (#633)
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
    else                              msg.control.println(F(": starts when CAN input or an output attaches"));
}

// The enabled CAN-sending outputs on this bus (OBD, PREOBD): a bit per output id
static bool canSenderUsesBus(uint8_t bus) { return outputCanSendersOnBus(bus) != 0; }
static uint8_t firstSender(RouteMask m) {
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) if (m & (1u << id)) return id;
    return OUT_ID_NONE;
}

// "the OBD output", or "output <id> (PREOBD)", one per sender, joined by " and "
static FLASHMEM void printSenderNames(uint8_t bus) {
    const RouteMask m = outputCanSendersOnBus(bus);
    bool first = true;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (!(m & (1u << id))) continue;
        if (!first) msg.control.print(F(" and"));
        first = false;
        msg.control.print(F(" "));
#if INCLUDE_CAN_LAYOUTS
        if (outputFormatOf(id) != FMT_OBD) {
            msg.control.print(F("output "));
            msg.control.print(id);
            msg.control.print(F(" ("));
            msg.control.print(outputFormatName(outputFormatOf(id)));
            msg.control.print(F(")"));
            continue;
        }
#endif
        msg.control.print(F("the OBD output"));
    }
}

// An enabled sender on this bus that isn't running (the bus was off, or CAN
// input held it, #632) starts now; ENABLE prints why if it still can't
static void startWaitingSenders(uint8_t bus) {
    const RouteMask m = outputCanSendersOnBus(bus);
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (!(m & (1u << id)) || outputLiveById(id)) continue;
        outputSetEnabledById(id, true);
    }
}
#endif

#if INCLUDE_CAN
// BUS CAN POLL <ms>: the least time between CAN input polling rounds (#633).
// Takes effect at once (input_can.cpp reads it on every call); a value that
// waits for CAN input or a NORMAL bus is still stored.
static FLASHMEM int busCanPoll(int argc, const char* const* argv) {
    char* end = nullptr;
    const long ms = argc == 4 ? strtol(argv[3], &end, 10) : 0;
    if (argc != 4 || end == argv[3] || *end != '\0' || !canPollMsValid(ms)) {
        msg.control.print(F("ERROR: POLL takes "));
        msg.control.print(CAN_POLL_MS_MIN);
        msg.control.print('-');
        msg.control.print(CAN_POLL_MS_MAX);
        msg.control.println(F(" (ms)"));
        return 1;
    }
    systemConfig.buses.canPollMs = (uint16_t)ms;
    msg.control.print(F("CAN input asks the ECU at most every "));
    msg.control.print(ms);
    msg.control.println(F(" ms"));
    printSaveReminder();
    return 0;
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
    if (streq_P(argv[2], PSTR("POLL"))) return busCanPoll(argc, argv);   // before the bus token: no bus here
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
        if (mode == CAN_BUS_LISTEN && canSenderUsesBus(bus)) {
            msg.control.print(F("ERROR:"));
            printSenderNames(bus);
            msg.control.println(F(" sends on this bus, and LISTEN never transmits"));
            msg.control.println(F("  OUTPUT <id> DISABLE first, or keep the bus at ENABLE"));
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
        startWaitingSenders(bus);
        printSaveReminder();
        return 0;
    }

    if (streq_P(verb, PSTR("DISABLE"))) {
        if (systemConfig.buses.canInputBus == bus || canSenderUsesBus(bus)) {
            const bool in = systemConfig.buses.canInputBus == bus;
            msg.control.print(F("ERROR: in use by"));
            if (in) msg.control.print(F(" CAN input"));
            if (in && canSenderUsesBus(bus)) msg.control.print(F(" and"));
            printSenderNames(bus);
            msg.control.println();
            msg.control.println(F("  BUS CAN <bus> INPUT DISABLE / OUTPUT <id> DISABLE first"));
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
            // One job per CAN bus (#632): refused before anything changes, so
            // CAN input stays where it was
            const uint8_t obd = firstSender(outputCanSendersOnBus(bus));
            if (obd != OUT_ID_NONE) {
                msg.control.print(F("ERROR: output "));
                msg.control.print(obd);
#if INCLUDE_CAN_LAYOUTS
                msg.control.print(F(" ("));
                msg.control.print(outputFormatName(outputFormatOf(obd)));
                msg.control.print(F(") uses "));
#else
                msg.control.print(F(" (OBD) uses "));
#endif
                msg.control.print(getCANBusName(bus));
                msg.control.print(F(" (one job per bus): OUTPUT "));
                msg.control.print(obd);
                msg.control.println(F(" DISABLE first"));
                return 1;
            }
            const uint8_t was = systemConfig.buses.canInputBus;
            systemConfig.buses.canInputBus = bus;   // one bus; moving it detaches the old one
            initCANInput();
            msg.control.print(F("CAN input on "));
            printCanApply(bus, canBusUp(bus) ? CAN_ATTACH_OK : (CanAttach)(canBusState(bus) == CAN_STATE_NO_PINS
                                                                  ? CAN_ATTACH_NO_PINS : CAN_ATTACH_WAITING));
            if (was != 0xFF && was != bus) startWaitingSenders(was);   // senders held on the bus input left
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
            startWaitingSenders(bus);   // a sender CAN input held (#632) starts
            printSaveReminder();
            return 0;
        }
    }

    msg.control.println(F("ERROR: Usage: BUS CAN <CAN1|CAN2|CAN3> ENABLE|LISTEN|DISABLE|BAUDRATE <bps>|INPUT ENABLE|DISABLE"));
    return 1;
#endif
}

// An enabled output writes to Serial<port> (OUTPUT LIST): refuse a change
// that would pull the port from under it, naming it (#612, one validator)
static FLASHMEM bool refuseUnderOutput(uint8_t port) {
    const uint8_t id = outputOnStream((uint8_t)(OT_SERIAL1 + port - 1), true, outputRecords);
    if (id == OUT_ID_NONE) return false;
    msg.control.print(F("ERROR: output "));
    msg.control.print(id);
    msg.control.print(F(" ("));
    printOutputLabel(id);
    msg.control.print(F(") uses Serial"));
    msg.control.print(port);
    msg.control.println(F("; OUTPUT <id> DISABLE or TRANSPORT first"));
    return true;
}

// The port carries commands (a console transport or framed, #561): DISABLE
// would release its pins and flow control while the console and the
// broadcasters keep writing to it, unpaced (#646). Refuse, naming the step.
static FLASHMEM bool refuseUnderCommands(uint8_t port) {
#if INCLUDE_FRAMED_TRANSPORT
    // FRAMED DISABLE clears the setting at once but the link stays framed
    // until a reboot (main.cpp sets it at boot), and the broadcasters keep
    // sending frames: still a live link, so still refused
    if (port == 1 && isSerialPortActive(1) && hwSerial1.isFramedEnabled() &&
        !serialPortCarriesCommands(1)) {
        msg.control.println(F("ERROR: Serial1 is framed until a reboot: SAVE and SYSTEM REBOOT first"));
        return true;
    }
#endif
    if (!serialPortCarriesCommands(port)) return false;
    msg.control.print(F("ERROR: Serial"));
    msg.control.print(port);
#if INCLUDE_FRAMED_TRANSPORT
    if (systemConfig.serial.framed_enabled_mask & (1u << (port - 1))) {
        msg.control.print(F(" is framed: BUS SERIAL "));
        msg.control.print(port);
        msg.control.println(F(" FRAMED DISABLE first"));
        return true;
    }
#endif
    // One string for both console cases (CONSOLE STATUS shows which): the
    // mega2560's near PROGMEM is nearly full (#523)
    msg.control.print(F(" is a console: move it with CONSOLE TRANSPORT"));
    msg.control.println(F(" first"));
    return true;
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
                syncOutputsToConfig();   // an output waiting for this port starts now (#612)
                printSaveReminder();
            } else {
                msg.control.print(F("ERROR: Failed to enable Serial"));
                msg.control.println(port_id);
            }
            return 0;
        }

        // BUS SERIAL <port> DISABLE
        if (streq_P(argv[3], PSTR("DISABLE"))) {
            if (refuseUnderOutput(port_id)) return 1;
            if (refuseUnderCommands(port_id)) return 1;
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
                // A framed port carries the console and ELM327 over BLE, never
                // a raw stream output (#612)
                if (refuseUnderOutput(port_id)) return 1;
                // ... nor the log, as LOG TRANSPORT refuses the other way round
                const uint8_t tid = (uint8_t)(TRANSPORT_SERIAL1 + port_id - 1);
                if (planeTransportId(PLANE_DEBUG, false) == tid || planeTransportId(PLANE_DEBUG, true) == tid) {
                    msg.control.print(F("ERROR: the log uses Serial"));
                    msg.control.print(port_id);
                    msg.control.println(F("; LOG TRANSPORT first"));
                    return 1;
                }
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
                // BLE outputs ride the framed link on Serial1: allowed (ELM327
                // over BLE is on by default), but say what stops (#612 2c)
                if (port_id == 1) {
                    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
                        const OutputRecord& r = outputRecords[id];
                        if (r.format == FMT_NONE || !r.enabled || r.transport != OT_BLE) continue;
                        msg.control.print(F("NOTE: output "));
                        msg.control.print(id);
                        msg.control.print(F(" ("));
                        printOutputLabel(id);
                        msg.control.println(F(") stops running then: BLE needs the framed link"));
                    }
                }
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
    "  BUS CAN POLL <ms>             CAN input asks the ECU at most every <ms> ms (50-1000)\n"
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
#if INCLUDE_CAN_LAYOUTS
        s.bus[b].layout = (canBusConsumers(b) & (1u << CANC_LAYOUT)) != 0;
#endif
        s.bus[b].pinsFree = canBusPinsFree(b);
    }
    s.inBus = systemConfig.buses.canInputBus;
    s.pollMs = systemConfig.buses.canPollMs;
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
