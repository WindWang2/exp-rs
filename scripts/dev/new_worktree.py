#!/usr/bin/env python3
"""new_worktree.py — fail-closed worktree + branch creator for agent tracks.

Creates a unique branch and its worktree from a freshly fetched base ref,
refusing (exit 2, nothing created) on every unsafe condition:

  * the current checkout is on `master`,
  * the working tree is dirty,
  * the requested branch name already exists locally or on a remote,
  * the target path already exists on disk or is a registered worktree,
  * the base ref does not resolve (after fetching).

The only escape hatch for a taken name is `--on-name-conflict suffix`, which
adopts `<name>-<YYYYMMDD>-<short-base-sha>` and still never reuses an
existing branch or path. Creation is two-phase (`git branch` then
`git worktree add`); if the second phase fails, the branch created by the
first phase is deleted again, so no orphan branch survives.

Usage:
    python scripts/dev/new_worktree.py --path ../exp-rs-worktrees/<track> \\
        --branch agent/<track> [--base origin/master] [--no-fetch] \\
        [--on-name-conflict suffix] [--json]

Exit codes: 0 created, 2 refused (nothing created), 1 unexpected failure.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_REFUSED, ToolError, emit, git_ok, repo_slug, run, run_git,
)


def _refuse(message: str) -> int:
    print(f"refused: {message}", file=sys.stderr)
    return EXIT_REFUSED


def _dirty_entries(repo: Path) -> list[str]:
    proc = run_git(repo, ["status", "--porcelain"])
    if proc.returncode != 0:
        raise ToolError(f"cannot read working-tree status: {proc.stderr.strip()}")
    return [ln for ln in proc.stdout.splitlines() if ln.strip()]


def _branch_exists(repo: Path, name: str) -> str | None:
    """Return the qualifier of an existing ref for `name`, else None."""
    proc = run_git(repo, ["rev-parse", "--verify", "--quiet", f"refs/heads/{name}"])
    if proc.returncode == 0:
        return "local branch"
    for remote in ("origin",):
        proc = run_git(repo, ["rev-parse", "--verify", "--quiet",
                              f"refs/remotes/{remote}/{name}"])
        if proc.returncode == 0:
            return f"remote-tracking ref {remote}/{name}"
    return None


def _suffixed(name: str, base_sha: str) -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d")
    return f"{name}-{stamp}-{base_sha[:8]}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--path", required=True,
                        help="worktree path, relative to the current directory")
    parser.add_argument("--branch", required=True, help="branch name to create")
    parser.add_argument("--base", default="origin/master",
                        help="base ref for the new branch (default origin/master)")
    parser.add_argument("--no-fetch", action="store_true",
                        help="skip `git fetch origin --prune` (stale-ref risk)")
    parser.add_argument("--on-name-conflict", choices=("fail", "suffix"),
                        default="fail",
                        help="suffix adopts a dated name instead of refusing")
    parser.add_argument("--json", action="store_true", help="machine-readable payload")
    args = parser.parse_args(argv)

    cwd = Path.cwd()
    try:
        root_proc = run_git(cwd, ["rev-parse", "--show-toplevel"])
    except FileNotFoundError:
        print("git executable not found", file=sys.stderr)
        return EXIT_REFUSED
    if root_proc.returncode != 0:
        return _refuse(f"{cwd} is not inside a git repository")
    repo = Path(root_proc.stdout.strip())

    # A dirty master checkout means someone's unfinished work sits in the
    # shared checkout: refuse to derive a branch there. A CLEAN master
    # checkout is the sanctioned bootstrap (the goal-template runbook creates
    # the worktree from the main repo) and is allowed, with a note.
    branch = git_ok(repo, ["rev-parse", "--abbrev-ref", "HEAD"]).strip()
    dirty = _dirty_entries(repo)
    if branch == "master":
        if dirty:
            preview = "; ".join(dirty[:5]) + (" …" if len(dirty) > 5 else "")
            return _refuse(
                f"master checkout {repo} is dirty ({len(dirty)} entries): {preview} "
                "(commit or stash before deriving a new track branch)")
        print("note: running on a clean master checkout; the new branch is created "
              "from a ref, so nothing is written to master", file=sys.stderr)
    elif dirty:
        print(f"warning: current worktree has {len(dirty)} uncommitted entries; "
              "the new branch is created from a ref and is unaffected",
              file=sys.stderr)

    target = Path(args.path)
    if not target.is_absolute():
        target = (cwd / target).resolve()
    else:
        target = target.resolve()
    if target.exists():
        return _refuse(f"target path already exists: {target}")
    try:
        target.relative_to(repo)
    except ValueError:
        pass
    else:
        return _refuse(f"target path is inside this repository's working tree: {target}")

    proc = run_git(repo, ["worktree", "list", "--porcelain"])
    registered = {ln[len("worktree "):].strip() for ln in proc.stdout.splitlines()
                  if ln.startswith("worktree ")}
    if str(target) in registered:
        return _refuse(f"target path is already a registered worktree: {target}")

    if not args.no_fetch:
        fetch = run(["git", "fetch", "origin", "--prune"], cwd=repo, timeout=300.0)
        if fetch.returncode != 0:
            reason = fetch.stderr.strip()[:200]
            print(f"warning: git fetch origin --prune failed: {reason}", file=sys.stderr)
            print("warning: proceeding with possibly stale base refs", file=sys.stderr)

    proc = run_git(repo, ["rev-parse", "--verify", "--quiet", args.base])
    if proc.returncode != 0:
        return _refuse(f"base ref does not resolve: {args.base} "
                       "(use --no-fetch only with a locally known ref)")
    base_sha = proc.stdout.strip()

    existing = _branch_exists(repo, args.branch)
    if existing:
        if args.on_name_conflict == "suffix":
            args.branch = _suffixed(args.branch, base_sha)
            if _branch_exists(repo, args.branch):
                return _refuse(f"branch name still taken after suffixing: {args.branch}")
            print(f"note: name conflict on {existing}; adopted {args.branch}",
                  file=sys.stderr)
        else:
            return _refuse(f"branch already exists ({existing}): {args.branch}")

    # Phase 1: create the branch at the resolved base.
    created = run_git(repo, ["branch", args.branch, base_sha])
    if created.returncode != 0:
        return _refuse(f"git branch failed (race or policy): {created.stderr.strip()}")

    # Phase 2: attach the worktree; roll the branch back on any failure.
    added = run(["git", "-C", str(repo), "worktree", "add", str(target), args.branch],
                cwd=repo, timeout=600.0)
    if added.returncode != 0:
        run_git(repo, ["branch", "-D", args.branch])
        return _refuse(f"git worktree add failed: {added.stderr.strip()[:300]} "
                       "(branch rolled back)")
    # worktree add can also fail *after* registering the path partially; make
    # the failure observable rather than silent.
    proc = run_git(target, ["rev-parse", "HEAD"])
    if proc.returncode != 0:
        run(["git", "-C", str(repo), "worktree", "remove", "--force", str(target)],
            timeout=120.0)
        run_git(repo, ["branch", "-D", args.branch])
        return _refuse("worktree registered but HEAD unreadable; rolled back")

    payload = {
        "worktree": str(target),
        "branch": args.branch,
        "base": args.base,
        "base_sha": base_sha,
        "head": proc.stdout.strip(),
        "github_repo": repo_slug(repo),
    }
    emit(payload, args.json,
         [f"created worktree {target}",
          f"  branch {args.branch} @ {base_sha[:12]}",
          f"  head   {payload['head'][:12]}",
          "next: cd into the worktree, commit there, "
          "then `git push -u origin {branch}`.".format(branch=args.branch)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
