#!/usr/bin/env python3
"""#639 phase 2 bench: a pressure input is shown in its application's reference.

    python3 tools/bench/pressure_reference.py /dev/cu.usbmodemXXXX [--pin 14 --label A0]
        [--baro-pin 19 --baro-label A5] [--baseline FILE] [--stay-config]

Needs one analog pin with a steady, in-range voltage (default 14 / A0 on the
teensy40 bench: a pin whose reading sits mid-scale; a pin held at the rail or
floating reads RAIL HIGH/LOW or noise, and the value is then NaN). That input
is re-pointed at MPX4250AP (an ABSOLUTE sensor) for the test; the baro input
is a free analog pin fed with TEST INJECT, so no sensor is needed. Everything
runs in CONFIG (sensors keep reading), so no relay is needed.

  1. LIST SENSORS / LIST APPLICATIONS JSON carry "reference" where it applies.
  2. The baro input (BAROMETRIC_PRESSURE) is injected 0.950 bar.
  3. MPX4250AP read as BOOST_PRESSURE (gauge) and as MAP_PRESSURE (absolute):
     MAP - BOOST equals the baro, at 0.950 and again at 1.000.
  4. TEST INJECT on the boost input shows as injected (not converted).
  5. SYSTEM DIAG INPUTS does not move the value out of the application's frame.
  6. (OBD PID 0x70 answer: skipped here, needs an OBD output on a CAN bus.)
  7. Baro NaN: the boost value holds (last good baro).

--can: #639 phase 3 instead: SET CAN 0x70 (needs the ECU sim answering 0x70 = 150 kPa absolute on a CAN input bus)
is BOOST_PRESSURE, converted by the baro (none: 1.01325; injected 0.950), switchable to MAP_PRESSURE and
back, OIL_TEMP refused, baro NaN holds. Deletes its inputs afterwards (the import restore).

--baseline FILE: a SYSTEM DUMP JSON to import first (to undo an earlier
interrupted run). Never sends SAVE: it snapshots SYSTEM DUMP JSON, re-imports it
at the end and checks the dump came back identical. Leaves the board in RUN
unless --stay-config.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]


def opt(name, default):
    return args[args.index(name) + 1] if name in args else default


PIN, LABEL = opt("--pin", "14"), opt("--label", "A0")
BARO, BLABEL = opt("--baro-pin", "19"), opt("--baro-label", "A5")
baseline_file = opt("--baseline", None)
CAN_MODE = "--can" in args

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.05)
time.sleep(0.3)
s.reset_input_buffer()
results = []


def read_for(seconds, until=None):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        out += s.read(8192)
        if until and until in out:
            break
    lines = out.decode(errors="replace").splitlines()
    return "\n".join(l for l in lines if not CSV.match(l.strip()))


def cmd(c, wait=1.0, until=None):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    return read_for(wait, until)


def dump():
    text = cmd("SYSTEM DUMP JSON", 4)
    i, j = text.find("{"), text.rfind("}")
    return json.loads(text[i:j + 1])


def comparable(d):
    d = copy.deepcopy(d)
    for k in ("firmware", "exportedAt", "timestamp", "mode"):
        d.pop(k, None)
    return d


def import_doc(doc):
    payload = json.dumps(doc, separators=(",", ":")).encode()
    s.reset_input_buffer()
    s.write(f"JSON IMPORT {len(payload)}\n".encode())
    head = read_for(3, b"bytes")
    if "Ready for" not in head:
        return head
    s.write(payload)
    return read_for(6, b"preOBD>")


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name)
    if detail:
        print("     " + str(detail).replace("\n", "\n     "))


def json_list(c):
    text = cmd(c, 4)
    return json.loads(text[text.find("["):text.rfind("]") + 1])


def value(label, samples=3):
    """Mean of LIST INPUTS readings for the input labelled e.g. A0 (None = not a number)."""
    vals = []
    for _ in range(samples):
        for line in cmd("LIST INPUTS", 0.6, b"preOBD>").splitlines():
            m = re.match(rf"\s*{label}:.*=\s*(\S+)\s", line + " ")
            if m:
                try:
                    vals.append(float(m.group(1)))
                except ValueError:
                    return None
    return sum(vals) / len(vals) if vals else None


def settle(sec=2.5):
    time.sleep(sec)       # warmup/persist (1 s + 0.5 s) plus margin


def fmt(v):
    return "None" if v is None else f"{v:.3f}"



def can_value(label):
    return value(label)


def run_can():
    """#639 phase 3: a CAN import is converted from its PID's reference to its application's."""
    r = cmd("SET CAN 0x70", 2.5)
    m = re.search(r"CAN sensor (CAN:\d+)", r)
    check("C1a. SET CAN 0x70 imports", bool(m), r)
    if not m:
        return
    node = m.group(1)
    label = "BOOST"
    settle(3)
    li = cmd("LIST INPUTS", 1, b"preOBD>")
    check("C1b. the import is BOOST_PRESSURE", f"{node}: BOOST (Boost Pressure" in li, li)
    raw = 1.50
    v = value(node)
    check(f"C1c. no baro: {fmt(v)} = 1.50 - 1.01325", v is not None and abs(v - (raw - 1.01325)) <= 0.01)
    r = cmd(f"SET {BARO} BAROMETRIC_PRESSURE GENERIC_BOOST")
    cmd(f"TEST INJECT {BARO} 0.950")
    settle(3)
    v = value(node)
    check(f"C2. baro 0.950: {fmt(v)} = 0.55", v is not None and abs(v - 0.55) <= 0.01)
    r = cmd(f"SET {node} APPLICATION MAP_PRESSURE")
    settle(3)
    v = value(node)
    check(f"C3a. MAP_PRESSURE accepted, reads {fmt(v)} = 1.50", "can't carry" not in r and v is not None and abs(v - raw) <= 0.01, r)
    r = cmd(f"SET {node} APPLICATION BOOST_PRESSURE")
    settle(3)
    v = value(node)
    check(f"C3b. back to BOOST_PRESSURE accepted, reads {fmt(v)}", "can't carry" not in r and v is not None and abs(v - 0.55) <= 0.01, r)
    r = cmd(f"SET {node} APPLICATION OIL_TEMP")
    check("C4. OIL_TEMP refused with can't carry", "can't carry" in r, r)
    print("SKIP C5. PID 0x0A: the sim does not answer it")
    before = value(node)
    cmd(f"TEST INJECT {BARO} NAN")
    settle(3)
    after = value(node)
    check(f"C6. baro NAN: holds ({fmt(before)} -> {fmt(after)})",
          before is not None and after is not None and abs(after - before) <= 0.02)

cmd("CONFIG", 1.5)
if baseline_file:
    text = open(os.path.expanduser(baseline_file)).read()
    print("baseline import:", "pplied" in import_doc(json.loads(text[text.find("{"):text.rfind("}") + 1])))
base = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/pressure-reference-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline saved to {snap}")

try:
    if CAN_MODE:
        run_can()
    else:
        # 1 -----------------------------------------------------------------------
        sens = {e["name"]: e for e in json_list("LIST SENSORS PRESSURE JSON")}
        temp = [e for e in json_list("LIST SENSORS TEMPERATURE JSON") if e.get("measurementType") == "TEMPERATURE"]
        apps = {e["name"]: e for e in json_list("LIST APPLICATIONS JSON")}
        check("1a. MPX4250AP is ABSOLUTE", sens.get("MPX4250AP", {}).get("reference") == "ABSOLUTE")
        check("1b. VDO_2BAR_CURVE is GAUGE", sens.get("VDO_2BAR_CURVE", {}).get("reference") == "GAUGE")
        check("1c. a temperature sensor has no reference key", bool(temp) and all("reference" not in e for e in temp),
              f"{len(temp)} temperature sensors checked")
        check("1d. MAP_PRESSURE is ABSOLUTE", apps.get("MAP_PRESSURE", {}).get("reference") == "ABSOLUTE")
        check("1e. BOOST_PRESSURE is GAUGE", apps.get("BOOST_PRESSURE", {}).get("reference") == "GAUGE")

        # 2 -----------------------------------------------------------------------
        r = cmd(f"SET {BARO} BAROMETRIC_PRESSURE GENERIC_BOOST")
        r += cmd(f"SET {PIN} BOOST_PRESSURE MPX4250AP")
        r += cmd(f"TEST INJECT {BARO} 0.950")
        check("2. baro input created and injected 0.950", "Injecting" in r and "Input " + PIN in r, r)
        settle()

        # 3 -----------------------------------------------------------------------
        for baro in (0.950, 1.000):
            cmd(f"TEST INJECT {BARO} {baro:.3f}")
            settle()
            cmd(f"SET {PIN} BOOST_PRESSURE MPX4250AP")
            settle()
            b = value(LABEL)
            cmd(f"SET {PIN} MAP_PRESSURE MPX4250AP")
            settle()
            m = value(LABEL)
            ok = b is not None and m is not None and abs((m - b) - baro) <= 0.02
            check(f"3. MAP - BOOST = {baro:.3f} +/- 0.02 (BOOST {fmt(b)}, MAP {fmt(m)})", ok,
                  None if (b is None or m is None) else f"delta {m - b:.3f}")
        cmd(f"SET {PIN} BOOST_PRESSURE MPX4250AP")
        cmd(f"TEST INJECT {BARO} 0.950")
        settle()

        # 4 -----------------------------------------------------------------------
        cmd(f"TEST INJECT {PIN} 0.5")
        settle(1.5)
        v = value(LABEL)
        check(f"4. TEST INJECT 0.5 on the boost input shows {fmt(v)} (not converted)", v is not None and abs(v - 0.5) <= 0.011)
        cmd("TEST CLEAR")
        cmd(f"TEST INJECT {BARO} 0.950")
        settle()

        # 5 -----------------------------------------------------------------------
        before = value(LABEL)
        diag = cmd("SYSTEM DIAG INPUTS", 3, b"preOBD>")
        after = value(LABEL, 1)
        check(f"5. DIAG INPUTS leaves the value in the application's frame ({fmt(before)} -> {fmt(after)})",
              before is not None and after is not None and abs(after - before) < 0.1 and f"pin {PIN}" in diag)

        # 6 -----------------------------------------------------------------------
        print("SKIP 6. OBD PID 0x70: no OBD output on a CAN bus (CAN1 is the input bus, one job per bus, #632)")

        # 7 -----------------------------------------------------------------------
        before = value(LABEL)
        cmd(f"TEST INJECT {BARO} NAN")
        settle()
        after = value(LABEL)
        check(f"7. baro NAN: boost holds ({fmt(before)} -> {fmt(after)})",
              before is not None and after is not None and abs(after - before) <= 0.02)
finally:
    # ---- restore ---------------------------------------------------------------
    cmd("TEST CLEAR")
    cmd("CONFIG", 1.5)
    rep = import_doc(base)
    check("baseline re-imported", "pplied" in rep, rep if "pplied" not in rep else "")
    check(f"pin {BARO} released", "Pin " + BLABEL not in cmd("SYSTEM PINS", 1.5))
    check("the dump matches the baseline", comparable(dump()) == comparable(base))
    if "--stay-config" not in args:
        cmd("RUN", 1.5)

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed")
sys.exit(1 if bad else 0)
