#!/usr/bin/env python3
"""Self-test for tools/check_layering.py: each case builds a tiny src/ tree and
an exceptions file in a temp dir and checks what the checker says. Run by the
pre-commit hook and CI. Exit 1 on any failure."""

import contextlib
import io
import os
import sys
import tempfile
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_layering  # noqa: E402

FAILS = []
_SAVED = (check_layering.REPO_ROOT, check_layering.SRC_ROOT)


def run(files, exceptions):
    with tempfile.TemporaryDirectory() as root:
        for rel, body in files.items():
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(textwrap.dedent(body))
        os.makedirs(os.path.join(root, "tools"), exist_ok=True)
        with open(os.path.join(root, "tools", "layering_exceptions.txt"), "w") as f:
            f.write(textwrap.dedent(exceptions))
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                rc = check_layering.main(["--root", root])
        finally:
            check_layering.REPO_ROOT, check_layering.SRC_ROOT = _SAVED
        return rc, out.getvalue()


def expect(name, result, want_rc, *needles, absent=()):
    rc, out = result
    ok = rc == want_rc and all(n in out for n in needles) and not any(n in out for n in absent)
    if not ok:
        FAILS.append(name)
        print(f"FAIL {name}: rc={rc} (want {want_rc}); "
              f"missing {[n for n in needles if n not in out]}; "
              f"unexpected {[n for n in absent if n in out]}\n{out}")


def expect_eq(name, got, want):
    if got != want:
        FAILS.append(name)
        print(f"FAIL {name}: got {got!r}, want {want!r}")


# ---- pair_direction -------------------------------------------------------
d = check_layering.pair_direction
expect_eq("sensors -> inputs is down", d("sensors", "inputs"), "down")
expect_eq("outputs -> transports is down", d("outputs", "transports"), "down")
expect_eq("buses -> hal is down", d("buses", "hal"), "down")
expect_eq("inputs -> sensors is up", d("inputs", "sensors"), "UP")
expect_eq("outputs -> rules is up", d("outputs", "rules"), "UP")
expect_eq("transports -> outputs is up", d("transports", "outputs"), "UP")
expect_eq("anything -> core is down", d("outputs", "core"), "down")
expect_eq("core -> hal is down", d("core", "hal"), "down")
expect_eq("core -> outputs is up", d("core", "outputs"), "UP")
expect_eq("console -> core is down", d("console", "core"), "down")
expect_eq("config -> core is down", d("config", "core"), "down")
expect_eq("(root) -> core is down", d("(root)", "core"), "down")
expect_eq("console is beside", d("console", "outputs"), "beside")
expect_eq("-> config is beside", d("sensors", "config"), "beside")
expect_eq("(root) is beside", d("(root)", "buses"), "beside")
expect_eq("same folder", d("rules", "rules"), "same")

# ---- the exceptions ratchet still holds ------------------------------------
HDR = '#pragma once\n'
TREE = {
    "src/rules/r.h": HDR,
    "src/inputs/i.h": '#include "../rules/r.h"\n',
}
expect("a listed pair passes", run(TREE, "inputs -> rules\n"), 0, "OK:")
expect("an unlisted pair fails", run(TREE, "\n"), 1, "NOT IN EXCEPTIONS FILE", "inputs -> rules")
expect("an upward listed pair is labelled, not failed",
       run({"src/rules/r.h": '#include "../inputs/i.h"\n', "src/inputs/i.h": HDR}, "rules -> inputs\n"),
       0, "rules -> inputs  [UP]", "point UP the layer order")
expect("a downward pair is labelled down", run(TREE, "inputs -> rules\n"), 0, "inputs -> rules  [down]")

# ---- section 14.10: formats may not include hal/ or transports/ -----------
FMT = {
    "src/outputs/formats/f.cpp": '#include "../../transports/t.h"\n#include "../../hal/h.h"\n',
    "src/transports/t.h": HDR,
    "src/hal/h.h": HDR,
}
PAIRS = "outputs -> transports\noutputs -> hal\n"
expect("a format including transports/ and hal/ fails even when the pairs are listed",
       run(FMT, PAIRS), 1, "outputs/formats/f.cpp -> transports", "outputs/formats/f.cpp -> hal", "14.10")
expect("listed FORMATS lines let those files through",
       run(FMT, PAIRS + "FORMATS outputs/formats/f.cpp -> transports\nFORMATS outputs/formats/f.cpp -> hal\n"),
       0, "OK:", absent=("FAIL",))
expect("one FORMATS line covers only its own layer",
       run(FMT, PAIRS + "FORMATS outputs/formats/f.cpp -> transports\n"), 1, "outputs/formats/f.cpp -> hal",
       absent=("f.cpp -> transports  (",))
expect("a stale FORMATS line is reported so it can go",
       run({"src/outputs/formats/f.cpp": HDR}, "FORMATS outputs/formats/f.cpp -> hal\n"),
       0, "no longer match any site", "FORMATS outputs/formats/f.cpp -> hal")
expect("the rule is for formats/ only: table/ may include hal/",
       run({"src/outputs/table/o.cpp": '#include "../../hal/h.h"\n', "src/hal/h.h": HDR}, "outputs -> hal\n"),
       0, "OK:", absent=("FAIL",))
expect("an extern into transports/ from a format counts too",
       run({"src/outputs/formats/f.cpp": "extern void sendFrame(int);\n",
            "src/transports/t.cpp": "void sendFrame(int x) {}\n"}, "outputs -> transports\n"),
       1, "outputs/formats/f.cpp -> transports")

# ---- src/-rooted includes (PlatformIO puts src/ on the include path) -------
ROOTED = '#include "transports/t.h"\n'
ANGLE = '#include <transports/t.h>\n'
for label, inc in (("quoted src-rooted", ROOTED), ("angle-bracket", ANGLE)):
    tree = {"src/inputs/i.cpp": inc, "src/transports/t.h": HDR}
    expect(f"a {label} include is a pair edge that must be listed",
           run(tree, "\n"), 1, "NOT IN EXCEPTIONS FILE", "inputs -> transports")
    expect(f"a {label} include of a listed pair passes",
           run(tree, "inputs -> transports\n"), 0, "OK:", absent=("UNRESOLVED",))
    fmt = {"src/outputs/formats/f.cpp": inc, "src/transports/t.h": HDR}
    expect(f"a {label} include from a format fails the FORMATS rule",
           run(fmt, "outputs -> transports\n"), 1, "outputs/formats/f.cpp -> transports")
    expect(f"a {label} include is let through by its FORMATS line",
           run(fmt, "outputs -> transports\nFORMATS outputs/formats/f.cpp -> transports\n"), 0, "OK:")
expect("a system angle include is ignored",
       run({"src/inputs/i.cpp": "#include <stdint.h>\n#include <Arduino.h>\n"}, "\n"), 0, "OK:")

print(f"\ntest_check_layering.py: {len(FAILS)} failure(s)")
sys.exit(1 if FAILS else 0)
