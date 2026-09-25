/*
 * config_generation.h - Monotonic "configuration changed" counter
 *
 * Bumped whenever a command that is only allowed in CONFIG mode runs (the
 * dispatcher's mode gate is the marker: every config mutation is CONFIG-gated)
 * and when a JSON configuration is applied. Carried in the status-frame
 * extension (ble_gatt_defs.h PreobdStatusExt) so a BLE client learns that
 * another client changed the configuration without polling (#455).
 *
 * RAM only, starts at 0 on boot: a client detects a reboot from uptime going
 * backwards, not from this counter. Wraps; compare for inequality only.
 * Never bump on a read path: every client refreshes on a bump, and a refresh
 * that bumped would ping-pong between clients.
 */

#ifndef CONFIG_GENERATION_H
#define CONFIG_GENERATION_H

#include <stdint.h>

#if INCLUDE_FRAMED_TRANSPORT
void     configGenerationBump();
uint16_t configGeneration();
#else
// Nothing reads it without the framed status frame — cost nothing (mega2560).
inline void     configGenerationBump() {}
inline uint16_t configGeneration() { return 0; }
#endif

#endif // CONFIG_GENERATION_H
