#!/usr/bin/env python3
"""Self-test for tools/check_pins.py: each case builds a tiny src/ tree and a
sites file in a temp dir and checks what the guard says. Run by the
pre-commit hook and CI. Exit 1 on any failure."""

import contextlib
import io
import os
import sys
import tempfile
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_pins  # noqa: E402

FAILS = []


def run(files, sites):
    with tempfile.TemporaryDirectory() as root:
        for rel, body in files.items():
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(textwrap.dedent(body))
        os.makedirs(os.path.join(root, "tools"), exist_ok=True)
        sp = os.path.join(root, "tools", "pin_drive_sites.txt")
        with open(sp, "w") as f:
            f.write(textwrap.dedent(sites))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = check_pins.main(["--root", root])
        return rc, out.getvalue()


def expect(name, rc, out, want_rc, *needles):
    ok = rc == want_rc and all(n in out for n in needles)
    if not ok:
        FAILS.append(name)
        print(f"FAIL {name}: rc={rc} (want {want_rc}); missing {[n for n in needles if n not in out]}\n{out}")
    else:
        print(f"ok   {name}")


RELAY = """
    static void driveAction(int p, bool on) {
        digitalWrite(p, on ? HIGH : LOW);
    }
    void guarded(const R& r) {
        if (held(r)) digitalWrite(r.pin, HIGH);
    }
"""
RELAY_SITES = """
    src/r.cpp  driveAction  digitalWrite  p   # CLAIMED: test (§12)
    src/r.cpp  guarded  digitalWrite  r.pin   # CLAIMED: test (§12)
"""

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES)
expect("baseline passes", rc, out, 0, "OK:")

rc, out = run({"src/r.cpp": RELAY + "void x() { pinMode(7, OUTPUT); }\n"}, RELAY_SITES)
expect("unlisted pinMode fails", rc, out, 1, "NEW PIN SITE: src/r.cpp", "in x: pinMode(7")

moved = RELAY.replace("void guarded(const R& r) {\n        if (held(r)) digitalWrite(r.pin, HIGH);\n    }",
                      "void guarded(const R& r) { }\n    void unguarded(const R& r) {\n        digitalWrite(r.pin, HIGH);\n    }")
rc, out = run({"src/r.cpp": moved}, RELAY_SITES)
expect("same-key move to a new function fails", rc, out, 1, "in unguarded: digitalWrite(r.pin", "GONE:")

rc, out = run({"src/r.cpp": RELAY + "void allOn() { driveAction(9, true); }\n"}, RELAY_SITES)
expect("new wrapper caller fails", rc, out, 1, "in allOn: driveAction(9")

rc, out = run({"src/r.cpp": RELAY + "int rd() { return 0 && digitalRead(5); }\n"}, RELAY_SITES)
expect("a read after && is a site", rc, out, 1, "in rd: digitalRead(5")

rc, out = run({"src/r.cpp": RELAY + "void b() {\n#if X\n    pinMode(4, INPUT);\n#endif\n}\n"}, RELAY_SITES)
expect("a call right after #if is a site", rc, out, 1, "in b: pinMode(4")

rc, out = run({"src/r.cpp": RELAY + "#warning this doesn't run\nvoid h() { pinMode(3, OUTPUT); }\n"}, RELAY_SITES)
expect("a stray quote in #warning hides nothing", rc, out, 1, "in h: pinMode(3")

rc, out = run({"src/r.cpp": RELAY + "const long k = 1'000;\nvoid g() { pinMode(2, OUTPUT); }\n"}, RELAY_SITES)
expect("a digit separator hides nothing", rc, out, 1, "in g: pinMode(2")

rc, out = run({"src/r.cpp": RELAY + "Foo::Foo(int q) : a(q), b(0) { pinMode(PIN_X, OUTPUT); }\n"}, RELAY_SITES)
expect("a constructor with an init list is named", rc, out, 1, "in Foo::Foo: pinMode(PIN_X,OUTPUT")

rc, out = run({"src/r.cpp": RELAY + "namespace n { struct S { void m() { pinMode(PIN_Y, INPUT); } }; }\n"}, RELAY_SITES)
expect("namespace and struct braces are transparent", rc, out, 1, "in m: pinMode(PIN_Y,INPUT")

rc, out = run({"src/r.cpp": RELAY + "#define OFF(p) do { digitalWrite((p), LOW); } while (0)\n"}, RELAY_SITES)
expect("a macro body is keyed define:NAME", rc, out, 1, "in define:OFF: digitalWrite((p)")

rc, out = run({"src/r.cpp": RELAY + "void declOnly(int p);\nFLASHMEM void rulesActionsInit();\n"}, RELAY_SITES)
expect("declarations are not sites", rc, out, 0, "OK:")

rc, out = run({"src/r.cpp": RELAY + "void w() { registerPin_unchecked(1, 0, 0, 0); }\n"}, RELAY_SITES)
expect("_unchecked outside pin_owner hits the wall", rc, out, 1, "WALL: src/r.cpp")

rc, out = run({"src/buses/pinctrl/pin_owner.cpp": "void c() { registerPin_unchecked(1, 0, 0, 0); }\n",
               "src/r.cpp": RELAY}, RELAY_SITES)
expect("_unchecked inside pin_owner is allowed", rc, out, 0, "OK:")

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES.replace(
    "guarded  digitalWrite  r.pin   # CLAIMED: test (§12)",
    "guarded  digitalWrite  r.pin   # RATCHET TARGET: was CLAIMED before (#1)"))
expect("the tag is the comment's first word", rc, out, 0, "1 ratchet target(s)")

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES.replace("# CLAIMED: test (§12)\n    src/r.cpp  guarded",
                                                         "# note: CLAIMED (§12)\n    src/r.cpp  guarded"))
expect("a tag not at the start is refused", rc, out, 1, "comment must start with a tag")

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES.replace("(§12)\n    src/r.cpp  guarded", "\n    src/r.cpp  guarded"))
expect("a line with no citation is refused", rc, out, 1, "cites no issue")

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES + "    src/r.cpp  gone  pinMode  9   # CLAIMED: test (§12)\n")
expect("a listed site that is gone fails", rc, out, 1, "GONE:")

rc, out = run({"src/r.cpp": RELAY + "void setLevel(int pin) { digitalWrite(pin, LOW); }\n"}, RELAY_SITES +
              "    src/r.cpp  setLevel  digitalWrite  pin   # CLAIMED: test (§12)\n")
expect("a new wrapper that passes its parameter must join PIN_CALLS", rc, out, 1, "WRAPPER: src/r.cpp", "setLevel()")

rc, out = run({"src/r.cpp": RELAY + "#define REGX registerPin_unchecked\nvoid z() { REGX(1, 0, 0, 0); }\n"}, RELAY_SITES)
expect("a macro alias of _unchecked hits the wall", rc, out, 1, "WALL: src/r.cpp", "alias or pointer")

rc, out = run({"src/r.cpp": RELAY + "void y() { auto f = &digitalWrite; f(5, HIGH); }\n"}, RELAY_SITES)
expect("a pointer to a pin call hits the wall", rc, out, 1, "WALL: src/r.cpp", "mentions digitalWrite without calling it")

rc, out = run({"src/r.cpp": RELAY + "bool v() { return not digitalRead(6); }\nvoid u() { UNUSED digitalWrite(5, HIGH); }\n"}, RELAY_SITES)
expect("inside a function a call after a word is still a call", rc, out, 1, "in v: digitalRead(6", "in u: digitalWrite(5")

rc, out = run({"src/r.cpp": RELAY + "void t() { pinMode(PIN_T, OUTPUT); }\n"}, RELAY_SITES +
              "    src/r.cpp  t  pinMode  PIN_T,INPUT_PULLUP   # CLAIMED: test (§12)\n")
expect("a reader that becomes a driver changes its key", rc, out, 1, "pinMode(PIN_T,OUTPUT", "GONE:")

rc, out = run({"src/r.cpp": RELAY}, RELAY_SITES + "    src/r.cpp  define:NOTLISTED  digitalWrite  p   # MACRO: test (§12)\n")
expect("a MACRO line for a macro outside PIN_CALLS fails", rc, out, 1, "MACRO: define:NOTLISTED")

rc, out = run({"src/r.cpp": RELAY + "void s(bool e) { pinMode(PIN_S, e ? OUTPUT : INPUT); }\n"}, RELAY_SITES +
              "    src/r.cpp  s  pinMode  PIN_S,e?INPUT_PULLUP:INPUT   # CLAIMED: test (§12)\n")
expect("a mode change inside a ternary changes the key", rc, out, 1, "pinMode(PIN_S,e?OUTPUT:INPUT", "GONE:")

rc, out = run({"src/r.cpp": RELAY, "src/w.h": "inline void clearTxRequests(uint8_t cs) { digitalWrite(cs, LOW); }\n",
               "src/z.cpp": "void zap() { clearTxRequests(7); }\n"},
              RELAY_SITES + "    src/w.h  clearTxRequests  digitalWrite  cs   # CLAIMED: test (§12)\n")
expect("a wrapper called from another file is a site there", rc, out, 1, "src/z.cpp:1 in zap: clearTxRequests(7")

rc, out = run({"src/r.cpp": RELAY, "src/hal/platforms/can_flexcan.cpp": "void abortUnacked(int base) { digitalWrite(base, HIGH); }\n"}, RELAY_SITES)
expect("an excluded same-named function still gets the WRAPPER hint", rc, out, 1, "WRAPPER: src/hal/platforms/can_flexcan.cpp")

rc, out = run({"src/r.cpp": RELAY + "static bool bb = not digitalRead(5);\n"}, RELAY_SITES)
expect("a file-scope initializer after 'not' is a site", rc, out, 1, "digitalRead(5")

def run_args(files, sites, *extra):
    import tempfile as tf
    with tf.TemporaryDirectory() as root:
        for rel, body in files.items():
            path = os.path.join(root, rel); os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "w").write(textwrap.dedent(body))
        os.makedirs(os.path.join(root, "tools"), exist_ok=True)
        open(os.path.join(root, "tools", "pin_drive_sites.txt"), "w").write(textwrap.dedent(sites))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = check_pins.main(["--root", root, *extra])
        return rc, out.getvalue()

target_sites = RELAY_SITES.replace("guarded  digitalWrite  r.pin   # CLAIMED: test (§12)",
                                   "guarded  digitalWrite  r.pin   # RATCHET TARGET: test (#1)")
expect("ratchet targets may not rise above main", *run_args({"src/r.cpp": RELAY}, target_sites, "--max-targets", "0"), 1, "MORE TARGETS")
expect("ratchet targets at main's count pass", *run_args({"src/r.cpp": RELAY}, target_sites, "--max-targets", "1"), 0, "OK:")

expect("status groups open targets by reason", *run_args({"src/r.cpp": RELAY}, target_sites, "--status"), 0,
       "§12 conformance", "RATCHET TARGET 1", "test (#1)  [1 site(s)]", "src/r.cpp: guarded")
expect("status with nothing open says so", *run_args({"src/r.cpp": RELAY}, RELAY_SITES, "--status"), 0,
       "Nothing open here")
rc, out = run_args({"src/r.cpp": RELAY + "void extra(){ digitalWrite(9, 1); }\n"}, RELAY_SITES, "--status")
expect("status is refused when the check fails", rc, out, 1, "STATUS NOT PRINTED")

print(f"\ntest_check_pins.py: {len(FAILS)} failure(s)")
sys.exit(1 if FAILS else 0)
