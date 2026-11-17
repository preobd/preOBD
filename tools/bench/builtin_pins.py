#!/usr/bin/env python3
"""#646 bench: a builtin (buzzer, user button) whose pin a stored input holds
reports it and leaves the pin alone; with its pin back, it works as before.

    python3 tools/bench/builtin_pins.py PORT stage    # on the STAGE build
    python3 tools/bench/builtin_pins.py PORT check [--hold]
    python3 tools/bench/builtin_pins.py PORT restore

A stored input wins its pin only at boot (§12.4), and the firmware rightly
refuses `SET <pin>` on a builtin's pin at runtime. So the inputs are stored
under a STAGE build that has no buzzer or button (wiring.ini untouched):

    PLATFORMIO_BUILD_FLAGS="-UBUZZER_PIN -UUSER_BUTTON_PIN" pio run -e teensy40 -t upload

  stage    reboot, snapshot SYSTEM DUMP JSON (the saved config) to
           ~/.preobd/builtin_pins_baseline.json, store FLOAT_SWITCH inputs on
           the buzzer (5) and button (4) pins, SAVE. Then flash the normal build.
  check    on the build under test: both pins held by the inputs, TEST OUTPUT
           skips the buzzer, both switches read their own pull-up (1.00; before
           #646 the buzzer drove its pin LOW and the switch read 0.00).
  check --hold   (run on its own; the operator presses when it says HOLD)
           waits up to 3 min for the user button to be held, which the pin-4
           switch reads as 0, reboots while it is held (CONFIG first: REBOOT is
           refused in RUN; fails if the port doesn't drop), and requires RUN
           (not CONFIG) and no "Display toggled" while it stays held; then waits
           for the release. The operator holds the button ~20 s, nothing else.
  restore  import the snapshot, SAVE, reboot, check the dump is identical,
           TEST OUTPUT chirps, SYSTEM PINS shows Buzzer and User Button again.

These cases SAVE on purpose (see above); the snapshot is what makes it safe.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if len(args) < 2 or args[1] not in ("stage", "check", "restore"):
    sys.exit(__doc__)
port, mode = args[0], args[1]
def opt(name, default):
    return int(args[args.index(name) + 1]) if name in args else default
BUZZER, BUTTON = opt("--buzzer", 5), opt("--button", 4)
HOLD = "--hold" in args
SNAP = os.path.expanduser("~/.preobd/builtin_pins_baseline.json")
CSV = re.compile(r"^([A-Za-z][A-Za-z0-9_]*),([-0-9.naNA]+),([^,]*)$")
s = None
results = []


def connect(timeout=20):
    global s
    t = time.time()
    while time.time() - t < timeout:
        try:
            s = serial.Serial(port, 115200, timeout=0.1)
            time.sleep(0.3)
            return
        except (serial.SerialException, OSError):
            time.sleep(0.1)
    sys.exit(f"{port} did not come back")


def read_raw(seconds, until=None):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        try:
            out += s.read(8192)
        except (serial.SerialException, OSError):
            break
        if until and until in out:
            break
    return out.decode(errors="replace")


def strip_csv(text):
    return "\n".join(l for l in text.splitlines() if not CSV.match(l.strip()))


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    return strip_csv(read_raw(wait))


def reboot(capture=4):
    """SYSTEM REBOOT is CONFIG-only (refused in RUN), so enter CONFIG first,
    then require the USB port to drop: a refused reboot would leave the port
    up and the 'boot' capture would be the live stream."""
    try:
        s.write(b"CONFIG\r\n")
        read_raw(0.8)
        s.write(b"SYSTEM REBOOT\r\n")
        t = time.time()
        while time.time() - t < 3:
            s.read(4096)            # raises once the Teensy leaves USB
        s.close()
        sys.exit("SYSTEM REBOOT did not reset the unit (port stayed up)")
    except (serial.SerialException, OSError):
        pass
    try:
        s.close()
    except Exception:
        pass
    time.sleep(0.5)
    connect()
    return read_raw(capture)


def dump():
    text = cmd("SYSTEM DUMP JSON", 4)
    i, j = text.find("{"), text.rfind("}")
    return json.loads(text[i:j + 1])


def comparable(d):
    d = copy.deepcopy(d)
    for k in ("firmware", "exportedAt", "timestamp", "mode"):
        d.pop(k, None)
    d.get("system", {}).pop("pins", None)   # the runtime registry, not config
    return d


def import_doc(doc):
    payload = json.dumps(doc, separators=(",", ":")).encode()
    s.reset_input_buffer()
    s.write(f"JSON IMPORT {len(payload)}\n".encode())
    head = read_raw(3, b"bytes")
    if "Ready for" not in head:
        return head
    s.write(payload)
    return read_raw(6, b"preOBD>")


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if not ok and detail:
        print("     " + detail.replace("\n", "\n     "), flush=True)


def pin_line(pins_text, pin):
    m = re.search(rf"^\s*Pin\s+{pin}:\s*(.*)$", pins_text, re.M)
    return m.group(1) if m else ""


def csv_values(text, name):
    return [m.group(2) for m in (CSV.match(l.strip()) for l in text.splitlines())
            if m and m.group(1) == name]


def wait_for(name, value, seconds, timeout):
    """Wait until the RUN-mode CSV shows `name` == value for `seconds`."""
    t0, since = time.time(), None
    buf = ""
    while time.time() - t0 < timeout:
        buf += read_raw(0.3)
        # split("\n"), not splitlines(): the last piece is the unfinished
        # line ("" after a newline). Keeping a finished line here glued it to
        # the next read, and OLVL, last in each burst, was lost every time.
        parts = buf.replace("\r", "").split("\n")
        buf = parts[-1]
        for v in csv_values("\n".join(parts[:-1]), name):
            if v.startswith(value):
                since = since or time.time()
            else:
                since = None
        if since and time.time() - since >= seconds:
            return True
    return False


connect()

if mode == "stage":
    reboot(capture=0)
    read_raw(20, until=b"preOBD>")
    read_raw(1)
    cmd("CONFIG", 1)
    pins = cmd("SYSTEM PINS", 2)
    if "Buzzer" in pins or "User Button" in pins:
        sys.exit("this is not the stage build (Buzzer/User Button still claimed); see --help")
    base = dump()
    os.makedirs(os.path.dirname(SNAP), exist_ok=True)
    with open(SNAP, "w") as f:
        json.dump(base, f)
    print(f"baseline saved to {SNAP}")
    for pin, app in ((BUZZER, "COOLANT_LEVEL"), (BUTTON, "OIL_LEVEL")):
        r = cmd(f"SET {pin} {app} FLOAT_SWITCH", 2)
        check(f"stage: input stored on pin {pin}", "configured" in r, r)
    print(cmd("SAVE", 2).strip().splitlines()[-2:])
    print("now flash the build under test, then run: check")

elif mode == "check" and HOLD:
    # Watching starts at once: the operator presses as soon as told.
    cmd("RUN", 0.5)
    print(">>> HOLD the user button now (about 20 s) ...", flush=True)
    held = wait_for("OLVL", "0", 1.0, 180)
    check("button held (OLVL reads 0)", held)
    if held:
        # Boot takes several seconds (SD, buses): read until the mode shows,
        # a CONFIG banner or RUN's CSV, not for a fixed time.
        boot = reboot(capture=0)
        t = time.time()
        while time.time() - t < 20 and "ENTERED CONFIG MODE" not in boot \
                and not csv_values(boot, "OLVL"):
            boot += read_raw(0.5)
        # "ENTERED CONFIG MODE" is msg.control and the CSV only streams in
        # RUN: those decide. The debug-level lines only add detail.
        check("held through boot: boots in RUN, not CONFIG",
              "ENTERED CONFIG MODE" not in boot and "CONFIG button detected" not in boot
              and bool(csv_values(boot, "OLVL")), strip_csv(boot)[-600:])
        check("OLVL still reads 0 after boot (the button really was held through it)",
              bool(csv_values(boot, "OLVL")) and csv_values(boot, "OLVL")[-1].startswith("0"),
              str(csv_values(boot, "OLVL")))
        more = read_raw(3)   # still held in RUN: past the 2 s long press
        check("still held 3 s into RUN (OLVL reads 0)",
              bool(csv_values(more, "OLVL")) and all(v.startswith("0") for v in csv_values(more, "OLVL")),
              str(sorted(set(csv_values(more, "OLVL")))))
        check("held in RUN: no long press (no display toggle)",
              "Display toggled" not in boot + more, strip_csv(more)[-300:])
        print(">>> release the button", flush=True)
        check("button released (OLVL back to 1)", wait_for("OLVL", "1", 1.0, 60))
    cmd("CONFIG", 1)

elif mode == "check":
    cmd("CONFIG", 1)
    pins = cmd("SYSTEM PINS", 2)
    for pin in (BUZZER, BUTTON):
        line = pin_line(pins, pin)
        check(f"pin {pin} held by its input", line.startswith("Input"), line or pins)
    t = cmd("TEST OUTPUT", 8)
    check("TEST OUTPUT skips the buzzer", "Buzzer skipped: not driven" in t, t)
    cmd("RUN", 0.5)
    run = read_raw(4)
    for name in ("CLVL", "OLVL"):
        vals = csv_values(run, name)
        check(f"{name} reads its own pull-up (1.00) on every sample",
              bool(vals) and all(v.startswith("1") for v in vals), f"{name}: {sorted(set(vals))}")
    cmd("CONFIG", 1)

elif mode == "restore":
    with open(SNAP) as f:
        base = json.load(f)
    cmd("CONFIG", 1)
    r = import_doc(base)
    check("baseline imported", "ERROR" not in r, r[-400:])
    cmd("SAVE", 2)
    reboot(capture=0)
    read_raw(20, until=b"preOBD>")   # boot takes several seconds
    read_raw(1)
    cmd("CONFIG", 1)
    check("dump identical to the baseline", comparable(dump()) == comparable(base))
    pins = cmd("SYSTEM PINS", 2)
    check("Buzzer and User Button hold their pins again",
          "Buzzer" in pin_line(pins, BUZZER) and "User Button" in pin_line(pins, BUTTON), pins)
    t = cmd("TEST OUTPUT", 8)
    check("TEST OUTPUT chirps the buzzer", "Buzzer -> chirp" in t, t)

print(f"\n{sum(ok for _, ok in results)}/{len(results)} passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
