/*
 * command_dispatch.h - Error-output seam for the dispatch routing layer
 *
 * The routing logic (dispatchCommand / dispatchSubcommand, defined in
 * command_dispatch.cpp) is deliberately decoupled from the message subsystem:
 * it calls these four reporter hooks instead of touching `msg` directly. This
 * keeps the routing translation unit free of the heavy message_router /
 * message_api header chain, so the native routing test (test/test_dispatch)
 * can compile command_dispatch.cpp against lightweight stubs and observe which
 * error path fired.
 *
 * Production definitions live in command_table.cpp and format via msg.control;
 * the test supplies capturing stubs.
 */

#ifndef _COMMAND_DISPATCH_H_
#define _COMMAND_DISPATCH_H_

// Top-level command name not found in COMMANDS[]. `name` is argv[0] as typed.
void dispatchReportUnknownCommand(const char* name);

// Top-level command is configModeOnly but the system is in RUN mode.
// `cmdUpper` is the upper-cased command name.
void dispatchReportConfigRequired(const char* cmdUpper);

// Subcommand token not found in the parent's Subcommand table.
void dispatchReportUnknownSubcommand(const char* commandName, const char* token);

// Subcommand matched but carries runModeAllowed=false while in RUN mode.
void dispatchReportSubcmdConfigRequired(const char* commandName, const char* token);

#endif // _COMMAND_DISPATCH_H_
