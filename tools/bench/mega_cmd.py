#!/usr/bin/env python3
"""Run preOBD console commands on the Mega over USB, waiting out its reset.

    python3 tools/bench/mega_cmd.py /dev/cu.usbmodemXXXX 2 VERSION "BUS CAN"

Opening the Mega's port resets it (DTR), so this waits for boot to finish
before the first command. Otherwise as serial_cmd.py; never sends SAVE for you.
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
t, boot = time.time(), b""
while time.time() - t < 6:          # reset + boot
    boot += s.read(8192)
print(">>> (boot)\n" + "\n".join(l for l in boot.decode(errors="replace").splitlines()
                                 if keep_csv or not csv.match(l.strip())))
for c in cmds:
    s.write((c + "\r\n").encode())
    t, out = time.time(), b""
    while time.time() - t < wait:
        out += s.read(8192)
    lines = out.decode(errors="replace").splitlines()
    if not keep_csv:
        lines = [l for l in lines if not csv.match(l.strip())]
    print(f">>> {c}\n" + "\n".join(lines))
