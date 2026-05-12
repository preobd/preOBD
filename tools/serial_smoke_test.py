#!/usr/bin/env python3
"""
Serial smoke test harness for preOBD command dispatch.

Sends a fixed corpus of commands over USB serial, asserts on output snippets,
and reports PASS/FAIL per test. Intended for hardware-in-the-loop validation
of PRs that touch command parsing, dispatch, or any user-visible serial behavior.

Usage:
    python3 tools/serial_smoke_test.py [PORT]

Default port: /dev/cu.usbmodem190859601 (override on the command line).

Prerequisites:
    pip install pyserial

The device must be flashed with current firmware before running. This script
does not flash — use `pio run -e <env> -t upload` separately.

Adding tests:
    Each test is a function that takes a `Session` and returns a list of
    assertion results. Append to TESTS at the bottom. See the existing tests
    for the pattern.
"""

import sys
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

try:
    import serial
except ImportError:
    print("error: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(2)


DEFAULT_PORT = "/dev/cu.usbmodem190859601"
BAUD = 115200


@dataclass
class Result:
    name: str
    ok: bool
    detail: List[str] = field(default_factory=list)
    output: str = ""


class Session:
    """Thin wrapper over pyserial that handles the preOBD prompt protocol."""

    def __init__(self, port: str):
        self.ser = serial.Serial(port, BAUD, timeout=2.0)
        time.sleep(2.0)  # let device finish boot / USB enumeration
        self._drain(1.0)  # discard banner

    def close(self):
        self.ser.close()

    def _drain(self, t: float = 0.3) -> str:
        """Read whatever's pending in `t` seconds, return as text."""
        time.sleep(t)
        buf = b""
        while self.ser.in_waiting:
            buf += self.ser.read(self.ser.in_waiting)
            time.sleep(0.05)
        return buf.decode("utf-8", errors="replace")

    def send(self, cmd: str, wait: float = 0.6) -> str:
        """Send `cmd\\n` and return everything received within `wait` seconds."""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        return self._drain(wait)


def check(name: str, output: str,
          must_contain: Optional[List[str]] = None,
          must_not_contain: Optional[List[str]] = None) -> Result:
    detail = []
    ok = True
    for s in (must_contain or []):
        if s not in output:
            ok = False
            detail.append(f"missing {s!r}")
    for s in (must_not_contain or []):
        if s in output:
            ok = False
            detail.append(f"unexpected {s!r}")
    return Result(name, ok, detail, output.strip())


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_selftest_baseline(s: Session) -> Result:
    out = s.send("SELFTEST", wait=1.5)
    return check("SELFTEST baseline", out, must_contain=["SELFTEST: PASS"])


def test_rpm_arity_rejects_excess_args(s: Session) -> Result:
    """#187: argc != 8 && argc != 9 — argc=11 must fail before arg parsing."""
    out = s.send("SET 5 RPM 12 3.0 2000 100 8000 EXTRA EXTRA EXTRA")
    return check("RPM arity rejects argc=11", out,
                 must_contain=["RPM requires 5 or 6 parameters"],
                 must_not_contain=["Input not configured"])


def test_speed_arity_rejects_excess_args(s: Session) -> Result:
    """#187: same fix in set_speed."""
    out = s.send("SET 2 SPEED 100 2008 3.73 2000 300 EXTRA EXTRA EXTRA")
    return check("SPEED arity rejects argc=11", out,
                 must_contain=["SPEED requires 5 or 6 parameters"])


def test_can_typo_errors_out(s: Session) -> Result:
    """#188: bare 'SET CAN <non-pid>' errors out instead of falling through."""
    out = s.send("SET CAN APPLICATION CHT")
    return check("CAN typo errors cleanly", out,
                 must_contain=["requires a numeric or 0xNN hex PID"],
                 must_not_contain=["Imported CAN sensor",
                                   "configured as CHT",
                                   "Thermocouple CS"])


def test_can_typo_creates_no_slot(s: Session) -> Result:
    """#188 follow-up: confirm LIST shows no phantom thermocouple-on-pin-192."""
    out = s.send("LIST INPUTS", wait=1.5)
    return check("No phantom CHT slot in LIST", out,
                 must_not_contain=["pin 192", "Thermocouple"])


def test_legit_can_import(s: Session) -> Result:
    """Negative control: legitimate import path still works."""
    out = s.send("SET CAN 0x0C", wait=1.0)
    return check("Legitimate CAN import (PID 0x0C)", out,
                 must_contain=["Imported CAN sensor"],
                 must_not_contain=["ERROR", "requires a numeric"])


def test_selftest_after(s: Session) -> Result:
    out = s.send("SELFTEST", wait=1.5)
    return check("SELFTEST after tests", out, must_contain=["SELFTEST: PASS"])


# --- RUN-mode gating tests (#194) ---
#
# These first switch the device into RUN mode, run their assertion, and the
# group ends with a CONFIG flip back so subsequent tests are unaffected. We
# deliberately DO NOT test the destructive-in-CONFIG path (SYSTEM RESET CONFIRM
# in CONFIG mode would wipe state) — only the *rejection* path in RUN mode.

def test_run_mode_allows_system_status(s: Session) -> Result:
    """SYSTEM STATUS is runModeAllowed=true; should still work in RUN mode."""
    s.send("RUN", wait=0.4)
    out = s.send("SYSTEM STATUS", wait=1.5)
    return check("RUN: SYSTEM STATUS still works", out,
                 must_not_contain=["requires CONFIG mode"])


def test_run_mode_blocks_system_reset(s: Session) -> Result:
    """SYSTEM RESET in RUN mode is the headline safety bug — must be blocked."""
    s.send("RUN", wait=0.4)
    out = s.send("SYSTEM RESET CONFIRM", wait=1.0)
    return check("RUN: SYSTEM RESET CONFIRM blocked", out,
                 must_contain=["SYSTEM RESET requires CONFIG mode"],
                 must_not_contain=["Erasing all configuration",
                                   "Configuration reset complete",
                                   "Rebooting"])


def test_run_mode_blocks_system_reboot(s: Session) -> Result:
    """SYSTEM REBOOT in RUN mode would silently reboot the device."""
    s.send("RUN", wait=0.4)
    out = s.send("SYSTEM REBOOT", wait=1.0)
    return check("RUN: SYSTEM REBOOT blocked", out,
                 must_contain=["SYSTEM REBOOT requires CONFIG mode"],
                 must_not_contain=["Rebooting system"])


def test_run_mode_blocks_system_sea_level(s: Session) -> Result:
    s.send("RUN", wait=0.4)
    out = s.send("SYSTEM SEA_LEVEL 1013", wait=0.6)
    return check("RUN: SYSTEM SEA_LEVEL blocked", out,
                 must_contain=["SYSTEM SEA_LEVEL requires CONFIG mode"])


def test_run_mode_allows_log_status(s: Session) -> Result:
    s.send("RUN", wait=0.4)
    out = s.send("LOG STATUS", wait=1.0)
    return check("RUN: LOG STATUS still works", out,
                 must_not_contain=["requires CONFIG mode"])


def test_run_mode_blocks_log_level(s: Session) -> Result:
    s.send("RUN", wait=0.4)
    out = s.send("LOG LEVEL DEBUG INFO", wait=0.6)
    return check("RUN: LOG LEVEL blocked", out,
                 must_contain=["LOG LEVEL requires CONFIG mode"])


def test_run_mode_blocks_bus_i2c(s: Session) -> Result:
    """BUS is configModeOnly=true at the parent level — gate fires in dispatchCommand."""
    s.send("RUN", wait=0.4)
    out = s.send("BUS I2C", wait=0.6)
    return check("RUN: BUS I2C blocked", out,
                 must_contain=["BUS requires CONFIG mode"])


def test_run_mode_blocks_set(s: Session) -> Result:
    """SET is configModeOnly=true; gate fires before reaching SET dispatch."""
    s.send("RUN", wait=0.4)
    out = s.send("SET A0 APPLICATION CHT", wait=0.6)
    return check("RUN: SET blocked", out,
                 must_contain=["SET requires CONFIG mode"])


def test_run_mode_unknown_command(s: Session) -> Result:
    """Sanity: unknown commands in RUN mode now get 'Unknown command' (the
    pre-fix code produced 'Configuration locked in RUN mode' for any unknown
    input, which was misleading)."""
    s.send("RUN", wait=0.4)
    out = s.send("DEFINITELYNOTACOMMAND", wait=0.6)
    return check("RUN: unknown command says 'Unknown'", out,
                 must_contain=["Unknown command"],
                 must_not_contain=["requires CONFIG mode"])


def test_back_to_config(s: Session) -> Result:
    """Cleanup: leave the device in CONFIG mode for any further work."""
    out = s.send("CONFIG", wait=0.4)
    # No specific assertion — just want the side effect.
    return check("Back to CONFIG mode", out, must_not_contain=[])


# --- parsePin counter leak (#196) ---
#
# Before the fix, parsePin("I2C") and parsePin("CAN") advanced internal static
# counters on every parse, regardless of whether the command actually went on
# to allocate a slot. So 14+ failed `SET I2C SENSOR <typo>` invocations would
# falsely exhaust the I2C pin range and start reporting "Too many I2C sensors"
# until reboot.
#
# After the fix, parsePin scans inputs[] for the highest allocated slot and
# returns max+1 — no internal state to leak. We send 16 typos (more than the
# 13-slot range) and assert "Too many" never appears.

def test_i2c_counter_does_not_leak(s: Session) -> Result:
    s.send("CONFIG", wait=0.3)
    seen_too_many = False
    for _ in range(16):
        out = s.send("SET I2C SENSOR DEFINITELY_NOT_A_SENSOR", wait=0.4)
        if "Too many I2C sensors" in out:
            seen_too_many = True
            break
    return check("I2C parse counter does not leak on failed commands",
                 "TOO_MANY_SEEN" if seen_too_many else "ok",
                 must_not_contain=["TOO_MANY_SEEN"])


def test_can_counter_does_not_leak(s: Session) -> Result:
    """Same shape for CAN — 0xC0..0xDF range, 32 slots. Send 35 typos."""
    s.send("CONFIG", wait=0.3)
    seen_too_many = False
    for _ in range(35):
        # Use a numeric-looking PID to enter the CAN-import prelude path
        # specifically (avoids the field-dispatch branch). The PID won't
        # actually allocate because we'll feed a syntactically-valid but
        # semantically-rejected one — actually any PID that triggers
        # parsePin("CAN") and then errors is fine. Use 0xFF (no standard
        # PID info, so it goes the "unknown PID, use defaults" path which
        # *does* allocate). That defeats the test.
        #
        # Instead: trigger parsePin("CAN") via a syntactically-bad-but-
        # parsed-first SET command. `SET CAN APPLICATION X` errors before
        # allocation but after parsePin("CAN") has run.
        out = s.send("SET CAN APPLICATION X", wait=0.3)
        if "Too many CAN sensors" in out:
            seen_too_many = True
            break
    return check("CAN parse counter does not leak on failed commands",
                 "TOO_MANY_SEEN" if seen_too_many else "ok",
                 must_not_contain=["TOO_MANY_SEEN"])


TESTS: List[Callable[[Session], Result]] = [
    # Existing PR #197 (cmd_set bugs) — all in CONFIG mode
    test_selftest_baseline,
    test_rpm_arity_rejects_excess_args,
    test_speed_arity_rejects_excess_args,
    test_can_typo_errors_out,
    test_can_typo_creates_no_slot,
    test_legit_can_import,
    test_selftest_after,
    # PR #198 (RUN-mode gating, #194)
    test_run_mode_allows_system_status,
    test_run_mode_blocks_system_reset,
    test_run_mode_blocks_system_reboot,
    test_run_mode_blocks_system_sea_level,
    test_run_mode_allows_log_status,
    test_run_mode_blocks_log_level,
    test_run_mode_blocks_bus_i2c,
    test_run_mode_blocks_set,
    test_run_mode_unknown_command,
    test_back_to_config,
    # PR #200 (parsePin counter leak, #196)
    test_i2c_counter_does_not_leak,
    test_can_counter_does_not_leak,
]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    print(f"Opening {port} at {BAUD} baud...")
    s = Session(port)

    # Ensure CONFIG mode for SET commands
    s.send("CONFIG", wait=0.4)

    results: List[Result] = []
    for fn in TESTS:
        results.append(fn(s))

    s.close()

    # Report
    print()
    print("=" * 60)
    print("SMOKE TEST RESULTS")
    print("=" * 60)
    all_pass = True
    for r in results:
        mark = "PASS" if r.ok else "FAIL"
        print(f"[{mark}] {r.name}")
        if not r.ok:
            all_pass = False
            for d in r.detail:
                print(f"       - {d}")
            # Show first non-prompt line of output for context
            first = next(
                (line for line in r.output.split("\n")
                 if line.strip() and not line.startswith("preOBD")),
                "",
            )
            if first:
                print(f"       output: {first[:120]}")

    print()
    print("=" * 60)
    print("OVERALL:", "PASS" if all_pass else "FAIL")
    print("=" * 60)
    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
