#!/usr/bin/env python3
"""
inject_webapp_version.py - stamp the webapp with its git build identity.

Mirrors the firmware versioning model (scripts/version_inject.py) for the
static webapp. Rewrites two sentinels in place:

  * webapp/index.html : const WEBAPP_VERSION = '...'  ->  'b<count> @<sha>'
  * webapp/sw.js      : const CACHE_VERSION = '...'    ->  'b<count>'

The version is scoped to the ``webapp/`` path (NOT repo-wide HEAD) so a
firmware-only commit does not churn the webapp stamp or needlessly bust the
service-worker cache:

  count = git rev-list --count HEAD -- webapp/
  sha   = git log -1 --format=%h    -- webapp/

Intended to run in CI (.github/workflows/static.yml) right before the Pages
artifact upload, so the *deployed* file carries an exact git identity while
the committed repo file keeps its 'dev' sentinel. Safe to run locally too;
it is idempotent and re-runnable.

If git metadata can't be read (e.g. not a repo), it leaves the files
untouched and exits 0 so a deploy/build never fails on the stamp alone.

Usage:
    python3 tools/inject_webapp_version.py [--webapp-dir PATH] [--check]
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Repo root is the parent of this tools/ directory.
REPO_ROOT = Path(__file__).resolve().parent.parent

INDEX_RE = re.compile(r"(const\s+WEBAPP_VERSION\s*=\s*')[^']*(';)")
CACHE_RE = re.compile(r"(const\s+CACHE_VERSION\s*=\s*')[^']*(';)")


def git_out(args):
    """Run a git command rooted at the repo; return stripped stdout or None."""
    try:
        result = subprocess.run(
            ["git", "-C", str(REPO_ROOT), *args],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def webapp_version():
    """(version_string, cache_string) scoped to the webapp/ path, or (None, None)."""
    count = git_out(["rev-list", "--count", "HEAD", "--", "webapp/"])
    sha = git_out(["log", "-1", "--format=%h", "--", "webapp/"])
    if not count or not sha:
        return None, None
    return f"b{count} @{sha}", f"b{count}"


def rewrite(path, pattern, value):
    """Replace the sentinel's quoted value in `path`. Returns True if changed."""
    if not path.exists():
        print(f"  skip: {path} not found")
        return False
    text = path.read_text(encoding="utf-8")
    new_text, n = pattern.subn(lambda m: m.group(1) + value + m.group(2), text)
    if n == 0:
        print(f"  warn: no sentinel matched in {path.name} (unchanged)")
        return False
    if new_text == text:
        print(f"  ok:   {path.name} already at '{value}'")
        return False
    path.write_text(new_text, encoding="utf-8")
    print(f"  set:  {path.name} -> '{value}'")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--webapp-dir", default=str(REPO_ROOT / "webapp"),
        help="Path to the webapp directory (default: <repo>/webapp)",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Print the computed version and exit without writing files",
    )
    args = parser.parse_args()

    version, cache = webapp_version()
    if version is None:
        print("inject_webapp_version: git metadata unavailable; leaving files as-is")
        return 0

    print(f"inject_webapp_version: WEBAPP_VERSION='{version}', CACHE_VERSION='{cache}'")
    if args.check:
        return 0

    webapp = Path(args.webapp_dir)
    rewrite(webapp / "index.html", INDEX_RE, version)
    rewrite(webapp / "sw.js", CACHE_RE, cache)
    return 0


if __name__ == "__main__":
    sys.exit(main())
