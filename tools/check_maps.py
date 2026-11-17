#!/usr/bin/env python3
"""
check_maps.py -- keeps the architecture maps a system of record.

Each map under architecture/maps/ is an HTML page (also published as a
claude.ai artifact) that records how one locked part of the firmware works.
It carries, as <meta> tags:

  preobd:locks      the ARCHITECTURE.md section it records, e.g. "§12"
  preobd:artifact   the artifact URL it is published to
  preobd:traced     the main commit it was traced against (for people; not
                    checked). The page shows it in every
                    <span data-preobd="traced">…</span>, which --stamp
                    rewrites too.
  preobd:describes  space-separated repo paths the map was traced from
  preobd:digest     a digest of those files, written by --stamp

A map is STALE when the files it describes no longer match its digest: the
code moved under it. Re-trace the map (read the code, fix the page), then run

    python3 tools/check_maps.py --stamp architecture/maps/<map>.html

which rewrites preobd:digest and preobd:traced, and republish the artifact
from the file. Stamping without re-tracing defeats the point; review checks it.

It also checks that src/README.md names every top-level folder under src/,
since it is the text form of the System Map's layer list.

Exit status: 1 on a malformed map, a missing described file or an
unlisted src/ folder. A stale map is a warning (exit 0) unless --strict, so
work-in-progress commits are not blocked; CI runs --strict, so a PR with a
stale map gets a red check (the repo has no branch protection, so the check
informs the merge; review enforces it).
"""

import argparse
import glob
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = os.path.join(ROOT, "architecture", "maps")
REQUIRED = ("preobd:locks", "preobd:artifact", "preobd:traced",
            "preobd:describes", "preobd:digest")
META_RE = re.compile(r'<meta\s+name="(preobd:[a-z]+)"\s+content="([^"]*)"\s*/?>')


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def read_meta(text):
    return {m.group(1): m.group(2) for m in META_RE.finditer(text)}


def digest(paths):
    h = hashlib.sha256()
    for p in sorted(paths):
        h.update(p.encode() + b"\0")
        with open(os.path.join(ROOT, p), "rb") as f:
            h.update(f.read())
        h.update(b"\0")
    return h.hexdigest()[:16]


def traced_commit():
    """The main commit the branch was traced against: the merge base with
    origin/main (a squash merge discards the branch's own hashes), else
    HEAD."""
    for cmd in (["git", "merge-base", "HEAD", "origin/main"], ["git", "rev-parse", "HEAD"]):
        try:
            full = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                                  check=True).stdout.strip()
            return full[:7]
        except (OSError, subprocess.CalledProcessError):
            continue
    return "unknown"


def set_meta(text, name, value):
    pat = re.compile(r'(<meta\s+name="%s"\s+content=")[^"]*("\s*/?>)' % re.escape(name))
    if not pat.search(text):
        raise SystemExit(f"check_maps.py: no <meta name=\"{name}\"> to update")
    return pat.sub(lambda m: m.group(1) + value + m.group(2), text, count=1)


def stamp(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    meta = read_meta(text)
    paths = meta.get("preobd:describes", "").split()
    missing = [p for p in paths if not os.path.isfile(os.path.join(ROOT, p))]
    if not paths or missing:
        raise SystemExit(f"check_maps.py: {rel(path)}: describes nothing, or missing: {' '.join(missing)}")
    commit = traced_commit()
    text = set_meta(text, "preobd:digest", digest(paths))
    text = set_meta(text, "preobd:traced", commit)
    text, n = re.subn(r'(<span data-preobd="traced">)[^<]*(</span>)',
                      lambda m: m.group(1) + commit + m.group(2), text)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"stamped {rel(path)}: traced {commit}, {len(paths)} files, {n} visible stamp(s)")


def check_src_readme():
    readme = os.path.join(ROOT, "src", "README.md")
    with open(readme, encoding="utf-8") as f:
        text = f.read()
    dirs = sorted(d for d in os.listdir(os.path.join(ROOT, "src"))
                  if os.path.isdir(os.path.join(ROOT, "src", d)))
    return [d for d in dirs if not re.search(r"(^|[\s`])%s/" % re.escape(d), text, re.M)]


def main(argv=None):
    global ROOT, MAPS
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=ROOT, help="repo root (tests)")
    ap.add_argument("--strict", action="store_true", help="fail on a stale map (CI)")
    ap.add_argument("--stamp", metavar="MAP", help="re-stamp one map after re-tracing it")
    args = ap.parse_args(argv)
    ROOT = os.path.abspath(args.root)
    MAPS = os.path.join(ROOT, "architecture", "maps")

    if args.stamp:
        stamp(os.path.abspath(args.stamp))
        return 0

    errors, stale = [], []
    maps = sorted(glob.glob(os.path.join(MAPS, "*.html")))
    if not os.path.isdir(MAPS):
        # The public snapshot ships without architecture/ (CLAUDE.md release
        # process); only the src/README.md check applies there.
        print("check_maps.py: no architecture/maps/ in this tree; checking src/README.md only")
    elif not maps:
        errors.append("no maps under architecture/maps/")
    for path in maps:
        with open(path, encoding="utf-8") as f:
            meta = read_meta(f.read())
        absent = [k for k in REQUIRED if not meta.get(k)]
        if absent:
            errors.append(f"{rel(path)}: missing {' '.join(absent)}")
            continue
        paths = meta["preobd:describes"].split()
        missing = [p for p in paths if not os.path.isfile(os.path.join(ROOT, p))]
        if missing:
            errors.append(f"{rel(path)}: describes files that don't exist: {' '.join(missing)}")
            continue
        if digest(paths) != meta["preobd:digest"]:
            stale.append((path, meta, paths))
        with open(path, encoding="utf-8") as f:
            shown = set(re.findall(r'<span data-preobd="traced">([^<]*)</span>', f.read()))
        if not shown:
            errors.append(f'{rel(path)}: shows no <span data-preobd="traced"> stamp; a page of record must say what it was traced at')
        elif shown - {meta["preobd:traced"]}:
            errors.append(f"{rel(path)}: the page shows traced {', '.join(sorted(shown))} "
                          f"but its meta says {meta['preobd:traced']}; run --stamp")

    for d in check_src_readme():
        errors.append(f"src/README.md does not name src/{d}/ (it is the System Map's layer list)")

    for e in errors:
        print(f"ERROR: {e}")
    for path, meta, paths in stale:
        word = "ERROR" if args.strict else "WARNING"
        print(f"{word}: {rel(path)} (ARCHITECTURE.md {meta['preobd:locks']}) is stale: "
              f"the code it records changed since {meta['preobd:traced']}.")
        print(f"  It describes: {' '.join(paths)}")
        print(f"  Re-trace it, then: python3 tools/check_maps.py --stamp {rel(path)}")
        print(f"  and republish {meta['preobd:artifact']}")

    print(f"check_maps.py: {len(maps)} map(s), {len(stale)} stale")
    if errors or (stale and args.strict):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
