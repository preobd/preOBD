#!/usr/bin/env python3
"""Self-test for tools/check_maps.py on temp trees. Run by the pre-commit
hook and CI. Exit 1 on any failure."""

import contextlib
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_maps  # noqa: E402

FAILS = []
README = "sensors/ inputs/\n"
MAP = ('<meta name="preobd:locks" content="§12">\n'
       '<meta name="preobd:artifact" content="https://claude.ai/artifact/x">\n'
       '<meta name="preobd:traced" content="abc1234">\n'
       '<meta name="preobd:describes" content="src/a.cpp">\n'
       '<meta name="preobd:digest" content="{digest}">\n'
       '<p>traced at <span data-preobd="traced">abc1234</span></p>\n')


def tree(root, with_maps=True, digest=None, dirs=("sensors", "inputs"), describes_exists=True,
         drop_meta=None, shown="abc1234"):
    for d in dirs:
        os.makedirs(os.path.join(root, "src", d), exist_ok=True)
    with open(os.path.join(root, "src", "README.md"), "w") as f:
        f.write(README)
    if describes_exists:
        with open(os.path.join(root, "src", "a.cpp"), "w") as f:
            f.write("int a;\n")
    if with_maps:
        os.makedirs(os.path.join(root, "architecture", "maps"))
        body = MAP.replace("{digest}", digest or "x")
        body = body.replace('<span data-preobd="traced">abc1234', f'<span data-preobd="traced">{shown}')
        if drop_meta:
            body = "\n".join(l for l in body.split("\n") if drop_meta not in l)
        with open(os.path.join(root, "architecture", "maps", "m.html"), "w") as f:
            f.write(body)


def run(root, *args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = check_maps.main(["--root", root, *args])
    return rc, out.getvalue()


def expect(name, rc, out, want_rc, *needles):
    if rc == want_rc and all(n in out for n in needles):
        print(f"ok   {name}")
    else:
        FAILS.append(name)
        print(f"FAIL {name}: rc={rc} (want {want_rc}); missing {[n for n in needles if n not in out]}\n{out}")


def fresh_digest(root):
    check_maps.ROOT = root
    return check_maps.digest(["src/a.cpp"])


with tempfile.TemporaryDirectory() as r:
    tree(r)
    d = fresh_digest(r)
with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d)
    expect("a fresh map passes strict", *run(r, "--strict"), 0, "0 stale")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest="0000")
    expect("a stale map warns", *run(r), 0, "WARNING:", "is stale")
    expect("a stale map fails strict", *run(r, "--strict"), 1, "ERROR:", "is stale")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d, describes_exists=False)
    expect("a described file that's missing fails", *run(r), 1, "describes files that don't exist")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d, drop_meta="preobd:artifact")
    expect("a missing meta tag fails", *run(r), 1, "missing preobd:artifact")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d, dirs=("sensors", "inputs", "newlayer"))
    expect("an unlisted src/ folder fails", *run(r), 1, "does not name src/newlayer/")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d, shown="fff0000")
    expect("a visible stamp that disagrees fails", *run(r), 1, "the page shows traced fff0000")
with tempfile.TemporaryDirectory() as r:
    tree(r, with_maps=False)
    expect("a tree without architecture/ checks the README only", *run(r), 0, "no architecture/maps/")
with tempfile.TemporaryDirectory() as r:
    tree(r, digest="0000")
    rc, out = run(r, "--stamp", os.path.join(r, "architecture", "maps", "m.html"))
    expect("stamp reports its visible stamps", rc, out, 0, "1 visible stamp(s)")
    expect("a stamped map passes strict", *run(r, "--strict"), 0, "0 stale")

with tempfile.TemporaryDirectory() as r:
    tree(r, digest=d)
    p = os.path.join(r, "architecture", "maps", "m.html")
    body = open(p).read().replace('<span data-preobd="traced">abc1234</span>', "abc1234")
    open(p, "w").write(body)
    expect("a page with no visible stamp fails", *run(r), 1, "shows no")

print(f"\ntest_check_maps.py: {len(FAILS)} failure(s)")
sys.exit(1 if FAILS else 0)
