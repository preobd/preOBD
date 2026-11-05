#!/usr/bin/env python3
"""
Watchdog bench checks for Teensy 4.x (#543).

Two jobs, both over USB serial:

  stall   Proves the watchdog still protects the board. Needs a build with
          INCLUDE_WATCHDOG_STALL, which release images never have:
            PLATFORMIO_BUILD_FLAGS="-D INCLUDE_WATCHDOG_STALL=1" pio run -e teensy41
          then flash it. Runs SYSTEM DIAG WATCHDOG STALL in RUN mode and checks:
            STALL 1500          survives        (inside the 2 s RUN timeout)
            STALL 3000          resets, TOUT    (WRSR 0x2, a real timeout)
            STALL 15000 SD      survives        (the SD window widens to 20 s)
            STALL 3000          resets, TOUT    (the window narrowed back)
          About a minute. Exit status 0 only if all four pass.

  soak    Counts unexpected resets under load: polls a command once a second
          for N minutes, reconnects after every drop, and prints each reset's
          VERSION "Last reset:" line. Any build. The #537 regression check is
          1 ms SD rows (OUTPUT SD_Log INTERVAL 1) plus the default poll for
          10 minutes: expect 0 resets.

Usage:
    python3 tools/watchdog_bench.py stall [PORT]
    python3 tools/watchdog_bench.py soak  [PORT] [--minutes N] [--poll "CMD"]

The board must be in RUN mode with a saved config (it reboots into RUN after
each reset). Opening a Teensy's USB port doesn't reset it. With two Teensys
plugged in, pass the port explicitly.

Prerequisites: pip install pyserial
"""

import argparse
import re
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
        except (serial.SerialException, OSError):
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
        except (serial.SerialException, OSError):
            pass
        time.sleep(0.5)
    return "?"


def stall(port, ms, sd):
    """Run one stall. Returns ("survived" | "reset" | "error", detail)."""
    s = open_port(port)
    cmd = f"SYSTEM DIAG WATCHDOG STALL {ms}{' SD' if sd else ''}\r\n"
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
    except (serial.SerialException, OSError):
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
        (1500, False, "survived", None),
        (3000, False, "reset", "WRSR 0x2"),
        (15000, True, "survived", None),
        (3000, False, "reset", "WRSR 0x2"),
    ]
    failures = 0
    for ms, sd, want, detail_has in steps:
        got, detail = stall(port, ms, sd)
        ok = got == want and (detail_has is None or detail_has in detail)
        failures += not ok
        label = f"STALL {ms}{' SD' if sd else ''}"
        print(f"{'PASS' if ok else 'FAIL'}  {label:16} want {want:8} got {got}: {detail}", flush=True)
        if got == "error":
            print("      (is this a build with INCLUDE_WATCHDOG_STALL, in RUN mode?)")
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["stall", "soak"])
    ap.add_argument("port", nargs="?", default=DEFAULT_PORT)
    ap.add_argument("--minutes", type=float, default=10.0, help="soak length (default 10)")
    ap.add_argument("--poll", default="SYSTEM SD JSON", help="soak poll command (default: SYSTEM SD JSON)")
    a = ap.parse_args()
    if a.mode == "stall":
        return cmd_stall(a.port)
    return cmd_soak(a.port, a.minutes, a.poll)


if __name__ == "__main__":
    sys.exit(main())
