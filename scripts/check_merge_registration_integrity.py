#!/usr/bin/env python3
"""check_merge_registration_integrity.py — merge-loss防线 for test
registrations and conflict markers (#1715 class, R23-E WP3).

The incident class (PR #1714 → #1715): a five-track append-only merge of
tests/CMakeLists.txt dropped or mangled a sibling track's registration block
(conflict markers survived into master), and earlier (#1516 / merge 29596c913)
an exclusively-changed file was silently resolved to the base side. Post-hoc
forensics exist (scripts/dev/merge_loss_audit.py, path-keyed, historical
merges); this gate runs BEFORE push, on the current tree, and checks the
three invariants that historically broke:

  1. registration-subset — the set of registered test targets at HEAD is a
     superset of the set at --base (default origin/master). A registration
     present on master that disappeared from HEAD is a lost sibling block.
     Intentional removals must be named in --allow-removed (fail-closed).
  2. conflict markers — no `<<<<<<< `, `>>>>>>> `, `||||||| ` or bare
     `=======` line-start markers in the shared merge surface (default:
     tests/CMakeLists.txt, CMakeLists.txt, CMakePresets.json, cmake/**,
     scripts/**, .github/workflows/**; *.md excluded — prose legitimately
     quotes markers). Widen with --all (whole tracked tree, still no .md).
  3. duplicate registrations — one target named twice in the uncommented
     CMake text of the tests tree (a pasted-twice block would shadow the
     first registration's properties).

Static fallback (--no-git, or base/head refs not resolvable): checks 2 and 3
run against the on-disk tree instead of the committed one, and the run says
so loudly. Nothing is merged, rewritten or deleted — read-only gate.

Usage:
    python3 scripts/check_merge_registration_integrity.py \
        [--tests-dir tests] [--repo-root .] [--base origin/master] \
        [--head HEAD] [--allow-removed t1,t2] [--all] [--no-git]

Exit: 0 = clean, 1 = findings, 2 = usage/io error.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

# Registration entry points that create a test target in the tests tree. The
# sicnu_add_* family is open-ended on purpose: new helper names register the
# same way and must not silently fall out of the set comparison.
REGISTER_RE = re.compile(
    r"\b(?:sicnu_add_[a-z_]+|qt_add_executable|add_executable)\s*\(\s*([^\s)$,]+)"
)

MARKER_RE = re.compile(r"^(<<<<<<< |>>>>>>> |\|\|\|\|\|\|\| |=======$)")
GIT_GREP_MARKER = r"^(<<<<<<< |>>>>>>> |\|\|\|\|\|\|\| |=======$)"

# The five-track shared merge surface (highest-collision files first).
DEFAULT_MARKER_PATHS = [
    "tests/CMakeLists.txt",
    "CMakeLists.txt",
    "CMakePresets.json",
    "cmake",
    "scripts",
    ".github/workflows",
]


def git(repo: str, *args: str) -> "subprocess.CompletedProcess[str]":
    return subprocess.run(
        ["git", "-C", repo, *args], capture_output=True, text=True, check=False
    )


def uncommented(text: str) -> str:
    """Strip `#`-to-EOL and `#[[ ]]` bracket comments (same semantics as
    check_test_registration.py: a commented-out registration must not count)."""
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


def targets_in_text(text: str) -> set[str]:
    return set(target_counts_in_text(text))


def target_counts_in_text(text: str) -> dict[str, int]:
    """Per-occurrence counts (a pasted-twice block must be visible even
    though a set would collapse it — the count IS the finding)."""
    counts: dict[str, int] = {}
    for name in REGISTER_RE.findall(uncommented(text)):
        if name.startswith("$") or name.startswith('"'):
            continue  # foreach-computed names are not literal targets
        counts[name] = counts.get(name, 0) + 1
    return counts


def tracked_cmake_files(repo: str, ref: str, tests_dir: str) -> list[str]:
    res = git(repo, "ls-tree", "-r", "--name-only", ref, "--", tests_dir)
    if res.returncode != 0:
        return []
    return [
        p for p in res.stdout.splitlines()
        if p.endswith("/CMakeLists.txt") or p == f"{tests_dir}/CMakeLists.txt"
    ]


def registration_set(repo: str, ref: str, tests_dir: str) -> set[str]:
    names: set[str] = set()
    for path in tracked_cmake_files(repo, ref, tests_dir):
        res = git(repo, "show", f"{ref}:{path}")
        if res.returncode == 0:
            names |= targets_in_text(res.stdout)
    return names


def marker_specs(paths: list[str], whole_tree: bool) -> list[str]:
    specs = [":(glob)!*.md"]
    if whole_tree:
        specs.append(":/")
    else:
        for p in paths:
            specs.append(p if (p.endswith(".txt") or p.endswith(".json"))
                         else f":(glob){p}/**")
    return specs


def scan_markers_git(repo: str, ref: str, paths: list[str], whole_tree: bool) -> list[str]:
    """Marker scan of the COMMITTED tree (what push actually sends)."""
    res = git(repo, "grep", "-n", "-I", "-E", GIT_GREP_MARKER, ref, "--",
              *marker_specs(paths, whole_tree))
    if res.returncode == 0:
        return [l for l in res.stdout.splitlines() if l.strip()]
    return []


def _marker_hits(full: str, rel: str) -> list[str]:
    hits = []
    try:
        with open(full, encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                if MARKER_RE.match(line.rstrip("\r\n")):
                    hits.append(f"{rel}:{lineno}: conflict marker line")
    except OSError as exc:
        hits.append(f"{rel}: unreadable ({exc})")
    return hits


def scan_markers_on_disk(repo_root: str, paths: list[str], tests_dir: str) -> list[str]:
    """Static twin of scan_markers_git for trees without usable git refs."""
    findings = []
    wanted = [f"{tests_dir}/CMakeLists.txt" if p == "tests/CMakeLists.txt" else p
              for p in paths]
    for rel in wanted:
        full = os.path.join(repo_root, rel)
        if os.path.isdir(full):
            for root, _dirs, files in os.walk(full):
                for f in sorted(files):
                    if f.endswith(".md"):
                        continue
                    child = os.path.join(root, f)
                    rel_child = os.path.relpath(child, repo_root).replace(os.sep, "/")
                    findings += _marker_hits(child, rel_child)
        elif os.path.isfile(full) and not rel.endswith(".md"):
            findings += _marker_hits(full, rel)
    return findings


def duplicate_counts_git(repo: str, ref: str, tests_dir: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in tracked_cmake_files(repo, ref, tests_dir):
        res = git(repo, "show", f"{ref}:{path}")
        if res.returncode != 0:
            continue
        for name, count in target_counts_in_text(res.stdout).items():
            counts[name] = counts.get(name, 0) + count
    return counts


def duplicate_counts_disk(repo_root: str, tests_dir: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    tests_full = os.path.join(repo_root, tests_dir)
    for root, _dirs, files in os.walk(tests_full):
        for f in sorted(files):
            if f == "CMakeLists.txt" or f.endswith(".cmake"):
                try:
                    text = open(os.path.join(root, f), encoding="utf-8",
                                errors="replace").read()
                except OSError:
                    continue
                for name, count in target_counts_in_text(text).items():
                    counts[name] = counts.get(name, 0) + count
    return counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tests-dir", default="tests")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--base", default="origin/master",
                    help="ref whose registrations must survive at HEAD")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--allow-removed", default="",
                    help="comma-separated targets intentionally dropped")
    ap.add_argument("--all", action="store_true",
                    help="marker-scan the whole tracked tree (still excluding *.md)")
    ap.add_argument("--no-git", action="store_true",
                    help="static mode: markers+duplicates only, no ref comparison")
    args = ap.parse_args()

    repo = args.repo_root
    tests_dir = args.tests_dir.strip("/")
    problems: list[str] = []

    # A linked worktree carries .git as a FILE pointing at the shared git
    # dir — isdir() would wrongly degrade every worktree run to static mode.
    have_git = os.path.exists(os.path.join(repo, ".git"))
    git_mode = have_git and not args.no_git
    if git_mode:
        base_ok = git(repo, "rev-parse", "--verify", args.base).returncode == 0
        head_ok = git(repo, "rev-parse", "--verify", args.head).returncode == 0
        if not (base_ok and head_ok):
            print(f"integrity: ref not resolvable (base={args.base} ok={base_ok}, "
                  f"head={args.head} ok={head_ok}) — degrading to static checks",
                  file=sys.stderr)
            git_mode = False

    if git_mode:
        # --- check 1: registration subset ------------------------------------
        base_names = registration_set(repo, args.base, tests_dir)
        head_names = registration_set(repo, args.head, tests_dir)
        allowed = {t.strip() for t in args.allow_removed.split(",") if t.strip()}
        lost = sorted(base_names - head_names - allowed)
        for name in lost:
            problems.append(
                f"registration lost vs {args.base}: test target '{name}' is registered "
                f"at {args.base} but not at {args.head} (merge-loss, or name it in "
                f"--allow-removed if the removal is intentional)")
        print(f"integrity: {len(head_names)} registered targets at {args.head} "
              f"(base {args.base}: {len(base_names)}; lost: {len(lost)}; "
              f"added: {len(head_names - base_names)})")

        # --- check 2: conflict markers in the committed tree -----------------
        marker_findings = scan_markers_git(repo, args.head, DEFAULT_MARKER_PATHS,
                                           args.all)
        # --- check 3: duplicate registrations --------------------------------
        counts = duplicate_counts_git(repo, args.head, tests_dir)
    else:
        marker_findings = scan_markers_on_disk(repo, DEFAULT_MARKER_PATHS, tests_dir)
        counts = duplicate_counts_disk(repo, tests_dir)

    for f in marker_findings:
        problems.append(f"conflict marker: {f}")
    dups = [f"{name} registered {count} times"
            for name, count in sorted(counts.items()) if count > 1]
    for d in dups:
        problems.append(f"duplicate registration: {d}")

    print(f"integrity: {len(marker_findings)} marker finding(s); {len(dups)} "
          f"duplicate(s); mode={'git' if git_mode else 'static (no ref comparison)'}")
    if problems:
        print("merge-integrity findings:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
