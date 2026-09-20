#!/usr/bin/env python3
"""preflight.py — one-command repository preflight for agent tracks.

Read-only by default (no fetch, no network writes). Prints, for the repository
the command runs in:

  * live master SHA / subject (and the local branch it reports from),
  * the local working-tree state (branch, HEAD, dirty-entry count),
  * GitHub open PRs / open issues / recently merged PRs (via gh; degrades to
    an explicit not-executed record when gh or the network is unavailable),
  * every linked worktree with its branch, HEAD and dirty-entry count,
  * every non-master remote branch with ahead/behind vs master.

Usage:
    python scripts/dev/preflight.py                 # human-readable summary
    python scripts/dev/preflight.py --json          # machine-readable payload
    python scripts/dev/preflight.py --fetch         # refresh origin refs first
    python scripts/dev/preflight.py --path <dir>    # inspect another worktree

Exit codes: 0 success (possibly gh-degraded), 2 refused (not a git repo),
75 transient git failure after retries.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_BUSY, EXIT_REFUSED, ToolError, emit, gh_json, git_ok, run, run_git,
    trunk_ref,
)


def _iso_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _worktrees(root: Path) -> list[dict]:
    out = []
    proc = run_git(root, ["worktree", "list", "--porcelain"])
    if proc.returncode != 0:
        return [{"error": proc.stderr.strip()}]
    entry: dict = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            if entry:
                out.append(entry)
                entry = {}
            continue
        if line.startswith("worktree "):
            entry["path"] = line[len("worktree "):]
        elif line.startswith("HEAD "):
            entry["head"] = line[len("HEAD "):]
        elif line.startswith("branch "):
            ref = line[len("branch "):]
            entry["branch"] = ref[len("refs/heads/"):] if ref.startswith("refs/heads/") else ref
        elif line == "bare":
            entry["bare"] = True
    if entry:
        out.append(entry)
    for item in out:
        path = item.get("path")
        if not path or item.get("bare"):
            item["dirty_entries"] = None
            continue
        status = run_git(Path(path), ["status", "--porcelain"])
        item["dirty_entries"] = (
            len([ln for ln in status.stdout.splitlines() if ln.strip()])
            if status.returncode == 0 else None
        )
    return out


def _remote_branches(root: Path, trunk: str) -> list[dict]:
    proc = run_git(root, ["for-each-ref", "--format=%(refname:short)",
                          "refs/remotes/origin"])
    if proc.returncode != 0:
        raise ToolError(f"cannot list remote refs: {proc.stderr.strip()}",
                        EXIT_BUSY)
    names = [n for n in (ln.strip() for ln in proc.stdout.splitlines())
             if n.startswith("origin/") and len(n) > len("origin/")
             and n not in ("origin/HEAD", trunk)]
    out = []
    for name in sorted(names):
        ahead = git_ok(root, ["rev-list", "--count", f"{trunk}..{name}"])
        behind = git_ok(root, ["rev-list", "--count", f"{name}..{trunk}"])
        subject = git_ok(root, ["log", "-1", "--format=%s", name])
        out.append({
            "name": name,
            "ahead_vs_master": int(ahead.strip() or 0),
            "behind_master": int(behind.strip() or 0),
            "tip_subject": subject.strip(),
        })
    return out


def _github(root: Path, merged_limit: int) -> dict:
    section: dict = {"available": True}
    open_prs, why = gh_json(root, [
        "pr", "list", "--state", "open", "--limit", "100",
        "--json", "number,title,headRefName,isDraft,updatedAt"])
    if open_prs is None:
        section["available"] = False
        section["open_prs"] = {"status": "not-executed", "reason": why}
    else:
        section["open_prs"] = open_prs
        section["open_pr_count"] = len(open_prs)

    open_issues, why = gh_json(root, [
        "issue", "list", "--state", "open", "--limit", "200",
        "--json", "number,title,updatedAt"])
    if open_issues is None:
        section["open_issues"] = {"status": "not-executed", "reason": why}
    else:
        section["open_issues"] = open_issues
        section["open_issue_count"] = len(open_issues)

    merged, why = gh_json(root, [
        "pr", "list", "--state", "merged", "--limit", str(merged_limit),
        "--json", "number,title,mergedAt"])
    if merged is None:
        section["merged_recent"] = {"status": "not-executed", "reason": why}
    else:
        section["merged_recent"] = merged
    return section


def _human(payload: dict) -> list[str]:
    lines = []
    master = payload["master"]
    lines.append(f"preflight @ {payload['generated_at']}")
    lines.append(f"  repository : {payload['repository']}")
    lines.append(f"  master     : {master['sha'][:12]} {master['subject']}"
                 f"  ({master.get('commit_date', '')})")
    lines.append(f"  from branch: {payload['branch']} (HEAD {payload['head'][:12]}, "
                 f"{payload['dirty_entries']} dirty entries)")
    gh = payload["github"]
    if gh["available"]:
        lines.append(f"  open PRs   : {gh.get('open_pr_count', '?')}"
                     f"   open issues: {gh.get('open_issue_count', '?')}")
        for pr in gh.get("merged_recent", [])[:5] if isinstance(
                gh.get("merged_recent"), list) else []:
            lines.append(f"    merged #{pr['number']} {pr['title']}")
    else:
        reason = gh["open_prs"].get("reason", "")
        lines.append(f"  github     : not-executed ({reason})")
    lines.append(f"  worktrees  : {len(payload['worktrees'])}")
    for wt in payload["worktrees"]:
        lines.append(f"    {wt.get('branch', '(detached)')} @ {wt.get('path')}"
                     f"  dirty={wt.get('dirty_entries')}")
    lines.append(f"  remote non-master branches: {len(payload['historical_branches'])}")
    for br in payload["historical_branches"]:
        lines.append(f"    {br['name']}: ahead {br['ahead_vs_master']}, "
                     f"behind {br['behind_master']}")
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="machine-readable payload")
    parser.add_argument("--fetch", action="store_true",
                        help="run `git fetch origin --prune` before reading (writes to refs)")
    parser.add_argument("--path", default=None, help="inspect this worktree/repo path")
    parser.add_argument("--merged-limit", type=int, default=20,
                        help="how many merged PRs to list (default 20)")
    args = parser.parse_args(argv)

    start = Path(args.path).resolve() if args.path else Path.cwd()
    try:
        root = run_git(start, ["rev-parse", "--show-toplevel"])
    except FileNotFoundError:
        print("git executable not found", file=sys.stderr)
        return EXIT_REFUSED
    if root.returncode != 0:
        print(f"refused: {start} is not inside a git repository", file=sys.stderr)
        return EXIT_REFUSED
    repo = Path(root.stdout.strip())

    if args.fetch:
        fetch = run(["git", "fetch", "origin", "--prune"], cwd=repo, timeout=300.0)
        if fetch.returncode != 0:
            print(f"warning: git fetch failed: {fetch.stderr.strip()[:200]}",
                  file=sys.stderr)

    try:
        trunk = trunk_ref(repo)
        master_sha = git_ok(repo, ["rev-parse", trunk]).strip()
        master_subject = git_ok(repo, ["log", "-1", "--format=%s", trunk]).strip()
        master_date = git_ok(repo, ["log", "-1", "--format=%cI", trunk]).strip()
        branch = git_ok(repo, ["rev-parse", "--abbrev-ref", "HEAD"]).strip()
        head = git_ok(repo, ["rev-parse", "HEAD"]).strip()
        status = run_git(repo, ["status", "--porcelain"])
        dirty = len([ln for ln in status.stdout.splitlines() if ln.strip()]) \
            if status.returncode == 0 else None
        worktrees = _worktrees(repo)
        historical = _remote_branches(repo, trunk)
    except ToolError as exc:
        print(f"preflight failed after retries: {exc}", file=sys.stderr)
        return exc.exit_code

    payload = {
        "generated_at": _iso_now(),
        "repository": str(repo),
        "trunk_ref": trunk,
        "master": {"ref": trunk, "sha": master_sha,
                   "subject": master_subject, "commit_date": master_date},
        "branch": branch,
        "head": head,
        "dirty_entries": dirty,
        "github": _github(repo, args.merged_limit),
        "worktrees": worktrees,
        "historical_branches": historical,
    }
    emit(payload, args.json, None if args.json else _human(payload))
    return 0


if __name__ == "__main__":
    sys.exit(main())
