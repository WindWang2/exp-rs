#!/usr/bin/env python3
"""check_test_registration.py — orphan-test census gate (#1494, R13-I WP-04).

Every tests/test_*.cpp must be accounted for exactly once:

  * registered   — its stem is named in the UNCOMMENTED tests/ CMake text
                   (comments stripped: a commented-out registration must not
                   mask an orphan), or (with --build-dir) the file appears in
                   compile_commands.json (ground truth for generated or
                   foreach-computed registrations)
  * template-ok  — produced by a template registration whose anchor is proven
                   present in the live CMake text (template_registrations);
                   an allowlist keyed on filenames alone would keep passing
                   after the foreach it names is deleted, so templates are
                   expanded from the text, never trusted blindly
  * ORPHAN       — neither: printed and counted, exit code 1

Static mode (no --build-dir) is a heuristic; build mode is authoritative.

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


def template_registrations(text: str) -> dict[str, str]:
    """Expand template (foreach-computed) registrations to concrete file names.

    Each family is emitted only when its anchor text is present RIGHT NOW in
    the CMake sources, so deleting or renaming the template turns its files
    back into visible orphans instead of permanent pattern-ok entries.
    """
    out: dict[str, str] = {}
    # tests/CMakeLists.txt: foreach(LANE ${SICNU_VERIFIER_LANES}) builds
    # test_verifier_${LANE}_14 — the literal stems never appear in the text.
    m = re.search(r"set\(\s*SICNU_VERIFIER_LANES\s*([^)]*?)\)", text, re.S)
    if m and "test_verifier_${LANE}_14" in text:
        for lane in m.group(1).split():
            out[f"test_verifier_{lane}_14.cpp"] = (
                "registered via SICNU_VERIFIER_LANES foreach (tests/CMakeLists.txt)")
    return out


def uncommented(text: str) -> str:
    """Strip comments so a commented-out registration cannot mask an orphan:
    `#...`-to-EOL and multi-line `#[[ ... ]]` bracket comments (N3).
    A `#` inside a quoted string on a registration line could theoretically
    produce a false orphan (noisy, fail-loud direction) — build mode
    (--build-dir) stays the authoritative check."""
    lines = []
    in_bracket = False
    for line in text.splitlines():
        if in_bracket:
            if "]]" in line:
                in_bracket = False
            continue
        stripped = line.lstrip()
        if stripped.startswith("#[["):
            if "]]" not in stripped[3:]:
                in_bracket = True
            continue
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def collect_test_sources(tests_dir: str) -> list[str]:
    """Walk the whole tests/ tree, not just its top level (#1494 follow-up).

    A test_*.cpp drafted into tests/support/ or any other subdirectory used
    to be invisible to the census — an orphan hiding one level down stayed
    green forever. Subdirectory paths are reported relative ("support/x.cpp")
    so the printed ORPHAN line names where the file actually is.
    """
    out = []
    for root, _dirs, files in os.walk(tests_dir):
        for f in sorted(files):
            if f.startswith("test_") and f.endswith(".cpp"):
                rel = os.path.relpath(os.path.join(root, f), tests_dir)
                out.append(rel.replace(os.sep, "/"))
    return sorted(out)


def cmake_text(tests_dir: str) -> str:
    chunks = []
    cm = os.path.join(tests_dir, "CMakeLists.txt")
    if os.path.isfile(cm):
        chunks.append(open(cm, encoding="utf-8", errors="replace").read())
    for root, _dirs, files in os.walk(tests_dir):
        for f in files:
            if f == "CMakeLists.txt" and root != tests_dir:
                # Subdirectory lists register sources under tests/ too; fixture
                # projects without any test_*.cpp references add nothing to
                # the searchable text.
                chunks.append(open(os.path.join(root, f), encoding="utf-8",
                                   errors="replace").read())
            elif f.endswith(".cmake"):
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
    searchable = uncommented(text)
    templates = template_registrations(text)
    compiled = compiled_sources(args.build_dir) if args.build_dir else set()
    tests_root = os.path.realpath(args.tests_dir)

    orphans, template_ok = [], []
    for f in collect_test_sources(args.tests_dir):
        # Basename stem: a registration names the file either bare
        # (add_executable(t test_x.cpp)) or prefixed (support/test_x.cpp);
        # the basename-stem regex matches both.
        stem = os.path.basename(f)[:-4]
        if re.search(r"(?<![A-Za-z0-9_])" + re.escape(stem) + r"(?![A-Za-z0-9_])", searchable):
            continue
        if compiled and os.path.realpath(os.path.join(tests_root, f)) in compiled:
            continue
        if f in templates or os.path.basename(f) in templates:
            # Template families are keyed on the bare filename the foreach
            # names; collect_test_sources yields relative paths for files in
            # subdirectories, so match the basename too (review R1 P2).
            template_ok.append((f, templates.get(f) or templates[os.path.basename(f)]))
        else:
            orphans.append(f)

    print(f"census: {len(collect_test_sources(args.tests_dir))} test_*.cpp; "
          f"{len(template_ok)} template-ok; {len(orphans)} orphan(s)")
    for f, why in template_ok:
        print(f"  template-ok: {f} — {why}")
    if orphans:
        print("unregistered test sources (not in uncommented CMake text, not "
              "compiled, no live template registration):", file=sys.stderr)
        for f in orphans:
            print(f"  ORPHAN: {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
