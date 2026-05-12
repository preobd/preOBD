/*
 * command_handlers.h - Internal prototypes for top-level command handlers.
 *
 * Each cmd_* handler lives in its own translation unit (cmd_set.cpp,
 * cmd_bus.cpp, etc.) but is referenced by COMMANDS[] in command_table.cpp.
 * This header is the single source of truth for those signatures so a drift
 * fails to compile rather than fails to link.
 *
 * Not part of the public API — only files in src/inputs/ should include this.
 */

#ifndef _COMMAND_HANDLERS_H_
#define _COMMAND_HANDLERS_H_

#include "../config.h"

int cmd_help(int argc, const char* const* argv);
int cmd_list(int argc, const char* const* argv);
int cmd_set(int argc, const char* const* argv);
int cmd_enable(int argc, const char* const* argv);
int cmd_disable(int argc, const char* const* argv);
int cmd_clear(int argc, const char* const* argv);
int cmd_info(int argc, const char* const* argv);
int cmd_output(int argc, const char* const* argv);
int cmd_display(int argc, const char* const* argv);
int cmd_transport(int argc, const char* const* argv);
int cmd_system(int argc, const char* const* argv);
int cmd_save(int argc, const char* const* argv);
int cmd_load(int argc, const char* const* argv);
int cmd_config(int argc, const char* const* argv);
int cmd_run(int argc, const char* const* argv);
int cmd_version(int argc, const char* const* argv);
int cmd_reboot(int argc, const char* const* argv);
int cmd_bus(int argc, const char* const* argv);
int cmd_log(int argc, const char* const* argv);
int cmd_at(int argc, const char* const* argv);

#if ENABLE_RELAY_OUTPUT
int cmd_relay(int argc, const char* const* argv);
#endif
#if ENABLE_TEST_MODE
int cmd_test(int argc, const char* const* argv);
#endif
#if ENABLE_CAN
int cmd_scan(int argc, const char* const* argv);
#endif
#if SUPPORTS_JSON_IMPORT_STREAM
int cmd_json(int argc, const char* const* argv);
#endif

// Platform-specific reboot helper (shared by REBOOT and SYSTEM REBOOT/RESET).
void platformReboot();

#if ENABLE_SELFTEST
// SELFTEST command — validates all dispatch tables.
int cmd_selftest(int argc, const char* const* argv);

// Per-TU table validators. Each returns the number of failing entries.
// Implementations live next to the tables so they read via the same
// pgm_read_ptr / streq_P path the dispatcher uses — a regression in that
// access pattern fails selftest immediately.
int selftestSetTable();      // cmd_set.cpp
int selftestBusTable();      // cmd_bus.cpp
int selftestSystemTable();   // cmd_system.cpp
#endif

#endif // _COMMAND_HANDLERS_H_
