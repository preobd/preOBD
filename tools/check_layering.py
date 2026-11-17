#!/usr/bin/env python3
"""check_layering.py -- report cross-directory dependency edges under src/.

This is a *reporting* tool, per GitHub issue #401 slice 5. It does not invent
a layering; it measures the one that exists and fails only when an edge is
not already recorded in tools/layering_exceptions.txt.

It analyses two kinds of dependency, because #401 found that include-graph
analysis alone has a blind spot:

  (a) `#include "..."` edges -- resolved relative to the including file,
      mapped to top-level directories under src/.

  (b) `extern` declarations inside .cpp files -- these declare a dependency
      on a symbol defined elsewhere without leaving any trace in the
      include graph. src/core/system_mode.cpp is the canonical example: it
      includes nothing from outputs/, but declares
      `extern void showConfigModeMessage();` / `extern void clearLCD();`,
      both defined in src/outputs/display/display_grid.cpp. An include-only
      checker misses this edge entirely.

Two further rules come from src/README.md and ARCHITECTURE.md 14.10:

  * LAYER ORDER. The flow layers rank sensors > inputs > rules > outputs >
    transports > buses > hal (data flows down the list); core/ is under all
    of them; console/, config/ and (root) sit beside. Every measured pair is
    labelled "down", "UP" (against the flow), "beside" or "same" so the
    report says which listed exceptions point the wrong way. The label is
    information; the fail condition stays "pair not listed", so the check is
    still a ratchet.
  * FORMATS. Files under outputs/formats/ may not include (or extern into)
    hal/ or transports/ -- a format never starts hardware (14.10). Existing
    sites are listed one file per line in the exceptions file as
    `FORMATS <path under src/> -> <hal|transports>`, and the list may only
    shrink.

Usage:
    python3 tools/check_layering.py [--exceptions PATH] [--verbose] [--json]
                                    [--root DIR]

Exit status: 0 if every cross-directory edge found is listed in the
exceptions file (or is a same-component edge, which is never a violation);
1 if any edge is not listed, or if the core/ ratchet property is violated
by anything not itself listed as a named ratchet target.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.join(REPO_ROOT, "src")

SOURCE_EXTS = (".h", ".hpp", ".cpp")
ROOT_COMPONENT = "(root)"

# core/ is allowed to include only <Arduino.h> and hal/ -- see #401.
CORE_ALLOWED_COMPONENTS = {"core", "hal"}

# Layer order from src/README.md, top (upstream) to bottom. Dependencies
# should point down this list.
FLOW_LAYERS = ["sensors", "inputs", "rules", "outputs", "transports", "buses", "hal"]
# Beside the flow: may be used from, and may use, any layer.
BESIDE = {"console", "config", ROOT_COMPONENT}
# Under all: everything may use it; it may use only hal (CORE_ALLOWED_COMPONENTS).
UNDER = "core"

# ARCHITECTURE.md section 14.10: outputs/formats/ may not include hal/ or
# transports/ internals.
FORMATS_DIR = "outputs/formats"
FORMATS_FORBIDDEN = {"hal", "transports"}


def pair_direction(a, b):
    """Label a cross-component pair: 'down', 'UP', 'beside' or 'same'."""
    if a == b:
        return "same"
    if b == UNDER:
        return "down"  # everything, console/config/(root) included, may use core/
    if a in BESIDE or b in BESIDE:
        return "beside"
    if a == UNDER:
        # core/ may use only hal/; hal is the one downward target it has.
        return "down" if b in CORE_ALLOWED_COMPONENTS else "UP"
    if a in FLOW_LAYERS and b in FLOW_LAYERS:
        return "down" if FLOW_LAYERS.index(b) > FLOW_LAYERS.index(a) else "UP"
    return "beside"  # a folder the table doesn't know: src/README.md must name it


def is_formats_file(rel_path):
    return rel_path.replace(os.sep, "/").startswith("src/" + FORMATS_DIR + "/")


def find_formats_violations(cross_edges):
    """Edges from outputs/formats/ files into hal/ or transports/, as
    {(file under src/, target component): [edge sites]}."""
    out = defaultdict(list)
    for e in cross_edges:
        if is_formats_file(e["from_file"]) and e["to_component"] in FORMATS_FORBIDDEN:
            key = (e["from_file"].replace(os.sep, "/")[len("src/"):], e["to_component"])
            out[key].append(e)
    return out


def all_source_files():
    files = []
    for dirpath, dirnames, filenames in os.walk(SRC_ROOT):
        dirnames.sort()
        for fn in sorted(filenames):
            if fn.endswith(SOURCE_EXTS):
                files.append(os.path.join(dirpath, fn))
    return files


def component_of(path):
    """Top-level directory under src/, or ROOT_COMPONENT for files directly in src/."""
    rel = os.path.relpath(path, SRC_ROOT)
    parts = rel.split(os.sep)
    if len(parts) == 1:
        return ROOT_COMPONENT
    return parts[0]


def rel(path):
    return os.path.relpath(path, REPO_ROOT)


# ---------------------------------------------------------------------------
# (a) #include edges
# ---------------------------------------------------------------------------

# Quoted and angle-bracket includes both count: PlatformIO puts src/ on the
# include path, so `#include "transports/x.h"` and `#include <transports/x.h>`
# compile. A quoted include is tried relative to the including file first,
# then against src/; an angle include only against src/. Anything that
# resolves nowhere under src/ (system and library headers) is ignored.
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')


def find_include_edges(files):
    """Returns list of dicts: from_file, line, to_file, from_component, to_component."""
    edges = []
    unresolved = []
    for f in files:
        d = os.path.dirname(f)
        with open(f, "r", encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, start=1):
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                quoted = m.group(1) == '"'
                inc = m.group(2)
                target = os.path.normpath(os.path.join(d, inc)) if quoted else ""
                if not (target and os.path.isfile(target)):
                    alt = os.path.normpath(os.path.join(SRC_ROOT, inc))
                    if os.path.isfile(alt) and alt.startswith(SRC_ROOT):
                        target = alt
                if not (target and os.path.isfile(target)):
                    # Not resolvable relative to the including file (system/lib
                    # header, or a path resolved via a build -I flag). Only
                    # report as unresolved if it looks like it was meant to be
                    # a same-tree (src/) header, i.e. starts inside src/ or
                    # has no directory separator suggesting a foreign root.
                    if quoted and target.startswith(SRC_ROOT):
                        unresolved.append((rel(f), lineno, inc))
                    continue
                if not target.startswith(SRC_ROOT):
                    continue
                from_c = component_of(f)
                to_c = component_of(target)
                edges.append({
                    "kind": "include",
                    "from_file": rel(f),
                    "line": lineno,
                    "to_file": rel(target),
                    "from_component": from_c,
                    "to_component": to_c,
                })
    return edges, unresolved


# ---------------------------------------------------------------------------
# (b) extern edges
# ---------------------------------------------------------------------------

# Matches a single-line `extern <decl>;` at (possibly indented) start of
# line, inside a .cpp file. Excludes `extern "C"` linkage blocks.
EXTERN_RE = re.compile(r'^\s*extern\s+(?!"C")(.+);\s*$')

# A plausible *definition* site: a line starting at column 0 (i.e. not
# indented -- global scope, not inside a function body), naming the symbol,
# that is not itself an `extern` declaration and is not a comment.
def is_definition_candidate_line(line):
    if line != line.lstrip():
        return False  # indented -> local scope, not a global definition
    s = line.strip()
    if not s or s.startswith("//") or s.startswith("/*") or s.startswith("*"):
        return False
    if s.startswith("extern "):
        return False
    if s.startswith("#"):
        return False
    return True


def extract_symbol_name(decl):
    """From the text between 'extern' and the trailing ';', extract the
    declared symbol's identifier.

    Handles:
      void foo(...)          -> foo
      Type name[SIZE]        -> name
      Type name(args...)     -> name   (global object with ctor args)
      Type name              -> name
    """
    decl = decl.strip()
    # Drop a top-level initializer ('=' not part of ==, !=, <=, >=) so
    # `uint8_t numActiveInputs = 0;` doesn't yield the literal as the name.
    decl = re.split(r'(?<![=!<>])=(?!=)', decl, maxsplit=1)[0].strip()
    # Function/ctor-style: identifier immediately followed by '('
    m = re.search(r'(\w+)\s*\(', decl)
    if m:
        return m.group(1)
    # Array: identifier immediately followed by '['
    m = re.search(r'(\w+)\s*\[', decl)
    if m:
        return m.group(1)
    # Plain variable: last identifier token
    tokens = re.findall(r'\w+', decl)
    if tokens:
        return tokens[-1]
    return None


def build_definition_index(files):
    """symbol name -> list of (file, lineno) definition candidates."""
    index = defaultdict(list)
    for f in files:
        if not f.endswith(".cpp"):
            continue
        with open(f, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        for lineno, line in enumerate(lines, start=1):
            if not is_definition_candidate_line(line):
                continue
            # Identify a leading identifier used as the *name* being
            # (re)declared/defined at this global-scope line: same
            # heuristics as extract_symbol_name, applied to the whole line
            # up to '{' or ';' or '=' or end.
            stripped = line.strip()
            head = re.split(r'\{', stripped, maxsplit=1)[0]
            name = extract_symbol_name(head.rstrip(";").rstrip())
            if name:
                index[name].append((f, lineno))
    return index


def find_extern_edges(files, def_index):
    edges = []
    unresolved = []
    for f in files:
        if not f.endswith(".cpp"):
            continue
        with open(f, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        for lineno, line in enumerate(lines, start=1):
            m = EXTERN_RE.match(line)
            if not m:
                continue
            decl = m.group(1)
            name = extract_symbol_name(decl)
            if not name:
                unresolved.append((rel(f), lineno, line.strip(), "could not parse symbol name"))
                continue
            candidates = def_index.get(name, [])
            # Exclude the declaring file's own extern line from candidates
            # (it isn't a definition), and prefer .cpp definitions elsewhere.
            candidates = [c for c in candidates if not (c[0] == f and c[1] == lineno)]
            if not candidates:
                unresolved.append((rel(f), lineno, line.strip(), f"no definition found for '{name}'"))
                continue
            from_c = component_of(f)
            for def_file, def_lineno in candidates:
                to_c = component_of(def_file)
                edges.append({
                    "kind": "extern",
                    "from_file": rel(f),
                    "line": lineno,
                    "to_file": rel(def_file),
                    "to_line": def_lineno,
                    "symbol": name,
                    "from_component": from_c,
                    "to_component": to_c,
                })
    return edges, unresolved


# ---------------------------------------------------------------------------
# Exceptions file
# ---------------------------------------------------------------------------

def load_exceptions(path):
    """Returns set of (from_component, to_component) pairs."""
    pairs = set()
    if not os.path.isfile(path):
        return pairs
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            m = re.match(r'^(\S+)\s*->\s*(\S+)$', line)
            if not m:
                continue
            pairs.add((m.group(1), m.group(2)))
    return pairs


def load_formats_exceptions(path):
    """Returns set of (file under src/, target component) from
    `FORMATS <file> -> <hal|transports>` lines."""
    entries = set()
    if not os.path.isfile(path):
        return entries
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            m = re.match(r'^FORMATS\s+(\S+)\s*->\s*(\S+)$', line)
            if m:
                entries.add((m.group(1), m.group(2)))
    return entries


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

# Libraries that only ONE file under src/ may include. FlexCAN_T4 keeps its
# ISR target and ISR as `static`s in its header while its template code is
# merged across files by the linker, so a second includer can make begin()
# install an ISR that reads a null target: the board watchdog-resets (#555).
SINGLE_INCLUDER = {
    "FlexCAN_T4.h": "src/hal/platforms/can_flexcan.cpp",
}
ANGLE_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def find_single_includer_violations(files):
    out = []
    for path in files:
        r = rel(path)
        with open(path, encoding="utf-8", errors="replace") as fh:
            for ln, line in enumerate(fh, 1):
                m = ANGLE_INCLUDE_RE.match(line)
                if not m:
                    continue
                owner = SINGLE_INCLUDER.get(os.path.basename(m.group(1)))
                if owner and r != owner:
                    out.append((r, ln, m.group(1), owner))
    return out


def main(argv=None):
    global REPO_ROOT, SRC_ROOT
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exceptions", default=None)
    ap.add_argument("--root", default=None, help="repo root to scan (default: this checkout; the self-test passes a temp tree)")
    ap.add_argument("--verbose", action="store_true", help="print every edge site found, not just the summary")
    ap.add_argument("--json", action="store_true", help="print machine-readable JSON summary instead of text")
    ap.add_argument("--quiet", action="store_true",
                    help="print only a one-line summary plus any violations; "
                         "intended for the pre-commit hook, where dumping the whole "
                         "graph on every commit trains people to ignore it")
    args = ap.parse_args(argv)
    if args.root:
        REPO_ROOT = os.path.abspath(args.root)
        SRC_ROOT = os.path.join(REPO_ROOT, "src")
    if args.exceptions is None:
        args.exceptions = os.path.join(REPO_ROOT, "tools", "layering_exceptions.txt")

    files = all_source_files()
    include_edges, include_unresolved = find_include_edges(files)
    def_index = build_definition_index(files)
    extern_edges, extern_unresolved = find_extern_edges(files, def_index)

    all_edges = include_edges + extern_edges
    cross_edges = [e for e in all_edges if e["from_component"] != e["to_component"]]

    by_pair = defaultdict(list)
    for e in cross_edges:
        by_pair[(e["from_component"], e["to_component"])].append(e)

    exceptions = load_exceptions(args.exceptions)

    violations = {pair: sites for pair, sites in by_pair.items() if pair not in exceptions}

    formats_listed = load_formats_exceptions(args.exceptions)
    formats_all = find_formats_violations(cross_edges)
    formats_violations = {k: v for k, v in formats_all.items() if k not in formats_listed}
    formats_stale = sorted(formats_listed - set(formats_all))
    upward = sorted(pair for pair in by_pair if pair_direction(*pair) == "UP")

    # core/ ratchet: core/ may include only <Arduino.h> and hal/. Any
    # cross-component edge FROM core TO something other than hal is a
    # violation of the target property -- even if it is (for now) listed in
    # the general exceptions file as a named ratchet target, it should still
    # be surfaced distinctly so the ratchet's remaining work is visible.
    core_violations = {
        pair: sites for pair, sites in by_pair.items()
        if pair[0] == "core" and pair[1] not in CORE_ALLOWED_COMPONENTS
    }

    if args.json:
        import json as _json
        print(_json.dumps({
            "edges_by_pair": {f"{a} -> {b}": len(v) for (a, b), v in sorted(by_pair.items())},
            "violations": {f"{a} -> {b}": len(v) for (a, b), v in sorted(violations.items())},
            "upward_pairs": [f"{a} -> {b}" for a, b in upward],
            "formats_violations": {f"{f} -> {t}": len(v) for (f, t), v in sorted(formats_violations.items())},
            "core_ratchet_targets": {f"{a} -> {b}": len(v) for (a, b), v in sorted(core_violations.items())},
            "unresolved_includes": include_unresolved,
            "unresolved_externs": extern_unresolved,
        }, indent=2))
    else:
        if args.quiet:
            print(f"check_layering.py: {len(by_pair)} directory pairs, "
                  f"{len(cross_edges)} sites, {len(core_violations)} core/ ratchet target(s) remaining")
        else:
            print(f"check_layering.py: {len(files)} source files scanned under src/")
            print(f"  {len(include_edges)} include edges, {len(extern_edges)} extern edges")
            print(f"  {len(cross_edges)} cross-directory dependency sites across {len(by_pair)} directory pairs\n")

            print("Measured cross-directory edges (component pairs):")
            for (a, b), sites in sorted(by_pair.items()):
                flag = "" if (a, b) in exceptions else "  <-- NOT IN EXCEPTIONS FILE"
                kinds = sorted(set(s["kind"] for s in sites))
                print(f"  {a} -> {b}  [{pair_direction(a, b)}]  ({len(sites)} site(s), {'/'.join(kinds)}){flag}")
                if args.verbose:
                    for s in sites:
                        if s["kind"] == "include":
                            print(f"      {s['from_file']}:{s['line']} -> {s['to_file']}")
                        else:
                            print(f"      {s['from_file']}:{s['line']} extern '{s['symbol']}' -> {s['to_file']}:{s['to_line']}")
            print()

        if core_violations and not args.quiet:
            print("core/ ratchet targets (core/ should include only <Arduino.h> and hal/):")
            for (a, b), sites in sorted(core_violations.items()):
                for s in sites:
                    if s["kind"] == "include":
                        print(f"  {s['from_file']}:{s['line']} -> {s['to_file']}")
                    else:
                        print(f"  {s['from_file']}:{s['line']} extern '{s['symbol']}' -> {s['to_file']}:{s['to_line']}")
            print()

        if include_unresolved:
            print("UNRESOLVED includes (looked like src/-relative but target not found):")
            for f, ln, inc in include_unresolved:
                print(f"  {f}:{ln} -> \"{inc}\"")
            print()

        if extern_unresolved:
            print("UNRESOLVED externs (could not find a defining site):")
            for f, ln, text, why in extern_unresolved:
                print(f"  {f}:{ln}: {text}  ({why})")
            print()

        if violations:
            print(f"FAIL: {len(violations)} directory-pair edge(s) are not listed in {rel(args.exceptions)}:")
            for (a, b), sites in sorted(violations.items()):
                print(f"  {a} -> {b}  ({len(sites)} site(s))")
                for s in sites[:5]:
                    if s["kind"] == "include":
                        print(f"      {s['from_file']}:{s['line']} -> {s['to_file']}")
                    else:
                        print(f"      {s['from_file']}:{s['line']} extern '{s['symbol']}' -> {s['to_file']}:{s['to_line']}")
                if len(sites) > 5:
                    print(f"      ... and {len(sites) - 5} more")
        else:
            print("OK: every cross-directory edge is accounted for in the exceptions file.")

        if upward and not args.quiet:
            print(f"  ({len(upward)} listed pair(s) point UP the layer order: "
                  + ", ".join(f"{a} -> {b}" for a, b in upward) + ")")

        if formats_violations:
            print(f"FAIL: {len(formats_violations)} outputs/formats/ file(s) include hal/ or transports/ (ARCHITECTURE.md 14.10), not listed in {rel(args.exceptions)}:")
            for (f, t), sites in sorted(formats_violations.items()):
                print(f"  {f} -> {t}  ({len(sites)} site(s))")
                for s2 in sites[:5]:
                    print(f"      {s2['from_file']}:{s2['line']} -> {s2['to_file']}")
        if formats_stale:
            print("NOTE: these FORMATS lines no longer match any site; remove them (the list only shrinks):")
            for f, t in formats_stale:
                print(f"  FORMATS {f} -> {t}")

    single = find_single_includer_violations(files)
    if single and not args.json:
        print(f"FAIL: {len(single)} include(s) of a single-includer library:")
        for f, ln, inc, owner in single:
            print(f"  {f}:{ln} includes <{inc}>; only {owner} may (see SINGLE_INCLUDER)")

    return 1 if (violations or single or formats_violations) else 0


if __name__ == "__main__":
    sys.exit(main())
