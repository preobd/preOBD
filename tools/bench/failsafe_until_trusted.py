#!/usr/bin/env python3
"""#669 bench: a FAILSAFE ON relay starts ON at the claim and stays ON until its
rule has a trusted verdict; a FAILSAFE OFF relay still starts OFF.

    python3 tools/bench/failsafe_until_trusted.py /dev/cu.usbmodemXXXX [--pins 6,20,21]

Pins (none may be listed by SYSTEM PINS):
  6   SOURCE: a FLOAT_SWITCH input, NOT wired. It reads its own pull-up, so its
      value is known from power-on and survives a reboot (TEST INJECT doesn't).
      The rule's limit is set around that value to make a hot, cold or in-band
      start.
  20  RELAY: the Action's target.
  21  SENSE: a FLOAT_SWITCH input JUMPERED to the relay pin, so the script reads
      the pin's real level, not just the firmware's reported state.

Each case stores its config with SAVE, reboots into RUN (a stored config and no
button) and polls ACTION LIST and INFO <sense> from the moment USB is back:
  hot    ABOVE (v - 0.5), HYST 0: ON from the first sample, never OFF, ends ACTIVE.
  cold   ABOVE (v + 5),   HYST 0: ON (FAILSAFE) first, then OFF once the rule settles.
  band   ABOVE (v + 0.5), HYST 1: v is inside the band (above limit-hyst, not above
         limit): never settles, so ON (FAILSAFE) for the whole capture.
  off    as hot, FAILSAFE OFF: OFF first, ON once the rule qualifies.
In every sample the sense pin must agree with the reported state.

USB is back about 3 s after a reboot, after the claim; a sub-millisecond blip at
the claim itself is the native test's job (test_membership, pin event log).

SAVEs on purpose (a reboot is the case). Safe because: it refuses to start if
the live config differs from the saved one (unsaved changes would be lost); it
snapshots the saved config to ~/.preobd/; and at the end it re-imports that
snapshot, SAVEs, reboots and checks the dump is identical.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
pins = [6, 20, 21]
if "--pins" in args:
    pins = [int(x) for x in args[args.index("--pins") + 1].split(",")]
SRC, RELAY, SENSE = pins
CAPTURE_S = 14.0

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = None
results = []


def connect(timeout=20):
    global s
    t = time.time()
    while time.time() - t < timeout:
        try:
            s = serial.Serial(port, 115200, timeout=0.05)
            return
        except (serial.SerialException, OSError):
            time.sleep(0.05)
    sys.exit(f"{port} did not come back")


def read_for(seconds, until=None):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        try:
            out += s.read(8192)
        except (serial.SerialException, OSError):
            break
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
    d.get("system", {}).pop("pins", None)   # the runtime registry, not config
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
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if not ok and detail:
        print("     " + detail.replace("\n", "\n     "), flush=True)


def pin_holder(p):
    for line in cmd("SYSTEM PINS", 1.5).splitlines():
        mm = re.match(r"\s*Pin\s+(\S+):\s*(.*)", line)
        if mm and mm.group(1) == str(p):
            return mm.group(2).strip()
    return "Available"


def reboot():
    """SYSTEM REBOOT is CONFIG-only; require the port to drop, then reconnect
    as fast as possible (the firmware waits up to 3 s for the host)."""
    try:
        s.write(b"CONFIG\r\n")
        read_for(0.8)
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
    t_reset = time.time()
    time.sleep(0.2)
    connect()
    return t_reset


def input_value(pin):
    # A reply can be cut by the 500 ms CSV stream or arrive late: retry.
    for _ in range(3):
        text = cmd(f"INFO {pin}", 0.5, b"To see its routes")
        mm = re.search(r"Current Value:\s*([-0-9.]+)", text)
        if mm:
            return float(mm.group(1))
    return None


def action_line(aid):
    text = cmd("ACTION LIST", 0.25, b"preOBD>")
    for line in text.splitlines():
        if re.search(rf"#{aid}\b", line):
            return line
    return ""


def relay_on(line):
    return re.search(r"-> ON\b", line) is not None


def rule_state(rid):
    for _ in range(3):
        text = cmd("RULE LIST", 1.0, b"System severity")
        for line in text.splitlines():
            if re.match(rf"\s*#{rid}\s", line):
                for st in ("ACTIVE", "READY", "WARMUP", "INIT", "DISABLED"):
                    if re.search(rf"\b{st}\b", line):
                        return st
    return "?"


def capture(aid):
    """[(t since port back, on, failsafe marker, sense value)] for CAPTURE_S."""
    out, t0 = [], time.time()
    while time.time() - t0 < CAPTURE_S:
        line = action_line(aid)
        v = input_value(SENSE)
        if line:
            out.append((time.time() - t0, relay_on(line), "FAILSAFE" in line, v))
    return out


def setup_case(limit, hyst, failsafe):
    """Rule + relay for one case, SAVEd. Returns (rule id, action id)."""
    cmd("CONFIG", 1.5)
    d = dump()
    for a in d["actions"]:
        if a["target"] == RELAY:
            cmd(f"ACTION DEL {a['id']}")
    for r in d["rules"]:
        if r.get("input") == SRC:
            cmd(f"RULE DEL {r['id']}")
    r = cmd(f"RULE ADD {SRC} ABOVE {limit} HYST {hyst} NONE")
    rid = [x["id"] for x in dump()["rules"] if x.get("input") == SRC]
    r += cmd(f"ACTION ADD RELAY {RELAY} FAILSAFE {failsafe}")
    aid = [a["id"] for a in dump()["actions"] if a["target"] == RELAY]
    if len(rid) != 1 or len(aid) != 1:
        sys.exit(f"case setup failed:\n{r}")
    r += cmd(f"ACTION {aid[0]} RULE {rid[0]}")
    r += cmd("SAVE", 2)
    if "ERROR" in r:
        sys.exit(f"case setup failed:\n{r}")
    return rid[0], aid[0]


def sense_agrees(samples, on_level):
    bad = [x for x in samples if x[3] is None or (x[3] == on_level) != x[1]]
    return not bad, bad[:3]


# ---- baseline ----------------------------------------------------------------
connect()
time.sleep(0.3)
cmd("CONFIG", 1.5)
live = dump()
cmd("LOAD", 3)
saved = dump()
if comparable(live) != comparable(saved):
    import_doc(live)                                  # put the live config back
    sys.exit("The live config has unsaved changes (it differs from the saved one).\n"
             "SAVE or LOAD it yourself first; this script SAVEs and would lose them.")
snap = time.strftime(os.path.expanduser("~/.preobd/failsafe-until-trusted-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(saved, f, indent=1)
print(f"saved config snapshot: {snap}", flush=True)
for p in pins:
    if "Available" not in pin_holder(p):
        sys.exit(f"pin {p} is not free on this unit:\n{pin_holder(p)}")

r = cmd(f"SET {SRC} COOLANT_LEVEL FLOAT_SWITCH") + cmd(f"SET {SRC} ALARM OFF")
r += cmd(f"SET {SENSE} COOLANT_LEVEL FLOAT_SWITCH") + cmd(f"SET {SENSE} ALARM OFF")
check("source and sense inputs set", "ERROR" not in r, r)
time.sleep(0.5)
v = input_value(SRC)
check("source reads its pull-up", v is not None, f"INFO {SRC}: {v}")


def bail():
    """Before anything is SAVEd: put the saved config back in RAM and stop."""
    cmd("CONFIG", 1.5)
    import_doc(saved)
    cmd("RUN", 1.5)
    sys.exit(1)


if v is None:
    bail()
print(f"     source value {v}", flush=True)

# The sense level for ON: a manual relay switched on and off by hand.
r = cmd(f"ACTION ADD RELAY {RELAY}")
aid = [a["id"] for a in dump()["actions"] if a["target"] == RELAY][0]
cmd(f"ACTION {aid} ON"); time.sleep(0.4); on_level = input_value(SENSE)
cmd(f"ACTION {aid} OFF"); time.sleep(0.4); off_level = input_value(SENSE)
cmd(f"ACTION DEL {aid}")
check(f"jumper: pin {SENSE} follows pin {RELAY}",
      on_level is not None and off_level is not None and on_level != off_level,
      f"ON reads {on_level}, OFF reads {off_level} (is the jumper on {RELAY}<->{SENSE}?)")
if on_level is None or on_level == off_level:
    bail()

cases = [
    ("hot",  v - 0.5, 0, "ON"),
    ("cold", v + 5.0, 0, "ON"),
    ("band", v + 0.5, 1, "ON"),
    ("off",  v - 0.5, 0, "OFF"),
]
try:
    for name, limit, hyst, fs in cases:
        rid, aid = setup_case(limit, hyst, fs)
        reboot()
        samples = capture(aid)
        st = rule_state(rid)
        if not samples:
            check(f"{name}: samples captured", False)
            continue
        first = samples[0]
        print(f"--- {name}: ABOVE {limit} HYST {hyst} FAILSAFE {fs}; {len(samples)} samples "
              f"over {CAPTURE_S:.0f} s from the port coming back; rule ends {st}", flush=True)
        ok, bad = sense_agrees(samples, on_level)
        check(f"{name}: pin {SENSE} agrees with the reported state in every sample", ok, repr(bad))
        offs = [x for x in samples if not x[1]]
        ons = [x for x in samples if x[1]]
        if name == "hot":
            check("hot: ON from the first sample, never OFF", not offs, repr(offs[:3]))
            check("hot: ends ACTIVE, no FAILSAFE marker", st == "ACTIVE" and not samples[-1][2],
                  f"{st} {samples[-1]}")
        elif name == "cold":
            check("cold: ON (FAILSAFE) at the first sample", first[1] and first[2], repr(first))
            went_off = next((x for x in samples if not x[1]), None)
            check("cold: OFF once the rule settles, and stays OFF",
                  went_off is not None and all(not x[1] for x in samples if x[0] >= went_off[0]),
                  repr(samples[-3:]))
            if went_off:
                print(f"     OFF at {went_off[0]:.2f} s after the port came back", flush=True)
            check("cold: ends READY", st == "READY", st)
        elif name == "band":
            check("band: ON (FAILSAFE) for the whole capture", not offs and all(x[2] for x in samples),
                  repr(offs[:3] or [x for x in samples if not x[2]][:3]))
            check("band: the rule stays READY (no verdict)", st == "READY", st)
        elif name == "off":
            check("off: OFF at the first sample (FAILSAFE OFF starts OFF)", not first[1], repr(first))
            check("off: ON once the rule qualifies", bool(ons) and samples[-1][1], repr(samples[-3:]))
finally:
    # ---- restore ---------------------------------------------------------------
    cmd("CONFIG", 1.5)
    rep = import_doc(saved)
    check("snapshot re-imported", "pplied" in rep, rep)
    if "pplied" not in rep:
        # Never SAVE over the owner's config with the test's in RAM: the last
        # case's config is what's stored now, so say where the snapshot is.
        print(f"RESTORE FAILED: nothing SAVEd. The unit's stored config is the last "
              f"test case's. Restore it with JSON IMPORT of {snap}, then SAVE.", flush=True)
        sys.exit(1)
    cmd("SAVE", 2)
    reboot()
    time.sleep(4)
    cmd("CONFIG", 1.5)
    check("the saved config matches the snapshot after a reboot",
          comparable(dump()) == comparable(saved))
    for p in pins:
        check(f"pin {p} released", "Available" in pin_holder(p), pin_holder(p))
    cmd("RUN", 1.5)   # where a stored config boots anyway

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed", flush=True)
sys.exit(1 if bad else 0)
