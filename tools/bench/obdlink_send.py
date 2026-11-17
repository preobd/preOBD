#!/usr/bin/env python3
"""Send commands to a running obdlink_daemon.py and print its replies.

    python3 tools/bench/obdlink_send.py ATE0 ATH1 ATSP6 0105 010C
    python3 tools/bench/obdlink_send.py --monitor 9 > capture.txt   # ATMA for 9 s

Never opens the OBDLink port itself (that costs a Bluetooth re-pair). Fails at
once when no daemon is reading the FIFO.
"""
import os, sys, time

HOME = os.path.expanduser("~/.preobd")
FIFO, LOG = os.path.join(HOME, "obdlink_fifo"), os.path.join(HOME, "obdlink_log.txt")
args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
monitor = None
if args[0] == "--monitor":
    monitor = float(args[1]) if len(args) > 1 else 5.0
    args = ["ATMA"]
try:
    fd = os.open(FIFO, os.O_WRONLY | os.O_NONBLOCK)   # ENXIO: nobody is reading it
except OSError:
    sys.exit("obdlink_daemon.py isn't running: start it in its own terminal")
start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
if monitor is not None:
    # The daemon monitors for the whole time and stops ATMA itself
    os.write(fd, f"ATMA {monitor}\n".encode())
    time.sleep(monitor + 3)
else:
    os.write(fd, ("\n".join(args) + "\n").encode())
    time.sleep(0.5 + sum(4.5 if a.upper() == "ATZ" else 3.0 for a in args))
os.close(fd)
with open(LOG) as f:
    f.seek(start)
    out = f.read()
if monitor is not None:   # one frame per line, ready for can_decode.py
    out = "\n".join(p.strip() for line in out.splitlines() if " ATMA -> " in line
                    for p in line.split(" ATMA -> ", 1)[1].split("|"))
print(out, end="" if out.endswith("\n") else "\n")
