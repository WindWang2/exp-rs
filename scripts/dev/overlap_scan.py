#!/usr/bin/env python3
"""overlap_scan.py — file-level overlap evidence between planned work and everything else.

Given planned paths, lists who else touches them: open PRs, recently merged
PRs, remote branches, and sibling worktrees' uncommitted changes. This is
EVIDENCE ONLY — no claim that two changes are equivalent or conflictual is
made anywhere, by design; a human (or the planning agent) reads the table.

Usage:
    python scripts/dev/overlap_scan.py src/geospatial/remote tests/test_io.cpp
    python scripts/dev/overlap_scan.py --from-HEAD            # use this branch's changes
    python scripts/dev/overlap_scan.py --include-merged 40 src/geospatial
    python scripts/dev/overlap_scan.py --json src/geospatial

Exit code is always 0 (evidence tool); `--fail-on-overlap` returns 2 when any
hit is found, for use as a CI-style gate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_REFUSED, ToolError, emit, gh_json, git_ok, run, run_git,
)


def _repo() -> Path:
    proc = run_git(Path.cwd(), ["rev-parse", "--show-toplevel"])
    if proc.returncode != 0:
        raise ToolError("not inside a git repository", EXIT_REFUSED)
    return Path(proc.stdout.strip())


def _normalize(paths: list[str]) -> list[str]:
    out = []
    for raw in paths:
        p = raw.replace("\\", "/").rstrip("/")
        if p and p not in out:
            out.append(p)
    return out


def _matches(planned: list[str], file_path: str) -> bool:
    f = file_path.replace("\\", "/")
    for want in planned:
        if f == want or f.startswith(want + "/"):
            return True
    return False


def _branch_changed_files(repo: Path, branch: str, base: str) -> list[str]:
    proc = run_git(repo, ["diff", "--name-only", f"{base}...{branch}"])
    if proc.returncode != 0:
        return []
    return [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]


def _worktree_changes(repo: Path) -> list[dict]:
    proc = run_git(repo, ["worktree", "list", "--porcelain"])
    entries: list[dict] = []
    current: dict = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            if current:
                entries.append(current)
                current = {}
            continue
        if line.startswith("worktree "):
            current = {"path": line[len("worktree "):]}
        elif line.startswith("branch "):
            current["branch"] = line[len("branch "):]
    if current:
        entries.append(current)
    out = []
    for entry in entries:
        path = entry.get("path")
        if not path or not Path(path).exists():
            continue
        files = set()
        status = run_git(Path(path), ["status", "--porcelain"])
        if status.returncode == 0:
            for ln in status.stdout.splitlines():
                if len(ln) > 3:
                    files.add(ln[3:].strip().strip('"'))
        staged = run_git(Path(path), ["diff", "--name-only", "HEAD"])
        if staged.returncode == 0:
            for ln in staged.stdout.splitlines():
                if ln.strip():
                    files.add(ln.strip())
        if files:
            out.append({"worktree": path, "branch": entry.get("branch"),
                        "changed_files": sorted(files)})
    return out


def collect_overlap_evidence(repo: Path, planned: list[str], base: str,
                             include_merged: int) -> dict:
    """Gather overlap evidence for `planned` paths from every available source."""
    evidence: dict = {"planned_paths": planned, "open_prs": [], "merged_prs": [],
                      "remote_branches": [], "sibling_worktrees": []}

    open_prs, why = gh_json(repo, [
        "pr", "list", "--state", "open", "--limit", "100", "--json",
        "number,title,headRefName,files,updatedAt"])
    if open_prs is None:
        evidence["open_prs"] = {"status": "not-executed", "reason": why}
    else:
        for pr in open_prs:
            hits = [f["path"] for f in pr.get("files", []) if _matches(planned, f["path"])]
            if hits:
                evidence["open_prs"].append({
                    "pr": pr["number"], "title": pr["title"],
                    "branch": pr.get("headRefName"), "overlapping_paths": sorted(set(hits)),
                })

    if include_merged > 0:
        merged, why = gh_json(repo, [
            "pr", "list", "--state", "merged", "--limit", str(include_merged),
            "--json", "number,title,headRefName,files"])
        if merged is None:
            evidence["merged_prs"] = {"status": "not-executed", "reason": why}
        else:
            for pr in merged:
                hits = [f["path"] for f in pr.get("files", []) if _matches(planned, f["path"])]
                if hits:
                    evidence["merged_prs"].append({
                        "pr": pr["number"], "title": pr["title"],
                        "branch": pr.get("headRefName"),
                        "overlapping_paths": sorted(set(hits)),
                    })

    proc = run_git(repo, ["for-each-ref", "--format=%(refname:short)",
                          "refs/remotes/origin"])
    if proc.returncode == 0:
        names = [ln.strip() for ln in proc.stdout.splitlines()
                 if ln.strip().startswith("origin/")
                 and len(ln.strip()) > len("origin/")
                 and ln.strip() not in ("origin/HEAD", "origin/master")]
        for name in sorted(names):
            files = _branch_changed_files(repo, name, base)
            hits = sorted({f for f in files if _matches(planned, f)})
            if hits:
                ahead = git_ok(repo, ["rev-list", "--count", f"{base}..{name}"]).strip()
                behind = git_ok(repo, ["rev-list", "--count", f"{name}..{base}"]).strip()
                evidence["remote_branches"].append({
                    "branch": name, "ahead_vs_base": int(ahead or 0),
                    "behind_base": int(behind or 0), "overlapping_paths": hits,
                })

    for wt in _worktree_changes(repo):
        hits = sorted({f for f in wt["changed_files"] if _matches(planned, f)})
        if hits:
            evidence["sibling_worktrees"].append({
                "worktree": wt["worktree"], "branch": wt["branch"],
                "overlapping_paths": hits,
            })
    return evidence


def _head_changed_files(repo: Path, base: str) -> list[str]:
    proc = run_git(repo, ["diff", "--name-only", f"{base}...HEAD"])
    return [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()] \
        if proc.returncode == 0 else []


def _human(ev: dict) -> list[str]:
    lines = [f"overlap scan for: {', '.join(ev['planned_paths'])}"]
    def prs(key: str, label: str) -> None:
        items = ev[key]
        if isinstance(items, dict):
            lines.append(f"  {label}: not-executed ({items.get('reason')})")
            return
        lines.append(f"  {label}: {len(items)}")
        for item in items:
            lines.append(f"    {item.get('branch') or item.get('worktree')}: "
                         f"{len(item['overlapping_paths'])} paths")
            for p in item["overlapping_paths"][:10]:
                lines.append(f"      {p}")
    prs("open_prs", "open PRs")
    prs("merged_prs", "recently merged PRs")
    prs("remote_branches", "remote branches")
    prs("sibling_worktrees", "sibling worktrees (uncommitted)")
    lines.append("  (evidence only — no equivalence judgement is made)")
    return lines


def _count(ev: dict) -> int:
    total = 0
    for key in ("open_prs", "merged_prs", "remote_branches", "sibling_worktrees"):
        value = ev[key]
        if isinstance(value, list):
            total += len(value)
    return total


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="planned files/directories")
    parser.add_argument("--from-HEAD", action="store_true",
                        help="use the current branch's changed files as the planned paths")
    parser.add_argument("--base", default="origin/master",
                        help="base ref for branch comparisons (default origin/master)")
    parser.add_argument("--include-merged", type=int, default=0,
                        help="also scan the N most recently merged PRs")
    parser.add_argument("--fail-on-overlap", action="store_true",
                        help="exit 2 when any overlap is found")
    parser.add_argument("--json", action="store_true", help="machine-readable payload")
    args = parser.parse_args(argv)

    try:
        repo = _repo()
        planned = _normalize(args.paths)
        if getattr(args, "from_HEAD") or not planned:
            head_files = _head_changed_files(repo, args.base)
            for f in head_files:
                if f not in planned:
                    planned.append(f)
        evidence = collect_overlap_evidence(repo, planned, args.base,
                                            args.include_merged)
    except ToolError as exc:
        print(f"overlap scan failed: {exc}", file=sys.stderr)
        return exc.exit_code

    evidence["overlap_count"] = _count(evidence)
    emit(evidence, args.json, None if args.json else _human(evidence))
    if args.fail_on_overlap and evidence["overlap_count"] > 0:
        return EXIT_REFUSED
    return 0


if __name__ == "__main__":
    sys.exit(main())
