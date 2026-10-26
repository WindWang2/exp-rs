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

  1. registration-subset — the set of registered test TARGETS and add_test
     NAMES at HEAD is a superset of the set at --base (default
     origin/master). A registration present on master that disappeared from
     HEAD is a lost sibling block. Intentional removals must be named in
     --allow-removed (fail-closed; the bare name matches either kind).
  2. conflict markers — no `<<<<<<< `, `>>>>>>> `, `||||||| ` or bare
     `=======` line-start markers in the shared merge surface (default:
     tests/CMakeLists.txt, CMakeLists.txt, CMakePresets.json, cmake/**,
     scripts/**, .github/workflows/**; *.md excluded — prose legitimately
     quotes markers). Widen with --all (whole tracked tree, still no .md).
  3. duplicate registrations — one target (or add_test name) named twice in
     the uncommented CMake text of the tests tree (a pasted-twice block
     would shadow the first registration's properties).

Static fallback (--no-git, or base/head refs not resolvable without
--require-refs): checks 2 and 3 run against the on-disk tree instead of the
committed one, and the run says so loudly. Nothing is merged, rewritten or
deleted — read-only gate.

Usage:
    python3 scripts/check_merge_registration_integrity.py \
        [--tests-dir tests] [--repo-root .] [--base origin/master] \
        [--head HEAD] [--allow-removed t1,t2] [--all] [--no-git] \
        [--require-refs]

Exit: 0 = clean, 1 = findings (or, with --require-refs, unresolvable refs),
2 = usage/io error.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

# Registration entry points that create a test target in the tests tree. The
# sicnu_add_* family is open-ended on purpose: new helper names register the
# same way and must not silently fall out of the set comparison. Captured
# names that contain "/" are skipped (multi-line helper calls hand their
# first FILE argument to the regex — e.g. sicnu_add_header_probes; real test
# targets in this repo are single path components).
REGISTER_RE = re.compile(
    r"\b(?:sicnu_add_[a-z_]+|qt_add_executable|add_executable)\s*\(\s*([^\s)$,]+)"
)
# Pure-script tests register via add_test(NAME ...) with no target — the
# #1715-class merge loss eats those blocks exactly as often (review R1 F2).
# The alternation captures QUOTED names ("M2-Challenger: ...") as one token;
# surrounding quotes are stripped in _valid_name (review R2 N1 — master
# carries 4 quoted registrations that were invisible to both subset and
# duplicate checks).
ADD_TEST_RE = re.compile(
    r"\badd_test\s*\(\s*(?:NAME\s+)?(\"[^\"]*\"|[^\s)$,]+)"
)

MARKER_RE = re.compile(r"^(<<<<<<< |>>>>>>> |\|\|\|\|\|\|\| |=======$)")
GIT_GREP_MARKER = r"^(<<<<<<< |>>>>>>> |\|\|\|\|\|\|\| |=======\r?$)"

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
    try:
        return subprocess.run(
            ["git", "-C", repo, *args], capture_output=True, text=True, check=False
        )
    except OSError as exc:
        # A missing/unlaunchable git is an io error, not a findings result —
        # the documented exit contract says 2 (review R2 N5).
        print(f"integrity: cannot run git ({exc})", file=sys.stderr)
        sys.exit(2)


def uncommented(text: str) -> str:
    """Strip `#`-to-EOL, `#[[ ]]` and long-bracket `#[==[ ]==]` comments
    (same semantics as check_test_registration.py: a commented-out
    registration must not count — review R1 F5 closed the long-bracket gap)."""
    lines = []
    bracket_level = 0  # 0 = none; >0 = inside #[==[ ... ]==] of that level
    for line in text.splitlines():
        if bracket_level:
            if re.match(r"^\]%s\]" % ("=" * bracket_level), line):
                bracket_level = 0
            continue
        stripped = line.lstrip()
        m = re.match(r"^#\[(=*)\[", stripped)
        if m:
            level = len(m.group(1))
            closer = "]%s]" % ("=" * level)
            if closer not in stripped[len(m.group(0)):]:
                bracket_level = level
            continue
        if stripped.startswith("#"):
            continue
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def _normalize_name(name: str | None) -> str | None:
    """Canonical registration name, or None when the capture is not one.

    Quoted add_test names ("M2-Challenger: ...") lose their quotes (CMake
    allows spaces in test names — review R2 N1); foreach-computed ${...}
    captures, quoted-but-empty names, path arguments of multi-line helpers
    and exclusion pathspecs are not registrations."""
    if not name:
        return None
    if name.startswith('"'):
        name = name.strip()
        if len(name) < 2 or not name.endswith('"'):
            return None
        name = name[1:-1]
        return name or None
    if name.startswith("$"):
        return None  # foreach-computed names are not literal registrations
    if "/" in name or name.startswith("!"):
        return None  # file arguments of multi-line helpers, negations
    return name


def _branch_aware_counts(text: str, pattern: re.Pattern) -> dict[str, int]:
    """Per-name duplicate maxima with if/elseif/else branch awareness.

    CMake's alternative-registration idiom registers the SAME test/target
    name once per exclusive branch (``if(TARGET x) ... else() ...`` — e.g.
    classroom_batch_conformance on master): sibling branches of one if are
    ONE registration at configure time, so their occurrences must not count
    as duplicates. The cell key is (name, full branch-index stack); a
    duplicate is any single cell holding two or more occurrences (the
    sequential-paste #1715 class), the same name under two sibling ifs
    (identical stack), or a name both in a scope AND inside that scope's if
    (they coexist whenever the condition holds — review R2 N4).

    One-line ``if(A) add_test(...) endif()`` forms are tokenized in order so
    the registration is attributed to the inner depth and the stack stays
    balanced (review R2 N4; latent on today's tree, closed anyway).
    """
    branch = [0]
    cells: dict[tuple, int] = {}
    keyword = re.compile(r"\b(if\s*\(|elseif\s*\(|else\b|endif\b)")

    def bump(match: "re.Match") -> None:
        name = _normalize_name(match.group(1))
        if name is not None:
            key = (name, tuple(branch))
            cells[key] = cells.get(key, 0) + 1

    for line in uncommented(text).splitlines():
        last = 0
        for kw in keyword.finditer(line):
            # registrations before this keyword belong to the current stack
            for m in pattern.finditer(line, last, kw.start()):
                bump(m)
            token = kw.group(0)
            if token.startswith("elseif") or token == "else":
                if branch:
                    branch[-1] += 1
            elif token.startswith("endif"):
                if len(branch) > 1:
                    branch.pop()
            else:  # if(
                branch.append(0)
                # skip past the condition's closing paren so a registration
                # inside a one-line if(...) body is scanned at the new depth
                close = line.find(")", kw.end())
                last = close + 1 if close != -1 else kw.end()
                continue
            last = kw.end()
        for m in pattern.finditer(line, last):
            bump(m)

    # Cell-level duplicates (sequential paste / sibling ifs)…
    per_name_max: dict[str, int] = {}
    for (name, sig), count in cells.items():
        per_name_max[name] = max(per_name_max.get(name, 0), count)
    # …plus scope-vs-own-if coexistence: a name registered in a scope AND in
    # a direct child if of that same scope can be active twice.
    for (name, sig), _count in list(cells.items()):
        if len(sig) >= 1 and cells.get((name, sig[:-1]), 0) >= 1:
            per_name_max[name] = max(per_name_max.get(name, 0), 2)
    return per_name_max


def names_in_text(text: str, pattern: re.Pattern) -> set[str]:
    out = set()
    for name in pattern.findall(uncommented(text)):
        normalized = _normalize_name(name)
        if normalized is not None:
            out.add(normalized)
    return out


def targets_in_text(text: str) -> set[str]:
    return names_in_text(text, REGISTER_RE)


def tracked_cmake_files(repo: str, ref: str, tests_dir: str) -> tuple[list[str], list[str]]:
    """CMake files of the tests tree at @p ref (CMakeLists.txt + included
    .cmake — review R1 F7: the two duplicate-scan modes must agree)."""
    problems: list[str] = []
    res = git(repo, "ls-tree", "-r", "--name-only", ref, "--", tests_dir)
    if res.returncode != 0:
        return [], [f"gate could not list {tests_dir} at {ref} (git rc={res.returncode})"]
    files = [
        p for p in res.stdout.splitlines()
        if p.endswith("/CMakeLists.txt") or p == f"{tests_dir}/CMakeLists.txt"
        or p.endswith(".cmake")
    ]
    return files, problems


def registration_sets(repo: str, ref: str, tests_dir: str) -> tuple[set[str], set[str], list[str]]:
    """(target names, add_test names, problems) at @p ref."""
    targets: set[str] = set()
    addtests: set[str] = set()
    problems: list[str] = []
    files, list_problems = tracked_cmake_files(repo, ref, tests_dir)
    problems += list_problems
    for path in files:
        res = git(repo, "show", f"{ref}:{path}")
        if res.returncode != 0:
            # Fail closed: a file the gate cannot read shrinks the base set
            # and silently weakens the subset check (review R1 F6).
            problems.append(f"gate could not read {ref}:{path} (git rc={res.returncode})")
            continue
        targets |= names_in_text(res.stdout, REGISTER_RE)
        addtests |= names_in_text(res.stdout, ADD_TEST_RE)
    return targets, addtests, problems


def marker_specs(paths: list[str], whole_tree: bool) -> list[str]:
    # `:!*.md` — exclude shorthand with default magic, where `*` crosses `/`
    # (a `:(glob)!*.md` spelling is glob magic with the LITERAL pattern
    # "!*.md" and excludes nothing — review R1 F1, verified by counterexample
    # on nested and top-level .md files). `*.MD` covers Windows-authored
    # spellings (review R2 N2; git pathspecs are case-sensitive).
    specs = [":!*.md", ":!*.MD"]
    if whole_tree:
        specs.append(":/")
    else:
        for p in paths:
            specs.append(p if (p.endswith(".txt") or p.endswith(".json"))
                         else f":(glob){p}/**")
    return specs


def scan_markers_git(repo: str, ref: str, paths: list[str],
                     whole_tree: bool) -> tuple[list[str], list[str]]:
    """Marker scan of the COMMITTED tree (what push actually sends)."""
    res = git(repo, "grep", "-n", "-I", "-E", GIT_GREP_MARKER, ref, "--",
              *marker_specs(paths, whole_tree))
    if res.returncode == 0:
        return [l for l in res.stdout.splitlines() if l.strip()], []
    if res.returncode == 1:
        return [], []  # plain "no matches"
    # rc >= 2 is a git failure, not a clean tree — fail closed (review R1 F6).
    return [], [f"gate could not scan the committed tree at {ref} "
                f"(git grep rc={res.returncode}): {res.stderr.strip()[:200]}"]


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
                    if f.lower().endswith(".md"):
                        continue
                    child = os.path.join(root, f)
                    rel_child = os.path.relpath(child, repo_root).replace(os.sep, "/")
                    findings += _marker_hits(child, rel_child)
        elif os.path.isfile(full) and not rel.lower().endswith(".md"):
            findings += _marker_hits(full, rel)
    return findings


def duplicate_counts_git(repo: str, ref: str, tests_dir: str) -> tuple[dict[str, int], dict[str, int], list[str]]:
    """Per-name branch-aware duplicate maxima. Cross-file occurrences keep
    their own per-file maxima (a name legitimately registered in a sub-list
    is not a duplicate of the main list's; a single file registering the
    same name twice in one branch IS the #1715 paste class)."""
    target_counts: dict[str, int] = {}
    addtest_counts: dict[str, int] = {}
    problems: list[str] = []
    files, list_problems = tracked_cmake_files(repo, ref, tests_dir)
    problems += list_problems
    for path in files:
        res = git(repo, "show", f"{ref}:{path}")
        if res.returncode != 0:
            problems.append(f"gate could not read {ref}:{path} (git rc={res.returncode})")
            continue
        for name, count in _branch_aware_counts(res.stdout, REGISTER_RE).items():
            target_counts[name] = max(target_counts.get(name, 0), count)
        for name, count in _branch_aware_counts(res.stdout, ADD_TEST_RE).items():
            addtest_counts[name] = max(addtest_counts.get(name, 0), count)
    return target_counts, addtest_counts, problems


def duplicate_counts_disk(repo_root: str, tests_dir: str) -> tuple[dict[str, int], dict[str, int]]:
    target_counts: dict[str, int] = {}
    addtest_counts: dict[str, int] = {}
    tests_full = os.path.join(repo_root, tests_dir)
    for root, _dirs, files in os.walk(tests_full):
        for f in sorted(files):
            if f == "CMakeLists.txt" or f.endswith(".cmake"):
                try:
                    text = open(os.path.join(root, f), encoding="utf-8",
                                errors="replace").read()
                except OSError:
                    continue
                for name, count in _branch_aware_counts(text, REGISTER_RE).items():
                    target_counts[name] = max(target_counts.get(name, 0), count)
                for name, count in _branch_aware_counts(text, ADD_TEST_RE).items():
                    addtest_counts[name] = max(addtest_counts.get(name, 0), count)
    return target_counts, addtest_counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tests-dir", default="tests")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--base", default="origin/master",
                    help="ref whose registrations must survive at HEAD")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--allow-removed", default="",
                    help="comma-separated targets/add_test names intentionally dropped")
    ap.add_argument("--all", action="store_true",
                    help="marker-scan the whole tracked tree (still excluding *.md)")
    ap.add_argument("--no-git", action="store_true",
                    help="static mode: markers+duplicates only, no ref comparison")
    ap.add_argument("--require-refs", action="store_true",
                    help="fail (exit 1) when --base/--head cannot be resolved "
                         "instead of degrading to static checks — use this in "
                         "CI/ctest where a silent degrade loses the subset "
                         "invariant behind a green run (review R1 F3)")
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
            message = (f"integrity: ref not resolvable (base={args.base} ok={base_ok}, "
                       f"head={args.head} ok={head_ok})")
            if args.require_refs:
                print(message + " — failing (--require-refs)", file=sys.stderr)
                return 1
            print(message + " — degrading to static checks", file=sys.stderr)
            git_mode = False

    if git_mode:
        # --- check 1: registration subset (targets AND add_test names) -----
        base_targets, base_addtests, p1 = registration_sets(repo, args.base, tests_dir)
        head_targets, head_addtests, p2 = registration_sets(repo, args.head, tests_dir)
        problems += p1 + p2
        allowed = {t.strip() for t in args.allow_removed.split(",") if t.strip()}
        for kind, base_names, head_names in (("target", base_targets, head_targets),
                                             ("add_test", base_addtests, head_addtests)):
            lost = sorted(base_names - head_names - allowed)
            for name in lost:
                problems.append(
                    f"registration lost vs {args.base}: {kind} '{name}' is registered "
                    f"at {args.base} but not at {args.head} (merge-loss, or name it "
                    f"in --allow-removed if the removal is intentional)")
        print(f"integrity: {len(head_targets)} registered targets + "
              f"{len(head_addtests)} add_test names at {args.head} "
              f"(base {args.base}: {len(base_targets)}/{len(base_addtests)}; "
              f"lost: {len(base_targets - head_targets - allowed)}"
              f"+{len(base_addtests - head_addtests - allowed)}; "
              f"added: {len(head_targets - base_targets)}"
              f"+{len(head_addtests - base_addtests)})")

        # --- check 2: conflict markers in the committed tree ---------------
        marker_findings, p3 = scan_markers_git(repo, args.head, DEFAULT_MARKER_PATHS,
                                               args.all)
        problems += p3
        # --- check 3: duplicate registrations ------------------------------
        target_counts, addtest_counts, p4 = duplicate_counts_git(repo, args.head, tests_dir)
        problems += p4
    else:
        marker_findings = scan_markers_on_disk(repo, DEFAULT_MARKER_PATHS, tests_dir)
        target_counts, addtest_counts = duplicate_counts_disk(repo, tests_dir)

    for f in marker_findings:
        problems.append(f"conflict marker: {f}")
    for kind, counts in (("target", target_counts), ("add_test", addtest_counts)):
        for name, count in sorted(counts.items()):
            if count > 1:
                problems.append(f"duplicate registration: {kind} '{name}' "
                                f"registered {count} times")

    print(f"integrity: {len(marker_findings)} marker finding(s); "
          f"{sum(1 for p in problems if p.startswith('duplicate'))} duplicate(s); "
          f"mode={'git' if git_mode else 'static (no ref comparison)'}")
    if problems:
        print("merge-integrity findings:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
