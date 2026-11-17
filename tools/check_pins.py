#!/usr/bin/env python3
"""
check_pins.py -- the pin-ownership guard (ARCHITECTURE.md §12).

Two checks over every .cpp/.h/.ino/.c under src/, after comments and string
literals are blanked:

  1. WALL. registerPin_unchecked() / reservePins_unchecked() write the pin
     registry without asking anyone. §12.5: only pin_owner.cpp may call them.
     Their definitions in pin_registry.{h,cpp} are the only other mentions
     allowed. There are no exceptions.

  2. RATCHET. Every place that configures, drives or reads a pin directly
     must be listed in tools/pin_drive_sites.txt with the reason it may
     touch that pin. A new site fails the commit until someone has checked
     it touches only a pin its code claimed first and still holds (§12.5).
     A listed site that no longer exists also fails, so the file stays an
     exact record.

What counts as a site (PIN_CALLS below):
  - Arduino pin calls: pinMode, digitalWrite(Fast), digitalRead(Fast),
    analogWrite, analogRead, tone, noTone, attachInterrupt,
    detachInterrupt, ledcAttachPin.
  - preOBD wrappers that touch pins: platformNoTone, platformAnalogRead,
    readAnalogPin, the relay helpers in rules_manager.cpp, and a few
    file-local ones (PIN_CALLS). A caller of a wrapper is a site in its own right, so
    a new caller (say, an "all relays on" function) fails the commit.

What it cannot see (held by review, ARCHITECTURE.md §12.5): a pinMode mode
held in a variable, drives inside
libraries (a constructor or begin() that takes a pin), direct register
writes, function pointers, and actuation through state the loop later
drives (rulesActionCommand, rulesActionSetManual, rulesActionSetOverride).
The CLAIMED tag below is also held only by review: the checker can't verify
the claim the comment names.

A site is keyed by (file, enclosing function, call, first argument), with
whitespace removed. Moving a call to another function changes its key, so a
guarded drive can't be swapped for an unguarded one in a new function
without a new line here. A call inside a #define body is keyed to
"define:NAME". Overloads share a function name, and so share keys. Two
identical calls in one function are two identical lines.

Sites file format, one site per line:

    <path>  <function>  <call>  <first-arg>   # <TAG>: <why>, citing #N or §N

The TAG is the first word(s) of the comment, one of:
  CLAIMED        -- touches a pin its code claimed first and holds; the
                    comment names where the claim is made.
  RATCHET TARGET -- touches a pin it has not claimed, or after a refused
                    claim. Cites the issue that removes it.
  MACRO          -- the body of a helper macro; its callers are listed too.

Exit status: 0 when both checks pass, 1 otherwise.
"""

import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PIN_CALLS = (
    # Arduino
    "pinMode", "digitalWrite", "digitalWriteFast", "digitalRead",
    "digitalReadFast", "analogWrite", "analogRead", "tone", "noTone",
    "attachInterrupt", "detachInterrupt", "ledcAttachPin",
    # preOBD wrappers
    "platformNoTone", "platformAnalogRead",
    "driveAction", "writeLatch", "configureHeldPin", "releaseActionPin",
    "rulesSyncActionPins", "rulesActionsQuiesce", "rulesActionDelete",
    "rulesActionTestPulse",
    "readAnalogPin", "diagSamplePin", "writePWM", "clearTxRequests", "abortUnacked",
)
# A wrapper name is a site everywhere except in the files listed here, which
# hold an unrelated function of the same name (can_flexcan.cpp has its own
# abortUnacked, which takes a bus, not a pin). An exclusion list fails
# closed: a new same-named function elsewhere is still seen.
SCOPED_OUT = {
    "abortUnacked": {"src/hal/platforms/can_flexcan.cpp"},
}
# Relay helpers take an Action, a slot or an id, not a pin: their first
# argument isn't a pin, so passing a parameter on to one doesn't make the
# caller a pin wrapper. Their callers are still sites.
NOT_PIN_ARG = {"driveAction", "configureHeldPin", "releaseActionPin", "rulesSyncActionPins",
               "rulesActionsQuiesce", "rulesActionDelete", "rulesActionTestPulse"}
UNCHECKED_CALLS = ("registerPin_unchecked", "reservePins_unchecked")
UNCHECKED_CALLER = "src/buses/pinctrl/pin_owner.cpp"
UNCHECKED_DEFINERS = ("src/buses/pinctrl/pin_registry.h",
                      "src/buses/pinctrl/pin_registry.cpp")
TAGS = ("CLAIMED", "UNHELD", "RATCHET TARGET", "MACRO")

CALL_RE = re.compile(r"\b(" + "|".join(PIN_CALLS + UNCHECKED_CALLS) + r")\s*\(")
# Any mention of a name, call or not: a macro alias (#define X digitalWrite)
# or a function pointer (&registerPin_unchecked) is how a check by "name("
# would be dodged.
NAME_RE = re.compile(r"\b(" + "|".join(PIN_CALLS + UNCHECKED_CALLS) + r")\b(?!\s*\()")
CITE_RE = re.compile(r"(#\d+|§\d+)")
TAG_RE = re.compile(r"^#\s*(CLAIMED|UNHELD|RATCHET TARGET|MACRO)\s*:")
# A call preceded by one of these words is still a call; any other
# identifier right before the name makes it a declaration or definition
# ("void driveAction(", "FLASHMEM void rulesActionsInit(").
CALL_PRECEDERS = {"return", "else", "do", "case", "throw", "co_return", "co_await", "co_yield",
                  "not", "and", "or", "compl", "new", "delete"}
# Words that look like "name(" in a function header but aren't its name.
NOT_A_NAME = {"__attribute__", "alignas", "decltype", "sizeof", "noexcept",
              "FLASHMEM", "PROGMEM", "if", "for", "while", "switch", "return"}
TRANSPARENT_RE = re.compile(r"\b(namespace|struct|class|union|enum)\b[^;]*$|\bextern\s*$")


def strip_comments_and_strings(text):
    """Blank out comments and string/char literals, keeping newlines so line
    numbers stay right. A literal also ends at an unescaped newline, so a
    stray quote (#warning text, 1'000) can't swallow the code after it."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c in "\"'":
            # A digit separator (1'000) is not a literal.
            if c == "'" and i > 0 and text[i - 1].isalnum() and nxt.isalnum():
                out.append(" ")
                i += 1
                continue
            quote = c
            out.append(" ")
            i += 1
            while i < n and text[i] != quote and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] != "\n":
                    out.append(" ")
                    i += 1
                out.append(" ")
                i += 1
            if i < n and text[i] == quote:
                out.append(" ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def directive_spans(text):
    """[(start, end, define_name_or_None)] for each preprocessor directive,
    continuation lines included."""
    spans = []
    for m in re.finditer(r"(?m)^[ \t]*#", text):
        start = m.start()
        end = start
        while True:
            nl = text.find("\n", end)
            if nl == -1:
                end = len(text)
                break
            if text[nl - 1:nl] == "\\":
                end = nl + 1
                continue
            end = nl
            break
        d = re.match(r"[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)", text[start:end])
        spans.append((start, end, d.group(1) if d else None))
    return spans


def blank_spans(text, spans):
    chars = list(text)
    for s, e, _ in spans:
        for k in range(s, e):
            if chars[k] != "\n":
                chars[k] = " "
    return "".join(chars)


def header_name(header):
    """The function name in a header like 'static FLASHMEM bool foo(int x)
    const' or 'Foo::Foo(int x) : a(x)'. None if it isn't a function."""
    depth = 0
    for m in re.finditer(r"([A-Za-z_][\w:]*)\s*\(|[()]", header):
        tok = m.group(0)
        if tok == "(":
            depth += 1
            continue
        if tok == ")":
            depth -= 1
            continue
        if depth == 0:
            name = m.group(1)
            if name.split("::")[-1] not in NOT_A_NAME:
                return name
        depth += 1  # the '(' that ended this match
    return None


def header_params(header):
    """Parameter names of a function header: the last identifier of each
    top-level piece of its first parenthesised list."""
    m = re.search(r"\(", header)
    if not m:
        return set()
    depth, i, start = 0, m.start(), m.start() + 1
    while i < len(header):
        if header[i] == "(":
            depth += 1
        elif header[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    names = set()
    for piece in header[start:i].split(","):
        piece = piece.split("=")[0]
        ids = re.findall(r"[A-Za-z_]\w*", piece)
        if ids:
            names.add(ids[-1])
    return names


def function_spans(code):
    """[(open, close, name)] for every function body in directive-free,
    comment-free code. namespace/struct/class/enum/extern "C" braces are
    transparent; blocks nested in a function belong to that function."""
    spans = []
    stack = []  # entries: ("fn", name, pos) | ("t", None, pos) | ("b", None, pos)
    last_boundary = 0
    for i, c in enumerate(code):
        if c == "{":
            in_fn = any(kind == "fn" for kind, _, _ in stack)
            header = code[last_boundary:i]
            if in_fn:
                stack.append(("b", None, i))
            elif TRANSPARENT_RE.search(header):
                stack.append(("t", None, i))
            else:
                name = header_name(header) if "(" in header else None
                if name:
                    params = header_params(header[header.find(name.split("::")[-1]):])
                    stack.append(("fn", (name, params), i))
                else:
                    stack.append(("b", None, i))
            last_boundary = i + 1
        elif c == "}":
            if stack:
                kind, name, pos = stack.pop()
                if kind == "fn":
                    spans.append((pos, i, name[0], name[1]))
            last_boundary = i + 1
        elif c == ";":
            last_boundary = i + 1
    return spans


def first_argument(text, start):
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and depth == 0:
            break
        i += 1
    return re.sub(r"\s+", "", text[start:i]) or "-"


def source_files(src):
    for dirpath, _, names in os.walk(src):
        for name in sorted(names):
            if name.endswith((".cpp", ".h", ".ino", ".c")):
                yield os.path.join(dirpath, name)


def is_declaration(text, pos):
    """True when the identifier at pos is being declared or defined, not
    called: the token before it is another identifier (a type, an
    attribute) other than a word that may precede a call."""
    j = pos - 1
    while j >= 0 and text[j] in " \t\r\n":
        j -= 1
    if j < 0 or not (text[j].isalnum() or text[j] == "_"):
        return False  # an operator, '(', ',', ';', '&&' ... : a call
    k = j
    while k >= 0 and (text[k].isalnum() or text[k] == "_"):
        k -= 1
    word = text[k + 1:j + 1]
    if word[0].isdigit():
        return False
    return word not in CALL_PRECEDERS


def scan(root, src):
    sites = collections.Counter()
    where = collections.defaultdict(list)
    wall = []
    wrappers = []   # (file, line, fn, call, arg): a pin passed through a function not in PIN_CALLS
    for path in sorted(source_files(src)):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = strip_comments_and_strings(f.read())
        r = os.path.relpath(path, root).replace(os.sep, "/")
        dirs = directive_spans(text)
        code = blank_spans(text, dirs)
        fns = sorted(function_spans(code), key=lambda s: s[0] - s[1])  # innermost first
        for m in NAME_RE.finditer(text):
            name = m.group(1)
            if r in SCOPED_OUT.get(name, ()):
                continue
            line = text.count("\n", 0, m.start()) + 1
            directive = next((d for d in dirs if d[0] <= m.start() < d[1]), None)
            if directive and directive[2] is None:
                continue  # #if / #include text
            if name in UNCHECKED_CALLS and (r == UNCHECKED_CALLER or r in UNCHECKED_DEFINERS):
                continue
            if directive and directive[2] in PIN_CALLS + UNCHECKED_CALLS:
                continue
            wall.append((r, line, name, "alias"))
        for m in CALL_RE.finditer(text):
            call = m.group(1)
            if r in SCOPED_OUT.get(call, ()):
                continue
            line = text.count("\n", 0, m.start()) + 1
            directive = next((d for d in dirs if d[0] <= m.start() < d[1]), None)
            if call in UNCHECKED_CALLS:
                if r == UNCHECKED_CALLER or r in UNCHECKED_DEFINERS:
                    continue
                wall.append((r, line, call, "unchecked"))
                continue
            if directive:
                if directive[2] is None:
                    continue  # #if/#include text: not code
                d = text[directive[0]:directive[1]]
                if re.match(r"[ \t]*#[ \t]*define[ \t]+" + re.escape(call) + r"\b", d) and \
                        m.start() < directive[0] + d.find(call) + len(call) + 1:
                    continue  # the macro's own name
                fn = "define:" + directive[2]
                params = set(re.findall(r"\(([^)]*)\)", d.split(directive[2], 1)[1])[0].replace(" ", "").split(",")) \
                    if re.match(r"[ \t]*#[ \t]*define[ \t]+\w+\(", d) else set()
                owner = directive[2]
            else:
                span = next((f for f in fns if f[0] < m.start() < f[1]), None)
                # Inside a function body a match is always a call; only a
                # file-scope match can be a declaration or definition.
                if span is None and is_declaration(code, m.start()):
                    continue
                fn = span[2] if span else "<file>"
                params = span[3] if span else set()
                owner = fn.split("::")[-1]
            arg = first_argument(text, m.end())
            if call == "pinMode":
                # A reader that becomes a driver must change its key: key on
                # the whole mode argument (a ternary included). A mode held in
                # a variable can't be seen; review holds that.
                depth, k = 0, m.end()
                while k < len(text):
                    if text[k] in "([{":
                        depth += 1
                    elif text[k] in ")]}":
                        if depth == 0:
                            break
                        depth -= 1
                    elif text[k] == "," and depth == 0:
                        arg += "," + first_argument(text, k + 1)
                        break
                    k += 1
            excluded = r in SCOPED_OUT.get(owner, ())
            if call not in NOT_PIN_ARG and arg in params and (owner not in PIN_CALLS or excluded):
                wrappers.append((r, line, fn, call, arg))
            key = (r, fn, call, arg)
            sites[key] += 1
            where[key].append(line)
    return sites, where, wall, wrappers


def load_sites(path, root):
    listed = collections.Counter()
    problems = []
    targets = 0
    rel = os.path.relpath(path, root)
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            body, sep, comment = line.partition("#")
            parts = body.split()
            if len(parts) != 4:
                problems.append(f"{rel}:{n}: expected '<path> <function> <call> <first-arg>  # <TAG>: <why>'")
                continue
            comment = "#" + comment if sep else ""
            m = TAG_RE.match(comment)
            if not m:
                problems.append(f"{rel}:{n}: comment must start with a tag ({', '.join(TAGS)}) and a colon")
            elif m.group(1) == "RATCHET TARGET":
                targets += 1
            if not CITE_RE.search(comment[1:]):
                problems.append(f"{rel}:{n}: cites no issue (#N) or section (§N)")
            listed[tuple(parts)] += 1
    return listed, problems, targets


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=ROOT, help="repo root (tests)")
    ap.add_argument("--sites", default=None, help="sites file (default <root>/tools/pin_drive_sites.txt)")
    ap.add_argument("--quiet", action="store_true", help="print only failures and the summary")
    ap.add_argument("--list", action="store_true", help="print every site found, in sites-file form")
    ap.add_argument("--max-targets", type=int, default=None,
                    help="fail if the RATCHET TARGET count is above this (CI: the count on main)")
    ap.add_argument("--count-targets", metavar="FILE", help="print the RATCHET TARGET count in a sites file and exit")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)
    src = os.path.join(root, "src")
    sites_path = args.sites or os.path.join(root, "tools", "pin_drive_sites.txt")
    if args.count_targets:
        print(load_sites(args.count_targets, root)[2])
        return 0

    found, where, wall, wrappers = scan(root, src)
    if args.list:
        for key in sorted(found):
            for _ in range(found[key]):
                print("  ".join(key))
        return 0

    listed, problems, targets = load_sites(sites_path, root)
    tag_counts = collections.Counter()
    with open(sites_path, encoding="utf-8") as f:
        for raw in f:
            m = TAG_RE.match("#" + raw.partition("#")[2]) if raw.strip() and not raw.lstrip().startswith("#") else None
            if m:
                tag_counts[m.group(1)] += 1
    new = found - listed
    gone = listed - found
    macro_problems = [k for k in listed if k[1].startswith("define:") and k[1][7:] not in PIN_CALLS]
    failed = bool(wall or new or gone or problems or wrappers or macro_problems)

    for r, line, call, why in wall:
        if why == "unchecked":
            print(f"WALL: {r}:{line} calls {call}(); only {UNCHECKED_CALLER} may (§12.5). "
                  f"Use claimPins() from pin_owner.h.")
        else:
            print(f"WALL: {r}:{line} mentions {call} without calling it by name (an alias or pointer?). "
                  f"A pin call must be a direct call so it is listed (§12.5); rename the "
                  f"identifier if it is unrelated.")
    for key in sorted(new):
        lines = ",".join(str(x) for x in where[key])
        print(f"NEW PIN SITE: {key[0]}:{lines} in {key[1]}: {key[2]}({key[3]}, ...)")
    if new:
        print("  Every direct pin call must touch only a pin its code claimed first and")
        print("  still holds (ARCHITECTURE.md §12.5). Check that, then list the site in")
        print(f"  {os.path.relpath(sites_path, root)} with its tag and citation.")
    for key in sorted(gone):
        print(f"GONE: {os.path.relpath(sites_path, root)} lists '{'  '.join(key)}', which is no longer in src/. Remove the line.")
    for p in problems:
        print(f"BAD LINE: {p}")
    for r, line, fn, call, arg in wrappers:
        print(f"WRAPPER: {r}:{line} {fn}() passes its parameter '{arg}' to {call}(): add {fn.split('::')[-1].replace('define:', '')} "
              f"to PIN_CALLS in tools/check_pins.py so its callers are listed")
    for k in macro_problems:
        print(f"MACRO: {k[1]} is listed but {k[1][7:]} is not in PIN_CALLS, so its callers can't be seen")

    if args.max_targets is not None and targets > args.max_targets:
        print(f"MORE TARGETS: {targets} RATCHET TARGET lines, main has {args.max_targets}. "
              "A known violation may be fixed, never added; review the new lines.")
        failed = True
    total = sum(found.values())
    print(f"check_pins.py: {total} pin sites, {targets} ratchet target(s) remaining "
          f"({', '.join(f'{t} {tag_counts[t]}' for t in TAGS)})")
    if not failed and not args.quiet:
        print("OK: every pin site is listed, and only pin_owner.cpp writes the registry unchecked.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
