/*
 * cmd_bus.cpp - BUS command (I2C/SPI/CAN/SERIAL configuration)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "input_manager.h"
#include "input.h"
#include "serial_config.h"
#include "../version.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/json_config.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include "../lib/log_filter.h"
#include "../lib/log_tags.h"
#include "../lib/units_registry.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/platform.h"
#include "../lib/json_registry.h"
#include "../lib/bus_manager.h"
#include "../lib/bus_defaults.h"
#include "../lib/serial_manager.h"
#include "../lib/pin_registry.h"
#include "../outputs/output_base.h"
#include "../lib/obd_query.h"
#include "../lib/display_manager.h"
#if ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif
#if ENABLE_ELM327
#include "../outputs/output_elm327.h"
#endif
#if ENABLE_TEST_MODE
#include "../test/test_mode.h"
#endif
#if ENABLE_CAN
#include "sensors/can/can_scan.h"
#include "../lib/can_sensor_library/standard_pids.h"
#endif
#include <string.h>
#include <ctype.h>

// BUS I2C subcommand handler
static int bus_i2c(int argc, const char* const* argv, int /*tokenIndex*/) {
    // BUS I2C (no arguments) - display I2C status
    if (argc == 2) {
        displayI2CStatus();
        return 0;
    }

    // BUS I2C CLOCK <kHz>
    if (streq(argv[2], "CLOCK")) {
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
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // BUS I2C NONE - Disable I2C bus
    if (streq(argv[2], "NONE")) {
        systemConfig.buses.active_i2c = 0xFF;
        msg.control.println(F("I2C bus set to NONE"));
        msg.control.println(F("I2C pins will be free on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
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

    systemConfig.buses.active_i2c = bus_id;
    msg.control.print(F("I2C bus set to "));
    msg.control.print(getI2CBusName(bus_id));
    msg.control.print(F(" (SDA="));
    msg.control.print(getDefaultI2CSDA(bus_id));
    msg.control.print(F(", SCL="));
    msg.control.print(getDefaultI2CSCL(bus_id));
    msg.control.println(F(")"));
    msg.control.println(F("Note: Takes effect on next reboot"));
    msg.control.println(F("Use SAVE to persist"));
    return 0;
}

// BUS SPI subcommand handler
static int bus_spi(int argc, const char* const* argv, int /*tokenIndex*/) {
    // BUS SPI (no arguments) - display SPI status
    if (argc == 2) {
        displaySPIStatus();
        return 0;
    }

    // BUS SPI CLOCK <Hz>
    if (streq(argv[2], "CLOCK")) {
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
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // BUS SPI NONE - Disable SPI bus
    if (streq(argv[2], "NONE")) {
        systemConfig.buses.active_spi = 0xFF;
        msg.control.println(F("SPI bus set to NONE"));
        msg.control.println(F("SPI pins will be free on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
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

    systemConfig.buses.active_spi = bus_id;
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
    msg.control.println(F("Use SAVE to persist"));
    return 0;
}

// BUS CAN subcommand handler
static int bus_can(int argc, const char* const* argv, int /*tokenIndex*/) {
#if NUM_CAN_BUSES == 0
    msg.control.println(F("ERROR: No CAN buses available on this platform"));
    return 1;
#else
    // BUS CAN (no arguments) - display CAN status
    if (argc == 2) {
        displayCANStatus();
        return 0;
    }

    // BUS CAN BAUDRATE <bps> - Set both input and output (backward compatibility)
    if (streq(argv[2], "BAUDRATE")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: BAUDRATE requires a speed in bps"));
            msg.control.println(F("  Usage: BUS CAN BAUDRATE <125000|250000|500000|1000000>"));
            return 1;
        }

        uint32_t baudrate = atol(argv[3]);
        if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
            msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
            return 1;
        }

        systemConfig.buses.can_input_baudrate = baudrate;
        systemConfig.buses.can_output_baudrate = baudrate;
        msg.control.print(F("CAN baudrate set to "));
        msg.control.print(baudrate / 1000);
        msg.control.println(F("kbps (both input and output)"));
        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // BUS CAN INPUT BAUDRATE <bps> or BUS CAN INPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [bps]
    if (streq(argv[2], "INPUT")) {
        // BUS CAN INPUT BAUDRATE <bps>
        if (argc >= 4 && streq(argv[3], "BAUDRATE")) {
            if (argc < 5) {
                msg.control.println(F("ERROR: BAUDRATE requires a speed in bps"));
                msg.control.println(F("  Usage: BUS CAN INPUT BAUDRATE <125000|250000|500000|1000000>"));
                return 1;
            }

            uint32_t baudrate = atol(argv[4]);
            if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
                msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
                return 1;
            }

            systemConfig.buses.can_input_baudrate = baudrate;

            // Shared-bus validation
            if (systemConfig.buses.input_can_bus == systemConfig.buses.output_can_bus &&
                systemConfig.buses.input_can_bus != 0xFF) {
                systemConfig.buses.can_output_baudrate = baudrate;
                msg.control.println(F("WARNING: Input and output share same bus - output baudrate also set to match"));
            }

            msg.control.print(F("CAN input baudrate set to "));
            msg.control.print(baudrate / 1000);
            msg.control.println(F("kbps"));
            msg.control.println(F("Note: Takes effect on next reboot"));
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS CAN INPUT <CAN1|CAN2|CAN3> <ENABLE|LISTEN|DISABLE> [baudrate]
        if (argc < 5) {
            msg.control.println(F("ERROR: Usage: BUS CAN INPUT <CAN1|CAN2|CAN3> <ENABLE|LISTEN|DISABLE> [baudrate]"));
            return 1;
        }

        // Parse bus number (CAN1=0, CAN2=1, CAN3=2)
        uint8_t bus_id = 0xFF;
        if (streq(argv[3], "CAN1")) bus_id = 0;
        else if (streq(argv[3], "CAN2")) bus_id = 1;
        else if (streq(argv[3], "CAN3")) bus_id = 2;
        else if (streq(argv[3], "NONE") || streq(argv[3], "DISABLE")) bus_id = 0xFF;
        else {
            msg.control.println(F("ERROR: Bus must be CAN1, CAN2, CAN3, or NONE"));
            return 1;
        }

        // Check bus availability
        if (bus_id != 0xFF && bus_id >= NUM_CAN_BUSES) {
            msg.control.print(F("ERROR: "));
            msg.control.print(argv[3]);
            msg.control.println(F(" not available on this platform"));
            return 1;
        }

        // Parse mode: ENABLE (normal with ACK), LISTEN (listen-only), DISABLE
        uint8_t mode = CAN_INPUT_OFF;
        if (streq(argv[4], "ENABLE") || streq(argv[4], "NORMAL")) {
            mode = CAN_INPUT_NORMAL;
        } else if (streq(argv[4], "LISTEN")) {
            mode = CAN_INPUT_LISTEN;
        } else if (streq(argv[4], "DISABLE")) {
            mode = CAN_INPUT_OFF;
        } else {
            msg.control.println(F("ERROR: Must be ENABLE/NORMAL, LISTEN, or DISABLE"));
            msg.control.println(F("  ENABLE/NORMAL - Active input with ACK (for CAN sensor devices)"));
            msg.control.println(F("  LISTEN        - Listen-only, no ACK/TX (for sniffing ECU bus)"));
            msg.control.println(F("  DISABLE       - Turn off CAN input"));
            return 1;
        }

        // Validate baudrate BEFORE applying any configuration
        uint32_t baudrate = systemConfig.buses.can_input_baudrate; // default
        if (argc >= 6) {
            baudrate = atol(argv[5]);
            if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
                msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
                return 1;
            }
        }

        // CRITICAL: Validate listen-only mode compatibility with shared bus
        if (mode == CAN_INPUT_LISTEN) {
            // Check if input bus will be shared with output
            if (bus_id != 0xFF &&
                bus_id == systemConfig.buses.output_can_bus &&
                systemConfig.buses.can_output_enabled) {
                msg.control.println(F("ERROR: LISTEN mode incompatible with shared output bus"));
                msg.control.println(F("  Listen-only disables ALL TX including output"));
                msg.control.println(F("  Options:"));
                msg.control.println(F("    1. Use separate buses (e.g., input=CAN2, output=CAN1)"));
                msg.control.println(F("    2. Disable CAN output first (BUS CAN OUTPUT CAN1 DISABLE)"));
                msg.control.println(F("    3. Use ENABLE/NORMAL mode instead of LISTEN"));
                return 1;
            }
        }

        // Apply configuration (all validation passed)
        if (mode != CAN_INPUT_OFF) {
            systemConfig.buses.input_can_bus = bus_id;
            systemConfig.buses.can_input_mode = mode;
            systemConfig.buses.can_input_baudrate = baudrate;

            msg.control.print(F("CAN input "));
            msg.control.print(mode == CAN_INPUT_LISTEN ? F("listen-only") : F("normal"));
            msg.control.print(F(" on "));
            msg.control.println(argv[3]);
            if (mode == CAN_INPUT_LISTEN) {
                msg.control.println(F("  No ACK/TX - safe for passive bus monitoring"));
            }

            // Shared-bus baudrate synchronization
            if (systemConfig.buses.input_can_bus == systemConfig.buses.output_can_bus &&
                systemConfig.buses.input_can_bus != 0xFF) {
                systemConfig.buses.can_output_baudrate = baudrate;
                msg.control.println(F("WARNING: Input and output share same bus - output baudrate also set to match"));
            }

            msg.control.print(F("CAN input baudrate set to "));
            msg.control.print(baudrate / 1000);
            msg.control.println(F("kbps"));
        } else {
            systemConfig.buses.can_input_mode = CAN_INPUT_OFF;
            msg.control.println(F("CAN input disabled"));
        }

        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // BUS CAN OUTPUT BAUDRATE <bps> or BUS CAN OUTPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [bps]
    if (streq(argv[2], "OUTPUT")) {
        // BUS CAN OUTPUT BAUDRATE <bps>
        if (argc >= 4 && streq(argv[3], "BAUDRATE")) {
            if (argc < 5) {
                msg.control.println(F("ERROR: BAUDRATE requires a speed in bps"));
                msg.control.println(F("  Usage: BUS CAN OUTPUT BAUDRATE <125000|250000|500000|1000000>"));
                return 1;
            }

            uint32_t baudrate = atol(argv[4]);
            if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
                msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
                return 1;
            }

            systemConfig.buses.can_output_baudrate = baudrate;

            // Shared-bus validation
            if (systemConfig.buses.input_can_bus == systemConfig.buses.output_can_bus &&
                systemConfig.buses.output_can_bus != 0xFF) {
                systemConfig.buses.can_input_baudrate = baudrate;
                msg.control.println(F("WARNING: Input and output share same bus - input baudrate also set to match"));
            }

            msg.control.print(F("CAN output baudrate set to "));
            msg.control.print(baudrate / 1000);
            msg.control.println(F("kbps"));
            msg.control.println(F("Note: Takes effect on next reboot"));
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS CAN OUTPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [baudrate]
        if (argc < 5) {
            msg.control.println(F("ERROR: Usage: BUS CAN OUTPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [baudrate]"));
            return 1;
        }

        // Parse bus number (CAN1=0, CAN2=1, CAN3=2)
        uint8_t bus_id = 0xFF;
        if (streq(argv[3], "CAN1")) bus_id = 0;
        else if (streq(argv[3], "CAN2")) bus_id = 1;
        else if (streq(argv[3], "CAN3")) bus_id = 2;
        else if (streq(argv[3], "NONE") || streq(argv[3], "DISABLE")) bus_id = 0xFF;
        else {
            msg.control.println(F("ERROR: Bus must be CAN1, CAN2, CAN3, or NONE"));
            return 1;
        }

        // Check bus availability
        if (bus_id != 0xFF && bus_id >= NUM_CAN_BUSES) {
            msg.control.print(F("ERROR: "));
            msg.control.print(argv[3]);
            msg.control.println(F(" not available on this platform"));
            return 1;
        }

        // Parse enable/disable
        bool enable = false;
        if (streq(argv[4], "ENABLE")) enable = true;
        else if (streq(argv[4], "DISABLE")) enable = false;
        else {
            msg.control.println(F("ERROR: Must be ENABLE or DISABLE"));
            return 1;
        }

        // Validate baudrate BEFORE applying any configuration
        uint32_t baudrate = systemConfig.buses.can_output_baudrate; // default
        if (argc >= 6) {
            baudrate = atol(argv[5]);
            if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
                msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
                return 1;
            }
        }

        // Apply configuration (all validation passed)
        if (enable) {
            systemConfig.buses.output_can_bus = bus_id;
            systemConfig.buses.can_output_enabled = 1;
            systemConfig.buses.can_output_baudrate = baudrate;

            msg.control.print(F("CAN output enabled on "));
            msg.control.println(argv[3]);

            // Shared-bus baudrate synchronization
            if (systemConfig.buses.input_can_bus == systemConfig.buses.output_can_bus &&
                systemConfig.buses.output_can_bus != 0xFF) {
                systemConfig.buses.can_input_baudrate = baudrate;
                msg.control.println(F("WARNING: Input and output share same bus - input baudrate also set to match"));
            }

            msg.control.print(F("CAN output baudrate set to "));
            msg.control.print(baudrate / 1000);
            msg.control.println(F("kbps"));
        } else {
            systemConfig.buses.can_output_enabled = 0;
            msg.control.println(F("CAN output disabled"));
        }

        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // Unknown CAN subcommand
    msg.control.println(F("ERROR: Unknown CAN subcommand"));
    msg.control.println(F("Valid: BAUDRATE, INPUT, OUTPUT"));
    msg.control.println(F("  BUS CAN BAUDRATE <bps>"));
    msg.control.println(F("  BUS CAN INPUT <CAN1|CAN2|CAN3> <ENABLE|LISTEN|DISABLE> [bps]"));
    msg.control.println(F("  BUS CAN INPUT BAUDRATE <bps>"));
    msg.control.println(F("  BUS CAN OUTPUT <CAN1|CAN2|CAN3> <ENABLE|DISABLE> [bps]"));
    msg.control.println(F("  BUS CAN OUTPUT BAUDRATE <bps>"));
    return 1;
#endif
}

// BUS SERIAL subcommand handler
static int bus_serial(int argc, const char* const* argv, int /*tokenIndex*/) {
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
            return 0;
        }

        // BUS SERIAL <port> ENABLE [baudrate]
        if (streq(argv[3], "ENABLE")) {
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
                msg.control.println(F("Use SAVE to persist"));
            } else {
                msg.control.print(F("ERROR: Failed to enable Serial"));
                msg.control.println(port_id);
            }
            return 0;
        }

        // BUS SERIAL <port> DISABLE
        if (streq(argv[3], "DISABLE")) {
            if (disableSerialPort(port_id)) {
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.println(F(" disabled"));
                msg.control.println(F("Use SAVE to persist"));
            }
            return 0;
        }

        // BUS SERIAL <port> BAUDRATE <rate>
        if (streq(argv[3], "BAUDRATE")) {
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
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS SERIAL <port> ELM327 ENABLE|DISABLE
#if ENABLE_ELM327
        if (streq(argv[3], "ELM327")) {
            if (argc < 5) {
                msg.control.println(F("ERROR: ELM327 requires ENABLE or DISABLE"));
                msg.control.println(F("  Usage: BUS SERIAL <port> ELM327 ENABLE|DISABLE"));
                return 1;
            }

            if (streq(argv[4], "ENABLE")) {
                if (!isSerialPortActive(port_id)) {
                    msg.control.print(F("ERROR: Serial"));
                    msg.control.print(port_id);
                    msg.control.println(F(" is not enabled"));
                    msg.control.print(F("  Run: BUS SERIAL "));
                    msg.control.print(port_id);
                    msg.control.println(F(" ENABLE <baud> first"));
                    return 1;
                }

                // Conflict: refuse if this port is used by the message router
                uint8_t routerTransportId = TRANSPORT_SERIAL1 + (port_id - 1);
                const char* conflictPlane = nullptr;
                if (systemConfig.router.control_primary == routerTransportId ||
                    systemConfig.router.control_secondary == routerTransportId)
                    conflictPlane = "CONTROL";
                else if (systemConfig.router.data_primary == routerTransportId ||
                         systemConfig.router.data_secondary == routerTransportId)
                    conflictPlane = "DATA";
                else if (systemConfig.router.debug_primary == routerTransportId ||
                         systemConfig.router.debug_secondary == routerTransportId)
                    conflictPlane = "DEBUG";

                if (conflictPlane) {
                    msg.control.print(F("ERROR: Serial"));
                    msg.control.print(port_id);
                    msg.control.print(F(" is in use by the router ("));
                    msg.control.print(conflictPlane);
                    msg.control.println(F(" plane) — reassign first"));
                    return 1;
                }

                // Conflict: refuse if another port is already the ELM327 port
                if (systemConfig.buses.elm327_serial_port != 0xFF &&
                    systemConfig.buses.elm327_serial_port != port_id) {
                    msg.control.print(F("ERROR: Serial"));
                    msg.control.print(systemConfig.buses.elm327_serial_port);
                    msg.control.println(F(" is already the ELM327 port"));
                    msg.control.print(F("  Run: BUS SERIAL "));
                    msg.control.print(systemConfig.buses.elm327_serial_port);
                    msg.control.println(F(" ELM327 DISABLE first"));
                    return 1;
                }

                Stream* ser = getSerialPort(port_id);
                elm327Output.setSerial(ser);
                elm327Output.begin();
                systemConfig.buses.elm327_serial_port = port_id;
                systemConfig.outputEnabled[OUTPUT_ELM327] = 1;

                msg.control.print(F("ELM327 emulator assigned to Serial"));
                msg.control.println(port_id);
                msg.control.println(F("Use SAVE to persist"));
                return 0;
            }

            if (streq(argv[4], "DISABLE")) {
                elm327Output.end();
                elm327Output.setSerial(nullptr);
                systemConfig.buses.elm327_serial_port = 0xFF;
                systemConfig.outputEnabled[OUTPUT_ELM327] = 0;

                msg.control.print(F("ELM327 disabled on Serial"));
                msg.control.println(port_id);
                msg.control.println(F("Use SAVE to persist"));
                return 0;
            }

            msg.control.println(F("ERROR: ELM327 requires ENABLE or DISABLE"));
            return 1;
        }
#endif  // ENABLE_ELM327

        // Unknown subcommand for port
        msg.control.print(F("ERROR: Unknown command '"));
        msg.control.print(argv[3]);
        msg.control.println(F("'"));
#if ENABLE_ELM327
        msg.control.println(F("  Valid: ENABLE, DISABLE, BAUDRATE, ELM327"));
#else
        msg.control.println(F("  Valid: ENABLE, DISABLE, BAUDRATE"));
#endif
        return 1;
    }

    // Not a port number - unknown subcommand
    msg.control.print(F("ERROR: Unknown serial command '"));
    msg.control.print(argv[2]);
    msg.control.println(F("'"));
#if ENABLE_ELM327
    msg.control.println(F("  Usage: BUS SERIAL [1-8] [ENABLE|DISABLE|BAUDRATE <rate>|ELM327 <ENABLE|DISABLE>]"));
#else
    msg.control.println(F("  Usage: BUS SERIAL [1-8] [ENABLE|DISABLE|BAUDRATE <rate>]"));
#endif
    return 1;
#endif
}


// BUS subcommand tokens (PROGMEM)
static const char PSTR_BUS_I2C[]    PROGMEM = "I2C";
static const char PSTR_BUS_SPI[]    PROGMEM = "SPI";
static const char PSTR_BUS_CAN[]    PROGMEM = "CAN";
static const char PSTR_BUS_SERIAL[] PROGMEM = "SERIAL";

static const Subcommand BUS_SUBCOMMANDS[] PROGMEM = {
    { PSTR_BUS_I2C,    bus_i2c },
    { PSTR_BUS_SPI,    bus_spi },
    { PSTR_BUS_CAN,    bus_can },
    { PSTR_BUS_SERIAL, bus_serial },
};
static const uint8_t NUM_BUS_SUBCOMMANDS = sizeof(BUS_SUBCOMMANDS) / sizeof(Subcommand);

int cmd_bus(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println();
        msg.control.println(F("Commands:"));
        msg.control.println(F("  BUS I2C [0|1|2|NONE]      - Show or select I2C bus (NONE frees pins)"));
        msg.control.println(F("  BUS I2C CLOCK <kHz>       - Set I2C clock (100/400/1000)"));
        msg.control.println(F("  BUS SPI [0|1|2|NONE]      - Show or select SPI bus (NONE frees pins)"));
        msg.control.println(F("  BUS SPI CLOCK <Hz>        - Set SPI clock"));
        msg.control.println(F("  BUS CAN                   - Show CAN status"));
        msg.control.println(F("  BUS CAN BAUDRATE <bps>    - Set CAN baudrate (both buses)"));
        msg.control.println(F("  BUS CAN INPUT <bus> <ENABLE|LISTEN|DISABLE> [bps]"));
        msg.control.println(F("  BUS CAN INPUT BAUDRATE <bps> - Set CAN input baudrate"));
        msg.control.println(F("  BUS CAN OUTPUT <bus> <ENABLE|DISABLE> [bps]"));
        msg.control.println(F("  BUS CAN OUTPUT BAUDRATE <bps> - Set CAN output baudrate"));
        msg.control.println(F("  BUS SERIAL                - Show all serial ports"));
        msg.control.println(F("  BUS SERIAL <1-8> ENABLE [baud] - Enable serial port"));
        msg.control.println(F("  BUS SERIAL <1-8> DISABLE  - Disable serial port"));
        msg.control.println(F("  BUS SERIAL <1-8> BAUDRATE <rate> - Set baud rate"));
        return 0;
    }

    const char* busType = argv[1];

    // Dispatch on bus type. BUS_SUBCOMMANDS lives in PROGMEM on AVR.
    return dispatchSubcommand(BUS_SUBCOMMANDS, NUM_BUS_SUBCOMMANDS,
                              busType, "BUS", argc, argv, 1);
}

// ===== LOG COMMAND =====

#if ENABLE_SELFTEST
int selftestBusTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_BUS_SUBCOMMANDS; i++) {
        const char*        tok = (const char*)pgm_read_ptr(&BUS_SUBCOMMANDS[i].token);
        SubcommandHandler  h   = (SubcommandHandler)pgm_read_ptr(&BUS_SUBCOMMANDS[i].handler);
        failures += validateDispatchEntry(F("BUS"), i, tok, (const void*)h, true);
    }
    return failures;
}
#endif
