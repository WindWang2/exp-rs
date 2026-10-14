#!/usr/bin/env python3
"""check_test_registration.py — orphan-test census gate (#1494, R13-I WP-04).

Every tests/test_*.cpp must be accounted for exactly once:

  * registered   — its stem is named in tests/CMakeLists.txt / *.cmake, or
                   (with --build-dir) the file appears in compile_commands.json
                   (catches foreach-computed registrations such as the
                   SICNU_VERIFIER_LANES family whose literal stems never
                   appear in the CMake text)
  * pattern-ok   — matches a documented PATTERN_ALLOWLIST entry (each entry
                   carries its reason; add one only with a justification)
  * ORPHAN       — neither: printed and counted, exit code 1

A file that is only #included by another test, or a deliberate utility, must
carry an allowlist entry — silent orphans fail the gate.

Usage:
    python3 scripts/check_test_registration.py [--build-dir build-r13-i]

Exit: 0 = no unexplained orphans, 1 = orphans found, 2 = usage/io error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

# Each entry: (regex over the file name, reason this family is registered or
# deliberately not a standalone test). Verified on origin/master 89229568.
PATTERN_ALLOWLIST: list[tuple[str, str]] = [
    # The SICNU_VERIFIER_LANES foreach in tests/CMakeLists.txt computes
    # test_verifier_{core,checks,provenance,packs,render,drift,adversarial}_14
    # at configure time; the literal stems never appear in the file.
    (r"^test_verifier_(core|checks|provenance|packs|render|drift|adversarial)_14\.cpp$",
     "registered via SICNU_VERIFIER_LANES foreach (tests/CMakeLists.txt)"),
]


def collect_test_sources(tests_dir: str) -> list[str]:
    return sorted(
        f for f in os.listdir(tests_dir)
        if f.startswith("test_") and f.endswith(".cpp")
        and os.path.isfile(os.path.join(tests_dir, f))
    )


def cmake_text(tests_dir: str) -> str:
    chunks = []
    cm = os.path.join(tests_dir, "CMakeLists.txt")
    if os.path.isfile(cm):
        chunks.append(open(cm, encoding="utf-8", errors="replace").read())
    for root, _dirs, files in os.walk(tests_dir):
        for f in files:
            if f.endswith(".cmake"):
                chunks.append(open(os.path.join(root, f), encoding="utf-8",
                                   errors="replace").read())
    return "\n".join(chunks)


def compiled_sources(build_dir: str) -> set[str]:
    db_path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(db_path):
        return set()
    try:
        db = json.load(open(db_path, encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return set()
    return {os.path.realpath(e.get("file", "")) for e in db}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tests-dir", default="tests")
    ap.add_argument("--build-dir", default=None,
                    help="configured build tree; compile_commands.json is used "
                         "as ground truth for foreach-computed registrations")
    args = ap.parse_args()

    if not os.path.isdir(args.tests_dir):
        print(f"census: tests dir '{args.tests_dir}' not found", file=sys.stderr)
        return 2

    text = cmake_text(args.tests_dir)
    compiled = compiled_sources(args.build_dir) if args.build_dir else set()
    tests_root = os.path.realpath(args.tests_dir)

    orphans, pattern_ok = [], []
    for f in collect_test_sources(args.tests_dir):
        stem = f[:-4]
        if re.search(r"(?<![A-Za-z0-9_])" + re.escape(stem) + r"(?![A-Za-z0-9_])", text):
            continue
        if compiled and os.path.join(tests_root, f) in compiled:
            continue
        reason = next((why for pat, why in PATTERN_ALLOWLIST if re.match(pat, f)), None)
        if reason:
            pattern_ok.append((f, reason))
        else:
            orphans.append(f)

    print(f"census: {len(collect_test_sources(args.tests_dir))} test_*.cpp; "
          f"{len(pattern_ok)} pattern-ok; {len(orphans)} orphan(s)")
    for f, why in pattern_ok:
        print(f"  pattern-ok: {f} — {why}")
    if orphans:
        print("unregistered test sources (not in CMake text, not compiled, "
              "no allowlist entry):", file=sys.stderr)
        for f in orphans:
            print(f"  ORPHAN: {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
