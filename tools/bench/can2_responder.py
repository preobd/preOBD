#!/usr/bin/env python3
"""#560 bench check (2): the OBD responder on CAN2, on code after the review
fixes (claim gate, HAL started-guard). The Mega sim, silenced, is the tester.

    python3 tools/bench/can2_responder.py TEENSY_PORT MEGA_PORT [--reps N]
    python3 tools/bench/can2_responder.py TEENSY_PORT MEGA_PORT --abort

--abort: #560's abortUnackedTx on bus 1 instead (#506/#522 on CAN2): CAN
input moved to CAN2; the sim's poll rate answering, then listen-only (no
ACK: retransmissions must stay bounded, ~3,600/s if the abort fails), then
back (RPM reads again); and no reset (#522 saw a watchdog reset on bus 0
when the mailbox ISR was left unmasked).

Needs: the nRF off Serial1 and a CAN transceiver on pins 0/1 (CAN2) on the
bus with the obd_ecu_sim Mega. No ecu_sim_hold.py running (this opens the
Mega's port itself; opening it resets the Mega, so the sim starts answering
and is then silenced with `s`).

Freeing pins 0/1 from a FRAMED Serial1 needs a reboot, so this SAVEs on
purpose: it refuses unsaved changes, snapshots the saved config to
~/.preobd/, stores Serial1 unframed with no secondary console, reboots,
disables Serial1 (RAM only), enables CAN2, adds an OBD output on CAN2 and
routes the inputs to it (the unit's OBD output moves; there is one per unit).
Then the sim sends Mode 01 requests on 0x7DF and
0x7E0 and counts 0x7E8 answers. Restore: import the snapshot, SAVE, reboot,
put the secondary console back by command (an import never moves the
console, #561), compare the dump.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if len(args) < 2:
    sys.exit(__doc__)
port, mega_port = args[0], args[1]
MODE = "abort" if "--abort" in args else "responder"
REPS = int(args[args.index("--reps") + 1]) if "--reps" in args else 5

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
    d.get("system", {}).pop("pins", None)
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


def reboot():
    try:
        s.write(b"CONFIG\r\n")
        read_for(0.8)
        s.write(b"SYSTEM REBOOT\r\n")
        t = time.time()
        while time.time() - t < 3:
            s.read(4096)
        s.close()
        sys.exit("SYSTEM REBOOT did not reset the unit (port stayed up)")
    except (serial.SerialException, OSError):
        pass
    try:
        s.close()
    except Exception:
        pass
    time.sleep(0.2)
    connect()
    time.sleep(4)          # boot (the firmware waits for the host)


# ---- snapshot ----------------------------------------------------------------
connect()
time.sleep(0.3)
cmd("CONFIG", 1.5)
live = dump()
cmd("LOAD", 3)
saved = dump()
if comparable(live) != comparable(saved):
    import_doc(live)
    sys.exit("The live config has unsaved changes; SAVE or LOAD it first (this script SAVEs).")
snap = time.strftime(os.path.expanduser("~/.preobd/can2-responder-%Y%m%d-%H%M%S.json"))
with open(snap, "w") as f:
    json.dump(saved, f, indent=1)
print(f"saved config snapshot: {snap}", flush=True)

try:
    # ---- stage: Serial1 unframed, stored, reboot ------------------------------
    r = cmd("BUS SERIAL 1 FRAMED DISABLE", 1.5) + cmd("CONSOLE TRANSPORT NONE SECONDARY", 1.5)
    print(r)
    cmd("SAVE", 2)
    reboot()
    cmd("CONFIG", 1.5)
    r = cmd("BUS SERIAL 1 DISABLE", 2)
    print(r)
    check("Serial1 disabled (unframed after the reboot)", "Serial1 disabled" in r, r)
    r = cmd("BUS CAN CAN2 ENABLE", 3)
    print(r)
    check("BUS CAN CAN2 ENABLE accepted", "ERROR" not in r, r)
    if MODE == "abort":
        r = cmd("BUS CAN CAN2 INPUT ENABLE", 3)
        print(r)
        out = cmd("BUS CAN", 1.5)
        print(out)
        check("CAN2 up with CAN input", re.search(r"CAN2: NORMAL.*up.*CAN input", out) is not None, out)
    else:
        # One OBD output per unit: move it (or add one if the unit has none)
        obd = next((o["id"] for o in dump()["system"]["outputs"] if o["format"] == "OBD"), None)
        r = cmd(f"OUTPUT {obd} TRANSPORT CAN2", 2) if obd is not None else cmd("OUTPUT ADD OBD CAN2", 2)
        print(r)
        oid = next((o["id"] for o in dump()["system"]["outputs"]
                    if o["format"] == "OBD" and o["transport"] == "CAN2"), None)
        check("OBD output on CAN2", oid is not None, r)
        if oid is None:
            raise RuntimeError("no OBD output on CAN2")
        print(cmd(f"OUTPUT {oid} ENABLE", 1.5))
        for p in ("A0", "A2"):            # BST (0x6F family), ECT (0x05)
            cmd(f"SET {p} ROUTE {oid} ENABLE", 1.5)
        print(cmd(f"OUTPUT {oid}", 1.5))
        out = cmd("BUS CAN", 1.5)
        print(out)
        check("CAN2 up", re.search(r"CAN2: NORMAL.*up", out) is not None, out)
    pins = cmd("SYSTEM PINS", 1.5)
    check("pins 0/1 held by CAN2", re.search(r"Pin  [01]: Reserved  - CAN2", pins) is not None, pins)
    reset_before = re.search(r"Last reset: (.*)", cmd("VERSION", 1.5))
    reset_before = reset_before.group(1).strip() if reset_before else None
    cmd("RUN", 1.5)

    # ---- the sim -----------------------------------------------------------------
    m = serial.Serial(mega_port, 115200, timeout=0.1)
    t, boot = time.time(), b""
    while time.time() - t < 6:
        boot += m.read(8192)
    print(boot.decode(errors="replace").strip().splitlines()[:2])

    def mega(c, wait):
        m.reset_input_buffer()
        m.write((c + "\n").encode())
        t, out = time.time(), b""
        while time.time() - t < wait:
            out += m.read(8192)
        return out.decode(errors="replace")

    def sim_rate(seconds):
        """Requests (or retransmissions) per second the sim sees over a window."""
        mega("z", 0.3)
        time.sleep(seconds)
        r = mega("r", 0.6)
        mm = re.search(r"req=(\d+) \(([0-9.]+)/s\)", r)
        return (float(mm.group(2)) if mm else None), r.strip()

    def teensy_values(name, seconds=3):
        s.reset_input_buffer()
        t, vals = time.time(), []
        while time.time() - t < seconds:
            for l in s.read(8192).decode(errors="replace").splitlines():
                mm = re.match(r"^([A-Za-z][A-Za-z0-9_]*),([-0-9.naNA]+),", l.strip())
                if mm and mm.group(1).upper() == name:
                    vals.append(mm.group(2))
        return vals

    if MODE == "abort":
        time.sleep(2)
        rate, r = sim_rate(8)
        print(f"     answering: {r}")
        check(f"answering: the sim sees polls ({rate}/s)", bool(rate), r)
        rpm = teensy_values("RPM")
        check("RPM reads from the sim over CAN2", any(v.lower() != "nan" for v in rpm), repr(rpm[-5:]))
        print(mega("l", 0.5).strip())              # listen-only: nobody ACKs
        time.sleep(3)
        rate, r = sim_rate(10)
        print(f"     no ACK: {r}")
        check(f"no ACK: retransmissions bounded ({rate}/s; unaborted is ~3,600/s, #522 bus 0 ~183/s)",
              rate is not None and rate < 500, r)
        print(mega("l", 0.5).strip())              # ACKing again
        time.sleep(3)
        rate, r = sim_rate(6)
        print(f"     recovered: {r}")
        check(f"recovered: polls again ({rate}/s)", bool(rate), r)
        rpm = teensy_values("RPM")
        check("recovered: RPM reads again", any(v.lower() != "nan" for v in rpm), repr(rpm[-5:]))
    else:
        r = mega("s", 0.5)
        check("sim silenced (only preOBD answers on 0x7E8)", "SILENT" in r or "silent" in r, r)
        queries = ["q 00", "q 05", "q 6F", "p 00", "p 05"]
        for q in queries:
            got, sent_fail, sample = 0, 0, ""
            for _ in range(REPS):
                r = mega(q, 0.35)
                if "SEND FAILED" in r:
                    sent_fail += 1
                ans = [l for l in r.splitlines() if "answer 0x7E8" in l and " 41 " in l]
                if ans:
                    got += 1
                    sample = ans[0].strip()
            check(f"{q}: {got}/{REPS} answered", got == REPS,
                  f"send failures {sent_fail}; last reply: {r.strip()}")
            if sample:
                print(f"     {sample}", flush=True)

    print(mega("r", 0.5).strip())
    m.close()                          # resets the sim back to answering
    cmd("CONFIG", 1.5)
    print(cmd("BUS CAN", 1.5))
    v = re.search(r"Last reset: (.*)", cmd("VERSION", 1.5))
    check(f"no reset during the test (Last reset: {reset_before})",
          v is not None and v.group(1).strip() == reset_before, v.group(0) if v else "no VERSION")
finally:
    # ---- restore ---------------------------------------------------------------
    cmd("CONFIG", 1.5)
    rep = import_doc(saved)
    check("snapshot re-imported", "pplied" in rep, rep)
    if "pplied" not in rep:
        print(f"RESTORE FAILED: the stored config is the staged one (Serial1 unframed). "
              f"JSON IMPORT {snap}, then SAVE.", flush=True)
        sys.exit(1)
    cmd("SAVE", 2)
    reboot()
    cmd("CONFIG", 1.5)
    # An import never moves the console (#561), and SERIAL1 is refused while
    # Serial1 is still disabled in RAM: set it once the reboot brought it up.
    sec = saved.get("system", {}).get("console", {}).get("secondary", "NONE")
    if comparable(dump()).get("system", {}).get("console", {}).get("secondary") != sec:
        print(cmd(f"CONSOLE TRANSPORT {sec} SECONDARY", 1.5))
        cmd("SAVE", 2)
    check("the saved config matches the snapshot after a reboot",
          comparable(dump()) == comparable(saved))
    cmd("RUN", 1.5)

print(f"\n{sum(ok for _, ok in results)}/{len(results)} passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
