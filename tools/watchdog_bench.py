#!/usr/bin/env python3
"""
Watchdog bench checks for Teensy 4.x (#543).

Two jobs, both over USB serial:

  stall   Proves the watchdog still protects the board. Needs a build with
          INCLUDE_WATCHDOG_STALL, which release images never have: the debug
          env (`pio run -e debug`, Teensy 4.1) has it, or add it to any env with
            PLATFORMIO_BUILD_FLAGS="-D INCLUDE_WATCHDOG_STALL=1" pio run -e <env>
          then flash it. Runs SYSTEM DIAG WATCHDOG STALL and checks, in RUN (2 s):
            STALL 1500          survives        (inside the 2 s RUN timeout)
            STALL 3000          resets, TOUT    (WRSR 0x2, a real timeout)
            STALL 15000 SCOPE 20000  survives   (a WatchdogScope widens it)
            STALL 3000          resets, TOUT    (the scope narrowed it back)
          then in CONFIG (8 s, #541):
            STALL 6000          survives
            STALL 10000         resets, TOUT
          About a minute and a half. Exit status 0 only if all pass.

  soak    Counts unexpected resets under load: polls a command once a second
          for N minutes, reconnects after every drop, and prints each reset's
          VERSION "Last reset:" line. Any build. The #537 regression check is
          1 ms SD rows (OUTPUT SD_Log INTERVAL 1) plus the default poll for
          10 minutes: expect 0 resets.

  where   Maps a PC from VERSION's "hung at PC 0x..." (#542) to a function,
          using the firmware.elf of the build that was running.

Usage:
    python3 tools/watchdog_bench.py where 0x6000F69A [--env teensy41]
    python3 tools/watchdog_bench.py stall [PORT]
    python3 tools/watchdog_bench.py soak  [PORT] [--minutes N] [--poll "CMD"]

Each step sets the mode itself (RUN or CONFIG) first, so the board needn't
boot into RUN. Opening a Teensy's USB port doesn't reset it. With two Teensys
plugged in, pass the port explicitly.

Prerequisites: pip install pyserial
"""

import argparse
import os
import re
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    print("error: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(2)

DEFAULT_PORT = "/dev/cu.usbmodem190859601"
BAUD = 115200
RESET_RE = re.compile(rb"Last reset: [^\r\n]*")


def open_port(port, wait_s=30.0):
    """Open the port, retrying while the board reboots and re-enumerates."""
    deadline = time.time() + wait_s
    while True:
        try:
            s = serial.Serial(port, BAUD, timeout=0.05)
            time.sleep(0.5)   # let a just-booted board finish its banner
            s.reset_input_buffer()
            return s
        except Exception:   # SerialException, OSError, termios.error: port mid-(re)enumeration
            if time.time() > deadline:
                raise
            time.sleep(0.2)


def read_for(s, seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        buf += s.read(4096)
    return buf


def last_reset(port, wait_s=15.0):
    """VERSION's "Last reset:" line. Retries: a board that just reset is still
    booting (SD mount, banner) when its USB port reappears."""
    deadline = time.time() + wait_s
    while time.time() < deadline:
        try:
            s = open_port(port)
            try:
                s.write(b"VERSION\r\n")
                m = RESET_RE.search(read_for(s, 1.0))
            finally:
                s.close()
            if m:
                return m.group(0).decode()
        except Exception:
            pass
        time.sleep(0.5)
    return "?"


def stall(port, ms, scope_ms, config=False):
    """Run one stall. Returns ("survived" | "reset" | "error", detail)."""
    s = open_port(port)
    # Set the mode every time: a board with no saved config boots into CONFIG,
    # and after a reset the board is back in its boot mode.
    s.write(b"CONFIG\r\n" if config else b"RUN\r\n")
    read_for(s, 0.5)
    cmd = f"SYSTEM DIAG WATCHDOG STALL {ms}{f' SCOPE {scope_ms}' if scope_ms else ''}\r\n"
    s.write(cmd.encode())
    buf = b""
    start = time.time()
    try:
        while time.time() - start < ms / 1000.0 + 5:
            buf += s.read(4096)
            if b"Stall survived" in buf:
                return "survived", f"{time.time() - start:.1f} s"
            # Line-anchored: in RUN, SD rows and debug text can contain "ERROR".
            m = re.search(rb"(?m)^ERROR:[^\r\n]*", buf)
            if m:
                return "error", m.group(0).decode(errors="replace").strip()
    except Exception:
        elapsed = time.time() - start
        time.sleep(3)   # reboot + USB re-enumeration
        return "reset", f"after {elapsed:.1f} s, {last_reset(port)}"
    finally:
        try:
            s.close()
        except Exception:
            pass
    return "error", "no reply and no reset"


def cmd_stall(port):
    steps = [
        (1500, 0, False, "survived", None),
        (3000, 0, False, "reset", "WRSR 0x2"),
        (15000, 20000, False, "survived", None),
        (3000, 0, False, "reset", "WRSR 0x2"),
        (6000, 0, True, "survived", None),
        (10000, 0, True, "reset", "WRSR 0x2"),
    ]
    failures = 0
    for ms, scope_ms, config, want, detail_has in steps:
        got, detail = stall(port, ms, scope_ms, config)
        ok = got == want and (detail_has is None or detail_has in detail)
        failures += not ok
        label = f"{'CONFIG ' if config else ''}STALL {ms}{f' SCOPE {scope_ms}' if scope_ms else ''}"
        print(f"{'PASS' if ok else 'FAIL'}  {label:24} want {want:8} got {got}: {detail}", flush=True)
        if got == "error":
            print("      (is this a build with INCLUDE_WATCHDOG_STALL?)")
            break
    print("all passed" if failures == 0 else f"{failures} failed")
    return 0 if failures == 0 else 1


def cmd_soak(port, minutes, poll):
    s = open_port(port)
    start = time.time()
    end = start + minutes * 60
    resets = polls = 0
    print(f"soak {minutes:g} min on {port}, polling {poll!r} at 1 Hz", flush=True)
    while time.time() < end:
        try:
            s.reset_input_buffer()
            s.write((poll + "\r\n").encode())
            read_for(s, 0.6)
            polls += 1
            if polls % 60 == 0:
                print(f"  {time.strftime('%H:%M:%S')} {polls} polls, {resets} resets", flush=True)
            time.sleep(0.4)
        except (serial.SerialException, OSError):
            resets += 1
            when = time.time() - start
            try:
                s.close()
            except Exception:
                pass
            time.sleep(3)
            print(f"  RESET #{resets} at {when:.0f} s: {last_reset(port)}", flush=True)
            while True:   # a slow reboot must not end the soak
                try:
                    s = open_port(port)
                    break
                except (serial.SerialException, OSError):
                    print("  (board not back yet, waiting)", flush=True)
    s.close()
    print(f"done: {resets} resets in {polls} polls; final {last_reset(port)}")
    return 0 if resets == 0 else 1


def cmd_where(pc, env):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    elf = os.path.join(root, ".pio", "build", env, "firmware.elf")
    a2l = os.path.expanduser(
        "~/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin/arm-none-eabi-addr2line")
    if not os.path.exists(elf):
        print(f"no {elf}: build the env that was running first (pio run -e {env})")
        return 2
    out = subprocess.run([a2l, "-f", "-C", "-i", "-e", elf, pc], capture_output=True, text=True)
    print(out.stdout.strip() or out.stderr.strip())
    print("(file:line needs the debug env's symbols; a release build gives the\n"
          " function only. The build must be the exact one that produced the PC.)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["stall", "soak", "where"])
    ap.add_argument("port", nargs="?", default=DEFAULT_PORT, help="serial port, or the PC for `where`")
    ap.add_argument("--env", default="teensy41", help="where: build env whose firmware.elf to use")
    ap.add_argument("--minutes", type=float, default=10.0, help="soak length (default 10)")
    ap.add_argument("--poll", default="SYSTEM SD JSON", help="soak poll command (default: SYSTEM SD JSON)")
    a = ap.parse_args()
    if a.mode == "where":
        return cmd_where(a.port, a.env)
    if a.mode == "stall":
        return cmd_stall(a.port)
    return cmd_soak(a.port, a.minutes, a.poll)


if __name__ == "__main__":
    sys.exit(main())
