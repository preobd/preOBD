#!/usr/bin/env python3
"""Hold the obd_ecu_sim Mega's serial port open and pass knob commands to it.

Opening the Mega's port resets it, and the sim forgets its settings (silent,
listen-only, delay) when the port closes, so keep one holder running and send
knobs through it:

    python3 tools/bench/ecu_sim_hold.py [/dev/port] [knob ...]   # in its own terminal
    echo s > ~/.preobd/ecu_sim_fifo                         # toggle ECU silent
    echo l > ~/.preobd/ecu_sim_fifo                         # toggle listen-only (no ACK)

Knobs given on the command line are sent once at start (e.g. `s` to start
silent). The sim's output is appended to ~/.preobd/ecu_sim_log.txt. A line `Q`
stops the holder. The sim's knobs are in tools/bench/obd_ecu_sim (h for help).
"""
import os, select, sys, time
import serial

args = sys.argv[1:]
PORT = args.pop(0) if args and args[0].startswith("/dev/") else "/dev/cu.usbmodem11101"
HOME = os.path.expanduser("~/.preobd")
FIFO, LOG = os.path.join(HOME, "ecu_sim_fifo"), os.path.join(HOME, "ecu_sim_log.txt")
os.makedirs(HOME, exist_ok=True)
if not os.path.exists(FIFO):
    os.mkfifo(FIFO)

def log(t):
    with open(LOG, "a") as f:
        f.write(t)

s = serial.Serial(PORT, 115200, timeout=0.1)
time.sleep(2.5)   # the port open reset the Mega: let it boot
log("=== open\n" + s.read(4096).decode(errors="replace"))
for knob in args:
    s.write((knob + "\n").encode())
    time.sleep(0.3)
fd = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
while True:
    r, _, _ = select.select([fd], [], [], 0.5)
    if r:
        d = os.read(fd, 256)
        if not d:
            os.close(fd)
            fd = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
            continue
        if d.strip() == b"Q":
            break
        s.write(d if d.endswith(b"\n") else d + b"\n")
    o = s.read(4096)
    if o:
        log(o.decode(errors="replace"))
log("=== closed\n")
