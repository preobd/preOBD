/*
 * realdash_framed.h - RealDash over the nRF's BLE data channel (#612 phase 2c)
 *
 * A REALDASH output on transport BLE sends its 16-byte RealDash frames as
 * framed type 0x31 (FRAME_TYPE_HM10_STREAM): the nRF52840 notifies them on
 * the phone's HM-10 FFE1 characteristic, where RealDash (connection type
 * Bluetooth LE) reads them. The channel carries one data output, so an
 * enabled ELM327-on-BLE output and this one exclude each other (admission).
 *
 * Why it is shaped like this:
 *   - Teensy 4's Serial1 TX ring is 64 bytes and sendFrame() refuses a frame
 *     that doesn't fit its free space (#528), so a pass goes out as frames of
 *     at most 3 whole 16-byte RealDash frames, drained across loop ticks.
 *     A RealDash frame is never split: RealDash only resyncs on 44 33 22 11.
 *   - A pass is packed only once the previous one has fully left; otherwise
 *     it is skipped and counted. Nothing stale queues up behind a slow phone,
 *     and nothing bursts out when it recovers.
 *   - Type 0x31, not 0x30: the nRF drops a 0x31 when the FFE1 notify stash is
 *     busy instead of parking it and stopping every Serial1 read, so a slow
 *     phone can't stall the app's status, live values and alarms.
 *   - Frames the phone writes to FFE1 meanwhile are dropped by
 *     Elm327Framed::handleInbound (not running), never parsed as ELM327.
 */
#ifndef REALDASH_FRAMED_H
#define REALDASH_FRAMED_H

#include <Arduino.h>

namespace RealdashFramed {

// The output starts and stops (output_manager.cpp)
void start();
void stop();

// Start a pass: the Print that collects this pass's RealDash frames, or
// nullptr when the previous pass hasn't fully left (the pass is skipped)
Print* beginPass();

// Send what the transport accepts (each loop, and right after a pass)
void tick();

// Passes skipped because the previous one was still leaving (bench)
uint32_t skippedPasses();

}  // namespace RealdashFramed

#endif // REALDASH_FRAMED_H
