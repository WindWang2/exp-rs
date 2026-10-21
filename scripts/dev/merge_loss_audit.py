#!/usr/bin/env python3
"""merge_loss_audit.py — post-merge invariant check for silent content loss.

The incident class (merge 29596c913, forensics in .planning/r18-c-agent/):
a merge commit resolved an exclusively-changed file to the OTHER parent's
(== merge-base) version, silently reverting an already-merged fix along with
its regression tests — which is exactly why nothing caught it at the time.

Detection, per merge commit M with parents P1 P2 and merge-base B:

    A path changed by exactly ONE parent relative to B ("exclusive" change)
    was LOST when the merge result equals the base state for that path —
    i.e. the path does not appear in diff(B, M). This single condition
    covers all three change kinds: a modified file that reverted to the base
    blob, an added file that vanished, and a deleted file that resurrected
    with its base content.

Both-sides-changed paths are skipped on purpose: there a human resolution
legitimately picks a side, and reporting those would be noise.

Advisory tool: it deletes nothing, merges nothing, rewrites no history.
Exit codes follow the scripts/dev suite: 0 clean (or degraded base data),
1 invariant findings, 2 refused (not a repo / bad arguments).

Limitations, on the record:
  * path-keyed comparison (--no-renames); a rename + revert pair on the
    same path is indistinguishable from a modification.
  * criss-cross histories have several merge-base candidates; whatever
    `git merge-base` reports is used as B.
  * octopus merges (>2 parents) are skipped and counted.
  * an INTENTIONAL revert done inside a merge resolution is reported like
    an accidental loss — intent is not in the blob data. Read the finding,
    then decide.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

EXIT_CLEAN = 0
EXIT_FINDINGS = 1
EXIT_REFUSED = 2


def git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", errors="replace")


def parse_name_status(out: str) -> dict[str, str]:
    """--name-status output → {path: status letter} (renames not requested)."""
    changes: dict[str, str] = {}
    for line in out.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t", 1)
        if len(parts) != 2:
            continue
        status, path = parts
        changes[path] = status[0]  # strip score suffixes like M100
    return changes


def merge_findings(repo: Path, merge_sha: str, parents: list[str],
                   base: str) -> list[dict]:
    p1, p2 = parents[0], parents[1]
    d1 = parse_name_status(git(repo, "diff", "--name-status", "--no-renames",
                               base, p1).stdout)
    d2 = parse_name_status(git(repo, "diff", "--name-status", "--no-renames",
                               base, p2).stdout)
    dm = parse_name_status(git(repo, "diff", "--name-status", "--no-renames",
                               base, merge_sha).stdout)
    findings: list[dict] = []
    # An exclusive change is lost exactly when the merge result still equals
    # the base state: the path is absent from diff(B, M).
    for side, own, other in ((1, d1, d2), (2, d2, d1)):
        for path, status in sorted(own.items()):
            if path in other or path in dm:
                continue
            findings.append({
                "merge": merge_sha,
                "path": path,
                "lost_parent": side,
                "change": status,
            })
    return findings


def audit(repo: Path, rev_range: str | None, first_parent_only: bool,
          max_merges: int) -> tuple[list[dict], dict]:
    rev_args = ["rev-list", "--merges"]
    if first_parent_only:
        rev_args.append("--first-parent")
    if rev_range:
        rev_args.append(rev_range)
    else:
        rev_args.append("HEAD")
    listing = git(repo, *rev_args)
    if listing.returncode != 0:
        print(f"merge_loss_audit: rev-list failed: {listing.stderr.strip()}",
              file=sys.stderr)
        sys.exit(EXIT_REFUSED)
    merges = listing.stdout.split()

    parent_listing = git(repo, "rev-list", "--parents", "--merges", "HEAD")
    parents_by_sha: dict[str, list[str]] = {}
    if parent_listing.returncode == 0:
        for line in parent_listing.stdout.splitlines():
            fields = line.split()
            if len(fields) >= 2:
                parents_by_sha[fields[0]] = fields[1:]

    findings: list[dict] = []
    skipped_octopus = 0
    degraded_base = 0
    audited = 0
    for sha in merges[:max_merges] if max_merges else merges:
        parents = parents_by_sha.get(sha)
        if parents is None:
            rows = git(repo, "rev-list", "--parents", "-n", "1", sha)
            if rows.returncode != 0:
                continue
            parents = rows.stdout.split()[1:]
        if len(parents) != 2:
            skipped_octopus += 1
            continue
        base_proc = git(repo, "merge-base", parents[0], parents[1])
        if base_proc.returncode != 0 or not base_proc.stdout.strip():
            degraded_base += 1
            continue
        audited += 1
        found = merge_findings(repo, sha, parents, base_proc.stdout.strip())
        for f in found:
            subject = git(repo, "log", "-1", "--format=%s", sha).stdout.strip()
            f["subject"] = subject
        findings.extend(found)
    stats = {"audited": audited, "skipped_octopus": skipped_octopus,
             "degraded_base": degraded_base,
             "merges_seen": len(merges[:max_merges]) if max_merges else len(merges)}
    return findings, stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json", action="store_true",
                        help="machine-readable findings on stdout")
    parser.add_argument("--since", metavar="REV",
                        help="audit only merges reachable from REV (e.g. a branch)")
    parser.add_argument("--first-parent", action="store_true",
                        help="follow first-parent history only (trunk merges)")
    parser.add_argument("--max-merges", type=int, default=0,
                        help="bound the audit to the N most recent merges (0 = all)")
    args = parser.parse_args()

    repo = Path.cwd()
    if git(repo, "rev-parse", "--git-dir").returncode != 0:
        print("merge_loss_audit: not inside a git repository", file=sys.stderr)
        return EXIT_REFUSED

    findings, stats = audit(repo, args.since, args.first_parent, args.max_merges)

    if args.json:
        print(json.dumps({"findings": findings, "stats": stats}, indent=2))
    else:
        for f in findings:
            side = f["lost_parent"]
            print(f"{f['merge'][:12]}  parent-{side} lost a {f['change']} "
                  f"on {f['path']}  ({f['subject']})")
        print(f"audited {stats['audited']} merges "
              f"({stats['skipped_octopus']} octopus skipped, "
              f"{stats['degraded_base']} without a merge-base); "
              f"{len(findings)} finding(s)")
    return EXIT_FINDINGS if findings else EXIT_CLEAN


if __name__ == "__main__":
    sys.exit(main())
