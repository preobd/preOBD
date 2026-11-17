#!/usr/bin/env python3
"""Run preOBD console commands over USB serial, each with a fixed time budget.

    python3 tools/bench/serial_cmd.py /dev/cu.usbmodemXXXX 1.5 VERSION "BUS CAN"

A fixed deadline, not "until idle": in RUN mode the CSV stream never goes quiet.
CSV lines (NAME,value,unit) are dropped from the output unless --csv is given.
Never sends SAVE for you: snapshot the config (SYSTEM DUMP JSON) before a test
that changes it, and restore it afterwards.
"""
import re, sys, time
import serial

args = sys.argv[1:]
keep_csv = "--csv" in args
args = [a for a in args if a != "--csv"]
if len(args) < 3:
    sys.exit(__doc__)
port, wait, cmds = args[0], float(args[1]), args[2:]
csv = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()
for c in cmds:
    s.write((c + "\r\n").encode())
    t, out = time.time(), b""
    while time.time() - t < wait:
        out += s.read(8192)
    lines = out.decode(errors="replace").splitlines()
    if not keep_csv:
        lines = [l for l in lines if not csv.match(l.strip())]
    print(f">>> {c}\n" + "\n".join(lines))
