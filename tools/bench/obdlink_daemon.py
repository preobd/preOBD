#!/usr/bin/env python3
"""Hold the OBDLink LX's Bluetooth serial port open for as long as you run it.

Closing the port drops the Bluetooth link, and the adapter then needs a manual
re-pair, so nothing else may open /dev/cu.OBDLinkLX: send commands through this
daemon instead (obdlink_send.py, or `echo 0105 > ~/.preobd/obdlink_fifo`).

    python3 tools/bench/obdlink_daemon.py [port]     # in its own terminal

Commands arrive one per line on ~/.preobd/obdlink_fifo; every reply is appended
to ~/.preobd/obdlink_log.txt. `ATMA <seconds>` monitors the bus for that long,
then stops the monitor itself, and logs everything it saw as one ATMA line. ATI goes out after 20 s of silence so the link never
idles. If the port vanishes (the adapter re-paired), it reopens it by itself.
Sessions check it is running (`pgrep -f obdlink_daemon`) and never start it.
"""
import os, select, sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.OBDLinkLX"
HOME = os.path.expanduser("~/.preobd")
FIFO = os.path.join(HOME, "obdlink_fifo")
LOG = os.path.join(HOME, "obdlink_log.txt")
os.makedirs(HOME, exist_ok=True)
if not os.path.exists(FIFO):
    os.mkfifo(FIFO)
s = None

def log(t):
    with open(LOG, "a") as f:
        f.write(t + "\n")

def reopen():
    global s
    while True:
        try:
            s = serial.Serial(PORT, 115200, timeout=0.3)
            log("=== port open")
            return
        except Exception:
            time.sleep(2)

def cmd1(c, w):
    s.reset_input_buffer()
    s.write((c + "\r").encode())
    t, o = time.time(), b""
    while time.time() - t < w:
        o += s.read(4096)
        if o.rstrip().endswith(b">"):
            break
    log(f"{time.strftime('%H:%M:%S')} {c} -> " + o.decode(errors="replace").replace("\r", " | ").strip())

def monitor(secs):
    """ATMA for `secs`, stopped by a keystroke; nothing read is thrown away."""
    s.reset_input_buffer()
    s.write(b"ATMA\r")
    t, o = time.time(), b""
    while time.time() - t < secs:
        o += s.read(8192)
    s.write(b"\r")   # any character stops ATMA
    t = time.time()
    while time.time() - t < 2 and not o.rstrip().endswith(b">"):
        o += s.read(8192)
    log(f"{time.strftime('%H:%M:%S')} ATMA -> " + o.decode(errors="replace").replace("\r", " | ").strip())

def cmd(c, w=2.5):
    for _ in range(2):
        try:
            p = c.split()
            if len(p) == 2 and p[0].upper() == "ATMA":
                return monitor(float(p[1]))
            return cmd1(c, w)
        except Exception as e:
            log(f"=== port error {e}; reopening")
            try:
                s.close()
            except Exception:
                pass
            reopen()

reopen()
log("=== daemon up")
fd = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
buf, idle = b"", time.time()
while True:
    r, _, _ = select.select([fd], [], [], 1.0)
    if r:
        d = os.read(fd, 4096)
        if not d:   # the writer closed: reopen to wait for the next one
            os.close(fd)
            fd = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
            continue
        buf += d
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            c = line.decode(errors="replace").strip()
            if c == "QUIT":
                log("=== daemon down")
                sys.exit(0)
            if c:
                cmd(c, 4 if c == "ATZ" else 2.5)
                idle = time.time()
    elif time.time() - idle > 20:
        cmd("ATI")
        idle = time.time()
