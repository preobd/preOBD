#!/usr/bin/env python3
"""#612 phase 4 bench: the HALTECH output on the wire, decoded by Haltech's own conversions.

    python3 tools/bench/haltech_bench.py <teensy port> <mega port> [--baseline FILE] [--stay-config]

Needs the ECU sim (tools/bench/obd_ecu_sim) flashed at 1 Mbit/s on the Mega
that shares CAN1 with the teensy40:

    cd tools/bench/obd_ecu_sim
    PLATFORMIO_BUILD_FLAGS='-D CAN_BITRATE_KBPS=1000' pio run -t upload --upload-port <mega port>

and flashed back at 500 kbit/s afterwards (plain `pio run -t upload`). The sim
ACKs the frames, prints them with `m` (ATMA form) and counts them with `k`.
Opens the Mega's port itself (it resets): no ecu_sim_hold.py running.

On the teensy40 bench (b1111 inputs: A0 BOOST, A1 TCASE, A2 COOLANT, A3 OIL
PRESSURE, A11 MAP, CAN:0 RPM; CAN input on CAN1 at 500 kbit/s) it:
  1. refuses HALTECH on CAN input's bus, then on a 500 kbit/s bus;
  2. adds it at 1 Mbit/s, and refuses BUS CAN CAN1 BAUDRATE 500000 under it;
  3. adds a baro (A5), a battery (A4) and a fuel pressure (A10) input and
     injects known values everywhere;
  4. decodes a capture with haltech_decode.py's table (copied from the PDF, not
     the firmware) and checks every value, that only the four frames with an
     input are sent, and that unfilled fields are 0;
  5. checks OUTPUT <id>'s detail, a route switched off (its frame leaves the
     bus), a NaN reading (0, frame still sent), a PREOBD output beside it;
  6. measures the rate at INTERVAL 20 from the sim's counts and the output's
     own pass counter.
Never sends SAVE: it snapshots SYSTEM DUMP JSON, re-imports it at the end and
checks the dump came back identical. Leaves the board in RUN unless --stay-config.
--replies FILE: also writes every command's reply verbatim to FILE (JSON, in
order), for the app's mock fixtures.
"""
import copy, json, os, re, sys, time
import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from haltech_decode import SPEC, field   # noqa: E402  (the spec's table, not the firmware's)

args = sys.argv[1:]
if len(args) < 2:
    sys.exit(__doc__)
tport, mport = args[0], args[1]
baseline_file = args[args.index("--baseline") + 1] if "--baseline" in args else None
replies_file = args[args.index("--replies") + 1] if "--replies" in args else None
replies = []

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
FRAME = re.compile(r"^([0-9A-F]{3})((?: [0-9A-F]{2}){1,8})\s*$")
t = serial.Serial(tport, 115200, timeout=0.05)
time.sleep(0.3)
t.reset_input_buffer()
results = []

# Pins on the teensy40 bench
COOLANT, OIL, MAP, BOOST, RPM = "16", "17", "25", "14", "CAN:0"
BARO, BATT, FUEL = "19", "18", "24"


def read_for(port, seconds, until=None):
    t0, out = time.time(), b""
    while time.time() - t0 < seconds:
        out += port.read(8192)
        if until and until in out:
            break
    return out.decode(errors="replace")


def cmd(c, wait=1.0, until=None):
    t.reset_input_buffer()
    t.write((c + "\r\n").encode())
    lines = read_for(t, wait, until).splitlines()
    out = "\n".join(l for l in lines if not CSV.match(l.strip()))
    if replies_file and not c.startswith("SYSTEM DUMP"):
        replies.append({"command": c, "reply": out})
    return out


def dump():
    text = cmd("SYSTEM DUMP JSON", 4)
    return json.loads(text[text.find("{"):text.rfind("}") + 1])


def comparable(d):
    d = copy.deepcopy(d)
    for k in ("firmware", "exportedAt", "timestamp", "mode"):
        d.pop(k, None)
    return d


def import_doc(doc):
    payload = json.dumps(doc, separators=(",", ":")).encode()
    t.reset_input_buffer()
    t.write(f"JSON IMPORT {len(payload)}\n".encode())
    head = read_for(t, 3, b"bytes")
    if "Ready for" not in head:
        return head
    t.write(payload)
    return read_for(t, 6, b"preOBD>")


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name)
    if detail and not ok:
        print("     " + str(detail).replace("\n", "\n     "))


# ---- the sim ----
m = serial.Serial(mport, 115200, timeout=0.05)
boot = read_for(m, 4)                       # opening the port resets the Mega


def sim(c, wait=0.3):
    m.reset_input_buffer()
    m.write((c + "\n").encode())
    return read_for(m, wait)


def capture(seconds):
    """Frames the sim saw, as (id, bytes), with the monitor on for `seconds`."""
    sim("m", 0.1)
    m.reset_input_buffer()
    text = read_for(m, seconds)
    sim("m", 0.2)
    frames = []
    for line in text.splitlines():
        g = FRAME.match(line.strip())
        if g:
            frames.append((int(g.group(1), 16), bytes(int(x, 16) for x in g.group(2).split())))
    return frames


def last_by_id(frames):
    out = {}
    for fid, data in frames:
        if len(data) == 8:
            out[fid] = data
    return out


def counts(text):
    g = re.search(r"\[count\] (\d+) ms:(.*)", text)
    if not g:
        return None, {}
    return int(g.group(1)), {int(k, 16): int(v) for k, v in re.findall(r"([0-9A-F]+)=(\d+)", g.group(2))}


def raw(data, first, signed=False):
    return field(data, first, signed)


cmd("CONFIG", 1.5)
if baseline_file:
    text = open(os.path.expanduser(baseline_file)).read()
    print("baseline import:", "pplied" in import_doc(json.loads(text[text.find("{"):text.rfind("}") + 1])))
base = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/haltech-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline saved to {snap}")
print("sim boot:", " | ".join(l for l in boot.splitlines() if "obd_ecu_sim" in l or "kbps" in l.lower() or "bit" in l.lower())[:200])

oid = None
try:
    # 1 -------------------------------------------------------------------------
    r = cmd("OUTPUT ADD HALTECH CAN1")
    check("1a. refused on CAN input's bus (one job per bus)", "one job per bus" in r and "ERROR" in r, r)
    cmd("BUS CAN CAN1 INPUT DISABLE", 1.5)
    r = cmd("OUTPUT ADD HALTECH CAN1")
    check("1b. refused on a 500 kbit/s bus, naming the rate", "needs a 1 Mbit/s bus" in r, r)

    # 2 -------------------------------------------------------------------------
    r = cmd("BUS CAN CAN1 BAUDRATE 1000000", 2)
    check("2a. CAN1 to 1 Mbit/s", "ERROR" not in r, r)
    r = cmd("OUTPUT ADD HALTECH CAN1", 2)
    g = re.search(r"[Oo]utput (\d+)", r)
    check("2b. HALTECH added at 1 Mbit/s", g is not None and "ERROR" not in r, r)
    oid = g.group(1) if g else None
    r = cmd("OUTPUT ADD HALTECH CAN1", 2)
    check("2e. a second HALTECH on the bus refused (same IDs)", "shares CAN IDs" in r and "one of each layout per bus" in r, r)
    r = cmd("OUTPUT ADD FOO CAN1")
    check("2f. the format list names HALTECH", "Formats: CSV, REALDASH, OBD, ELM327, PREOBD, HALTECH" in r, r)
    r = cmd("BUS CAN CAN1 BAUDRATE 500000", 1.5)
    check("2c. BAUDRATE 500000 refused under it", f"output {oid} (HALTECH) needs 1000 kbit/s" in r, r)
    bc = cmd("BUS CAN", 1.5)
    check("2d. CAN1 still at 1 Mbit/s, with the layout output attached", "1000kbps" in bc and "layout output" in bc, bc)

    # 3 -------------------------------------------------------------------------
    for c in (f"SET {BARO} BAROMETRIC_PRESSURE GENERIC_BOOST", f"SET {BATT} PRIMARY_BATTERY VOLTAGE_DIVIDER",
              f"SET {FUEL} FUEL_PRESSURE GENERIC_BOOST"):
        r = cmd(c, 1.5)
        # (BAROMETRIC_PRESSURE logs an I2C error for its default sensor on this bench; the input is set all the same)
        check(f"3. {c}", "configured as" in r, r)
    # The bench's CAN:0 was imported before #636 at application NONE, which no
    # layout maps: give it ENGINE_RPM (the import restore puts it back)
    r = cmd(f"SET {RPM} APPLICATION ENGINE_RPM", 1.5)
    check(f"3. SET {RPM} APPLICATION ENGINE_RPM", "ERROR" not in r, r)
    inject = {COOLANT: "90", OIL: "3.0", MAP: "1.0", BOOST: "0.5", RPM: "3000",
              BARO: "0.95", BATT: "13.8", FUEL: "4.0"}
    for pin, v in inject.items():
        r = cmd(f"TEST INJECT {pin} {v}")
        check(f"3. TEST INJECT {pin} {v}", "Injecting" in r, r)
    cmd(f"OUTPUT {oid} INTERVAL 200")
    time.sleep(2)

    # 4 -------------------------------------------------------------------------
    frames = capture(3)
    last = last_by_id(frames)
    seen = set(last)
    check(f"4a. frames captured ({len(frames)})", len(frames) >= 30, frames[:5])
    check("4b. exactly the frames with an input: 0x360 0x361 0x372 0x3E0",
          seen == {0x360, 0x361, 0x372, 0x3E0}, sorted(hex(i) for i in seen))
    f360, f361, f372, f3e0 = (last.get(i, bytes(8)) for i in (0x360, 0x361, 0x372, 0x3E0))
    check("4c. RPM 3000", raw(f360, 0) == 3000, f360.hex(" "))
    check("4d. MAP 100.0 kPa abs (1.0 bar, absolute already)", raw(f360, 2) == 1000, f360.hex(" "))
    check("4e. TPS and coolant pressure 0 (no input)", raw(f360, 4) == 0 and raw(f360, 6) == 0, f360.hex(" "))
    check("4f. fuel 4.0 bar gauge + baro 0.95 = 495.0 kPa abs", raw(f361, 0) == 4950, f361.hex(" "))
    check("4g. oil 3.0 bar gauge + baro 0.95 = 395.0 kPa abs", raw(f361, 2) == 3950, f361.hex(" "))
    check("4h. engine demand, wastegate 0", raw(f361, 4) == 0 and raw(f361, 6) == 0, f361.hex(" "))
    check("4i. battery 13.8 V", raw(f372, 0) == 138, f372.hex(" "))
    check("4j. baro 95.0 kPa abs (absolute already)", raw(f372, 6) == 950, f372.hex(" "))
    check("4k. coolant 90 C = 363.15 K (x10, 3632)", raw(f3e0, 0) == 3632, f3e0.hex(" "))
    check("4l. air, fuel, oil temperature 0 (no input)", raw(f3e0, 2) == 0 and raw(f3e0, 4) == 0 and raw(f3e0, 6) == 0,
          f3e0.hex(" "))
    check("4m. no frame has a byte the spec leaves out non-zero",
          all(all(b == 0 for i, b in enumerate(d) if not any(i in (s[0], s[0] + 1) for s in SPEC[fid]))
              for fid, d in last.items() if fid in SPEC))
    # The dash's view, as Haltech's conversion reads it
    print("     dash reads: oil %.1f kPa gauge, fuel %.1f kPa gauge, coolant %.2f C" %
          (raw(f361, 2) / 10 - 101.3, raw(f361, 0) / 10 - 101.3, raw(f3e0, 0) / 10 - 273.15))

    # 5 -------------------------------------------------------------------------
    d = cmd(f"OUTPUT {oid}", 1.5)
    check("5a. OUTPUT <id> lists the 11 IDs and the no-data rule",
          "Frames: 11 at 0x360 0x361 0x36B 0x370 0x372 0x373 0x374 0x376 0x3E0 0x3E1 0x6F8" in d and
          "is not sent" in d and "Bus load" in d, d)
    r = cmd(f"SET {COOLANT} ROUTE {oid} DISABLE")
    time.sleep(1)
    seen = set(last_by_id(capture(2)))
    check("5b. coolant unrouted: 0x3E0 leaves the bus, the rest stay", seen == {0x360, 0x361, 0x372}, (r, sorted(map(hex, seen))))
    cmd(f"SET {COOLANT} ROUTE {oid} ENABLE")
    cmd(f"TEST INJECT {COOLANT} NAN")
    time.sleep(1)
    last = last_by_id(capture(2))
    check("5c. coolant NaN: 0x3E0 still sent, coolant 0", 0x3E0 in last and raw(last[0x3E0], 0) == 0,
          last.get(0x3E0, b"").hex(" "))
    cmd(f"TEST INJECT {COOLANT} 90")
    r = cmd("OUTPUT ADD PREOBD CAN1 INTERVAL 200", 2)
    g = re.search(r"[Oo]utput (\d+)", r)
    pid_ = g.group(1) if g else None
    time.sleep(1)
    seen = set(last_by_id(capture(2)))
    check("5d. PREOBD beside it on one bus: both sets of IDs", pid_ is not None and 0x500 in seen and 0x360 in seen,
          (r, sorted(map(hex, seen))))
    if pid_:
        cmd(f"OUTPUT DEL {pid_}", 1.5)

    # 6 -------------------------------------------------------------------------
    cmd(f"OUTPUT {oid} INTERVAL 20")
    time.sleep(1)
    PASSES = r"Passes sent: (\d+), skipped \(still leaving\): (\d+), unACKed \(dropped\): (\d+)"
    t0 = time.time()
    before = re.search(PASSES, cmd(f"OUTPUT {oid}", 1.0, b"unACKed"))
    t0 = (t0 + time.time()) / 2
    sim("k")
    time.sleep(5)
    ms, c = counts(sim("k"))
    t1 = time.time()
    after = re.search(PASSES, cmd(f"OUTPUT {oid}", 1.0, b"unACKed"))
    t1 = (t1 + time.time()) / 2
    if before and after and ms:
        passes = int(after.group(1)) - int(before.group(1))
        per_s = passes / (t1 - t0)
        rate = {hex(k): round(v * 1000 / ms, 1) for k, v in c.items()}
        print(f"     output: {passes} passes in {t1 - t0:.2f} s = {per_s:.1f}/s, skipped {after.group(2)}, unACKed {after.group(3)}")
        print(f"     frames/s at the sim (a floor: the MCP2515's two receive buffers drop in a burst): {rate}")
        check(f"6a. the output paces 50 passes/s at INTERVAL 20 ({per_s:.1f}/s)", 47 <= per_s <= 53)
        check("6b. the sim sees the four IDs, the first at the full rate (>= 48/s)",
              set(c) == {0x360, 0x361, 0x372, 0x3E0} and c[0x360] * 1000 / ms >= 48, c)
        check("6c. no skipped or unACKed passes", after.group(2) == before.group(2) and after.group(3) == before.group(3))
    else:
        check("6. counters read", False, (before, after, ms))
finally:
    # ---- restore ---------------------------------------------------------------
    cmd("TEST CLEAR")
    cmd("CONFIG", 1.5)
    rep = import_doc(base)
    check("baseline re-imported", "pplied" in rep, rep)
    check("the dump matches the baseline", comparable(dump()) == comparable(base))
    bc = cmd("BUS CAN", 1.5)
    check("CAN1 back at 500 kbit/s with CAN input", "CAN1: NORMAL @ 500kbps" in bc and "CAN input" in bc, bc)
    if "--stay-config" not in args:
        cmd("RUN", 1.5)
    m.close()

if replies_file:
    with open(os.path.expanduser(replies_file), "w") as f:
        json.dump(replies, f, indent=1)
    print(f"replies written to {replies_file}")
fails = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(fails)}/{len(results)} passed")
sys.exit(1 if fails else 0)
