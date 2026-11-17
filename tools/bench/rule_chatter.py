#!/usr/bin/env python3
"""#668 bench: rules must not chatter when a reading sits at a threshold.

    python3 tools/bench/rule_chatter.py /dev/cu.usbmodemXXXX [--input A1] [--secs 30]
        [--relay 20] [--sense 21] [--debounce-ms 500] [--slack-ms 200]
        [--limit <v>]            (default: the median of 10 s of readings)

Wiring: an analog input whose reading is NOISY at the ADC (a thermistor, or a
potentiometer whose noise reaches the scaled value; --input names it, default A1).
Its value is only read, never reconfigured. The limit is the median of the
reading, so the noise straddles it. A relay Action goes on --relay (default 20),
read back on --sense (default 21): pins 20 and 21 JUMPERED together (Serial5's
pins, so Serial5 must be disabled; neither may be listed by SYSTEM PINS).
If the reading never crosses the limit the run is INCONCLUSIVE (exit 2): nudge the
source, or pass --limit.

Two checks, each ~--secs seconds in RUN (rules evaluate in RUN):
  1. ACTIVE chatter: RULE ADD <input> ABOVE <limit> HYST 0 <severity NONE>,
     linked to a RELAY action. Polls RULE LIST, ACTION LIST and INFO <sense>;
     counts READY<->ACTIVE flips, relay ON/OFF flips, and the shortest dwell.
  2. Warning-tier chatter: an ALARM rule with its limit well above the median
     (so it never asserts) and WARN <limit>, HYST 0. Polls ALARM STATUS
     ("System severity:") and RULE LIST; counts NORMAL<->WARNING flips.
     (Rules #0/#1 etc. already on the unit stay READY; they are not touched.)

PASS = no chatter: every state change is at least --debounce-ms (default 500, the
RULE ADD default; the debounce can't be set from the CLI) after the previous one,
less --slack-ms for the polling jitter (--set-debounce <ms> sets the rules'
debounce through RULE ADD ... DEBOUNCE <ms>). Fewer than 2 changes despite >= 10
threshold crossings also PASSES (the debounce rode them all out). On #668's unfixed firmware the expected
result is FAIL on both checks; after the fix, PASS.

Never sends SAVE. It snapshots SYSTEM DUMP JSON to ~/.preobd/ first, deletes only
the rules and actions it added, and checks the dump is identical afterwards.
"""
import copy, json, os, re, statistics, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]


def opt(name, default, conv=str):
    return conv(args[args.index(name) + 1]) if name in args else default


INPUT = opt("--input", "A1")
SECS = opt("--secs", 30.0, float)
RELAY = opt("--relay", 20, int)
SENSE = opt("--sense", 21, int)
DEBOUNCE = opt("--debounce-ms", 500.0, float)
SLACK = opt("--slack-ms", 200.0, float)
LIMIT = opt("--limit", None, float)
# --set-debounce <ms>: give the rules that debounce through the CLI (RULE ADD ...
# DEBOUNCE <ms>, #668) and expect it; without it the rules keep the application default.
SET_DEB = opt("--set-debounce", None, int)
if SET_DEB is not None:
    DEBOUNCE = float(SET_DEB)
DEB_ARG = f" DEBOUNCE {SET_DEB}" if SET_DEB is not None else ""
# Check 1 runs once per percentile of the sampled reading. 50 = the median: the
# noise straddles the limit, the ASSERT debounce keeps resetting and the rule may
# never go ACTIVE. 25 = mostly above the limit with brief dips, which exercises
# the clear side (#668). The warn check always uses the median (or --limit).
PCTS = [float(x) for x in opt("--pcts", "50,25").split(",")]

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.02)
time.sleep(0.3)
results = []
added_rules, added_actions, sense_added = [], [], []


def read_for(seconds, until=None, keep_csv=False):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        out += s.read(8192)
        if until and until in out:
            break
    lines = out.decode(errors="replace").splitlines()
    if keep_csv:
        return "\n".join(lines)
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


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if detail:
        print("     " + detail.replace("\n", "\n     "), flush=True)


def poll_rule(rid):
    text = cmd("RULE LIST", 0.6, b"System severity")
    for line in text.splitlines():
        if re.match(rf"\s*#{rid}\s", line):
            st = next((x for x in ("ACTIVE", "READY", "WARMUP", "INIT", "DISABLED")
                       if re.search(rf"\b{x}\b", line)), "?")
            m = re.search(r"val=([-0-9.]+)", line)
            return st, float(m.group(1)) if m else None
    return "?", None


def poll_severity():
    text = cmd("ALARM STATUS", 0.5, b"Silence policy")
    m = re.search(r"System severity:\s*(\w+)", text)
    return m.group(1) if m else None


def poll_relay(aid):
    text = cmd("ACTION LIST", 0.5, b"preOBD>")
    for line in text.splitlines():
        if re.search(rf"#{aid}\b", line):
            return re.search(r"-> ON\b", line) is not None
    return None


def poll_sense():
    text = cmd(f"INFO {SENSE}", 0.5, b"To see its routes")
    m = re.search(r"Current Value:\s*([-0-9.]+)", text)
    return float(m.group(1)) if m else None


def short_name():
    text = cmd(f"INFO {INPUT}", 1.0, b"To see its routes")
    m = re.search(r"Short Name:\s*'([^']*)'", text)
    return m.group(1).strip() if m else None


def sample_reading(name, secs):
    s.reset_input_buffer()
    txt = read_for(secs, keep_csv=True)
    return [float(x) for x in re.findall(rf"^{re.escape(name)},([-0-9.]+),", txt, re.M)]


def flips(series):
    """series [(t, state)] -> (changes [(t, old, new)], shortest dwell s per state)."""
    ch, dwell = [], {}
    last_t, last = None, None
    for t, st in series:
        if st is None or st == "?":
            continue
        if last is None:
            last_t, last = t, st
        elif st != last:
            ch.append((t, last, st))
            if len(ch) > 1:                 # the first dwell began before the capture
                dwell[last] = min(dwell.get(last, 1e9), t - last_t)
            last_t, last = t, st
    return ch, dwell


def verdict(label, series):
    ch, dwell = flips(series)
    states = sorted({st for _, st in series if st not in (None, "?")}, key=str)
    print(f"--- {label}: {len(series)} polls over {SECS:.0f} s, {len(ch)} changes, "
          f"states seen {states}", flush=True)
    for k, v in sorted(dwell.items(), key=lambda kv: str(kv[0])):
        print(f"     shortest dwell in {k}: {v * 1000:.0f} ms", flush=True)
    if len(ch) < 2:
        return None, ch, dwell
    worst = min(dwell.values()) if dwell else 1e9
    # gaps between consecutive changes (covers the first and last dwell as well)
    gaps = [b[0] - a[0] for a, b in zip(ch, ch[1:])]
    worst = min([worst] + gaps)
    return worst * 1000 >= DEBOUNCE - SLACK, ch, dwell


def rule_id(reply):
    m = re.search(r"Rule #(\d+) added", reply)
    return int(m.group(1)) if m else None


def cleanup():
    cmd("CONFIG", 1.5)
    if sense_added:
        cmd(f"CLEAR {SENSE}")
        sense_added.clear()
    for a in added_actions:
        cmd(f"ACTION DEL {a}")
    for r in added_rules:
        cmd(f"RULE DEL {r}")
    added_actions.clear(); added_rules.clear()


# ---- baseline ---------------------------------------------------------------
print(cmd("VERSION", 1.0).strip().splitlines()[2:4], flush=True)
mode_text = cmd("SYSTEM STATUS", 1.0)
cmd("CONFIG", 1.5)
saved = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/rule-chatter-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(saved, f, indent=1)
print(f"config snapshot: {snap}", flush=True)
sym = cmd("SYSTEM PINS", 1.5)
for p in (RELAY, SENSE):
    if re.search(rf"Pin\s+{p}:", sym):
        sys.exit(f"pin {p} is not free:\n{sym}")
cmd("RUN", 1.5)

try:
    name = short_name()
    if not name:
        sys.exit(f"no input on {INPUT}")
    vals = sample_reading(name, 10)
    if len(vals) < 5:
        sys.exit(f"too few {name} samples ({len(vals)}) in 10 s")
    med = statistics.median(vals)
    print(f"{INPUT} ({name}): n={len(vals)} median={med} min={min(vals)} max={max(vals)} "
          f"spread={max(vals) - min(vals):.3f}", flush=True)
    limit = LIMIT if LIMIT is not None else med
    print(f"limit = {limit}", flush=True)

    # ---- 1: ACTIVE chatter --------------------------------------------------
    qs = statistics.quantiles(vals, n=100, method="inclusive")
    for pct in PCTS:
        lim1 = LIMIT if LIMIT is not None else (limit if pct == 50 else qs[int(pct) - 1])
        print(f"=== check 1, limit {lim1} (p{pct:g}): ABOVE, HYST 0", flush=True)
        cmd("CONFIG", 1.5)
        if not sense_added:
            r = cmd(f"SET {SENSE} COOLANT_LEVEL FLOAT_SWITCH") + cmd(f"SET {SENSE} ALARM OFF")
            sense_added.append(SENSE)
            if "ERROR" in r:
                sys.exit(r)
        r = cmd(f"RULE ADD {INPUT} ABOVE {lim1} HYST 0 NONE{DEB_ARG}")
        rid = rule_id(r)
        if rid is None:
            sys.exit(f"RULE ADD failed:\n{r}")
        added_rules.append(rid)
        before = {a["id"] for a in dump()["actions"]}
        r = cmd(f"ACTION ADD RELAY {RELAY}")
        aids = [a["id"] for a in dump()["actions"] if a["id"] not in before]
        if len(aids) != 1:
            sys.exit(f"ACTION ADD failed:\n{r}")
        aid = aids[0]
        added_actions.append(aid)
        r += cmd(f"ACTION {aid} RULE {rid}")
        if "ERROR" in r:
            sys.exit(r)
        cmd("RUN", 1.5)
        time.sleep(1.5)                   # past the rule's warmup
        rs, rl, sn, vs = [], [], [], []
        t0 = time.time()
        while time.time() - t0 < SECS:
            st, v = poll_rule(rid)
            rs.append((time.time() - t0, st)); vs.append(v)
            rl.append((time.time() - t0, poll_relay(aid)))
            sn.append((time.time() - t0, poll_sense()))
        above = sum(1 for v in vs if v is not None and v > lim1)
        below = sum(1 for v in vs if v is not None and v <= lim1)
        print(f"     polled readings: {above} above, {below} at/below {lim1}; "
              f"{len(rs) / SECS:.1f} polls/s", flush=True)
        ok_r, _, _ = verdict("rule ACTIVE<->READY", rs)
        ok_a, _, _ = verdict("relay ON/OFF (ACTION LIST)", rl)
        ok_p, _, _ = verdict("relay pin readback", [(t, None if v is None else str(v)) for t, v in sn])
        cross = sum(1 for a, b in zip(vs, vs[1:])
                    if a is not None and b is not None and (a > lim1) != (b > lim1))
        print(f"     polled readings crossed the limit {cross} times", flush=True)
        if ok_r is None and cross >= 10:
            # Fewer than 2 state changes although the reading crossed the limit many
            # times: the debounce rode out every crossing (#668: no chatter at all).
            check(f"1[p{pct:g}]. debounce rode out {cross} crossings (<2 rule changes)",
                  ok_a in (None, True) and ok_p in (None, True))
        elif ok_r is None:
            check(f"1[p{pct:g}]. rule/relay do not chatter at the threshold", False,
                  "INCONCLUSIVE: fewer than 2 state changes (reading never crossed, or never held past the assert debounce)")
        else:
            check(f"1[p{pct:g}]. rule ACTIVE<->READY changes are >= debounce apart", ok_r)
            check(f"1[p{pct:g}]. relay ON/OFF changes are >= debounce apart", bool(ok_a))
            check(f"1[p{pct:g}]. relay pin readback changes are >= debounce apart", bool(ok_p))
        cleanup()

    # ---- 2: warning-tier chatter -------------------------------------------
    cmd("CONFIG", 1.5)
    r = cmd(f"RULE ADD {INPUT} ABOVE {limit + 20} HYST 0 WARN {limit} ALARM{DEB_ARG}")
    rid2 = rule_id(r)
    if rid2 is None:
        sys.exit(f"RULE ADD (warn) failed:\n{r}")
    added_rules.append(rid2)
    cmd("RUN", 1.5)
    time.sleep(1.5)
    sev, rs2, vs2 = [], [], []
    t0 = time.time()
    while time.time() - t0 < SECS:
        sev.append((time.time() - t0, poll_severity()))
        st, v = poll_rule(rid2)
        rs2.append((time.time() - t0, st)); vs2.append(v)
    above = sum(1 for v in vs2 if v is not None and v > limit)
    below = sum(1 for v in vs2 if v is not None and v <= limit)
    print(f"     polled readings: {above} above, {below} at/below the warn edge; "
          f"rate {len(sev) / SECS:.1f} polls/s", flush=True)
    ok_s, ch_s, _ = verdict("system severity (ALARM STATUS)", sev)
    print("     rule state seen: "
          f"{sorted({st for _, st in rs2})}", flush=True)
    cross2 = sum(1 for a, b in zip(vs2, vs2[1:])
                 if a is not None and b is not None and (a > limit) != (b > limit))
    print(f"     polled readings crossed the warn edge {cross2} times", flush=True)
    if ok_s is None and above and below and cross2 >= 10:
        check(f"2. debounce rode out {cross2} warn-edge crossings (<2 severity changes)", True)
    elif ok_s is None or above == 0 or below == 0:
        check("2. warning tier does not chatter", False,
              "INCONCLUSIVE: the reading never crossed the warn edge (or <2 changes)")
        inconclusive = True
    else:
        check("2. NORMAL<->WARNING changes are >= debounce apart", ok_s)
    cleanup()
finally:
    cleanup()
    cmd("CONFIG", 1.5)
    same = comparable(dump()) == comparable(saved)
    check("restore: dump identical to the snapshot", same,
          "" if same else f"RESTORE FAILED: remove rules {added_rules} actions {added_actions}; snapshot {snap}")
    cmd("RUN", 1.5)

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed", flush=True)
sys.exit(1 if bad else 0)
