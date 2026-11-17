#!/usr/bin/env python3
"""#652 bench: a cleared pulse input lets go of its interrupt, and a release
of a forced-OFF relay is reported.

    python3 tools/bench/release_pulse_isr.py /dev/cu.usbmodemXXXX [--pins 6,19]

Uses two spare GPIOs (default 6 and 19: neither may be listed by SYSTEM PINS,
and nothing should be wired to them). No extra hardware: the relay's own pin
is the pulse source.

  1. RPM inputs on pin A and pin B, then CLEAR A and put a relay on A.
     Pin B's RPM input is the only one left, and the RPM ISR is shared, so if
     A's interrupt is still attached, toggling the relay makes B read a value.
     Fixed: B stays at 0 (no pulse within its timeout).
  2. A rule on B drives the relay; ACTION <id> OFF forces it off; a JSON
     import of the baseline must print "override released" for it.

Runs in CONFIG and never sends SAVE: it snapshots SYSTEM DUMP JSON first and
re-imports that snapshot at the end, then checks the dump came back identical.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
pins = [6, 19]
if "--pins" in args:
    pins = [int(x) for x in args[args.index("--pins") + 1].split(",")]
A, B = pins

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()
results = []


def read_raw(seconds, until=None):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        out += s.read(8192)
        if until and until in out:
            break
    return out.decode(errors="replace")


def read_for(seconds, until=None):
    lines = read_raw(seconds, until).splitlines()
    return "\n".join(l for l in lines if not CSV.match(l.strip()))


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    return read_for(wait)


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
    if not ok and detail:
        print("     " + detail.replace("\n", "\n     "))


def pin_holder(p):
    text = cmd("SYSTEM PINS", 1.5)
    for line in text.splitlines():
        mm = re.match(r"\s*Pin\s+(\S+):\s*(.*)", line)
        if mm and mm.group(1) == str(p):
            return mm.group(2).strip()
    return "Available"


def rpm_now():
    """B's value from INFO (new inputs aren't on the serial CSV route)."""
    m = re.search(r"Current Value:\s*(\S+)", cmd(f"INFO {B}", 0.6))
    return m.group(1) if m else "?"


def is_zero(v):
    try:
        return float(v) == 0.0
    except ValueError:
        return False


cmd("CONFIG")
base = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/release-pulse-isr-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline saved to {snap}")
for p in pins:
    if "Available" not in pin_holder(p):
        sys.exit(f"pin {p} is not free on this unit:\n{pin_holder(p)}")
if any(i.get("application") == "ENGINE_RPM" for i in base.get("inputs", [])):
    sys.exit("this unit already has an ENGINE_RPM input; the test needs B to be the only one")

# ---- 1. CLEAR detaches -------------------------------------------------------
r = cmd(f"SET {A} ENGINE_RPM W_PHASE_RPM") + cmd(f"SET {B} ENGINE_RPM W_PHASE_RPM")
# B: 1 pulse per rev, 2 s timeout, 1..100000 RPM, so any edge pair reads in range
r += cmd(f"SET {B} CAL RPM 2 1.0 2000 1 100000")
check("two RPM inputs set", "ERROR" not in r, r)
r = cmd(f"CLEAR {A}")
check(f"CLEAR {A}", "ERROR" not in r, r)
r = cmd(f"ACTION ADD RELAY {A}")
check(f"relay on pin {A}", "added" in r, r)
aid = [a["id"] for a in dump()["actions"] if a["target"] == A][0]

before = [rpm_now() for _ in range(3)]
check("B reads 0 with nothing on its pin", all(is_zero(v) for v in before), str(before))
vals = []
for _ in range(4):
    cmd(f"ACTION {aid} ON", 0.25)
    vals.append(rpm_now())
    cmd(f"ACTION {aid} OFF", 0.25)
    vals.append(rpm_now())
check("toggling the relay on the cleared pin doesn't reach B's RPM",
      all(is_zero(v) for v in vals), str(vals))

# ---- 2. a released FORCE_OFF is reported -------------------------------------
r = cmd(f"RULE ADD {B} BELOW 50 NONE")    # B reads 0: the rule is ACTIVE
rid = [x["id"] for x in dump()["rules"] if x.get("input") == B]
check("control rule on B added", "ERROR" not in r and rid, r)
if rid:
    r = cmd(f"ACTION {aid} RULE {rid[0]}")
    check("relay follows the rule", "ERROR" not in r, r)
    r = cmd(f"ACTION {aid} OFF")
    check("relay forced OFF", "ERROR" not in r, r)
    rep = import_doc(base)
    check("import reports the released force",
          f"NOTE: Action #{aid} override released, pin {A}" in rep, rep)
else:
    rep = import_doc(base)

# ---- restore -----------------------------------------------------------------
if "pplied" not in rep:
    rep = import_doc(base)
check("baseline re-imported", "pplied" in rep, rep)
check(f"pin {A} released", "Available" in pin_holder(A), pin_holder(A))
check(f"pin {B} released", "Available" in pin_holder(B), pin_holder(B))
check("the dump matches the baseline", comparable(dump()) == comparable(base))

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed")
sys.exit(1 if bad else 0)
