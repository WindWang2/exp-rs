#!/usr/bin/env python3
"""stale_branches.py — classify remote branches; recommend, never act.

For every non-master remote branch this reports: ahead/behind vs master, how
many of its commits are already upstream by patch-id (`git cherry`), which
merged PRs touched the same files, and any files the branch carries that
master does not have at all (an unmerged increment — e.g. a test asset dropped
by a squash merge). Classification is advisory evidence; the tool NEVER
deletes branches, closes PRs, or merges anything.

Usage:
    python scripts/dev/stale_branches.py                     # human report
    python scripts/dev/stale_branches.py --json              # machine payload
    python scripts/dev/stale_branches.py --behind-threshold 50
    python scripts/dev/stale_branches.py --branch fix/ci-master-unblock

Exit codes: 0 always (report tool).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_FAILURE, ToolError, emit, gh_json, git_ok, run_git, trunk_ref,
)


def _remote_branches(repo: Path, only: str | None, trunk: str) -> list[str]:
    proc = run_git(repo, ["for-each-ref", "--format=%(refname:short)",
                          "refs/remotes/origin"])
    if proc.returncode != 0:
        raise ToolError(f"cannot list remote refs: {proc.stderr.strip()}",
                        EXIT_FAILURE)
    names = []
    for ln in proc.stdout.splitlines():
        name = ln.strip()
        if not name.startswith("origin/") or len(name) <= len("origin/"):
            continue
        if name in ("origin/HEAD", trunk):
            continue
        if only and name != f"origin/{only}":
            continue
        names.append(name)
    return sorted(names)


def _branch_files(repo: Path, branch: str, base: str) -> list[str]:
    proc = run_git(repo, ["diff", "--name-only", f"{base}...{branch}"])
    if proc.returncode != 0:
        return []
    return [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]


def _cherry(repo: Path, branch: str, base: str) -> tuple[int, int]:
    """(equivalent-upstream, not-upstream) commit counts by patch id."""
    proc = run_git(repo, ["cherry", base, branch])
    if proc.returncode != 0:
        return (0, 0)
    plus = minus = 0
    for ln in proc.stdout.splitlines():
        if ln.startswith("+"):
            plus += 1
        elif ln.startswith("-"):
            minus += 1
    return (minus, plus)


def _merged_prs_touching(repo: Path, files: list[str], limit: int,
                         branch_name: str) -> dict:
    prs, why = gh_json(repo, ["pr", "list", "--state", "merged", "--limit", str(limit),
                              "--json", "number,title,headRefName,files"])
    if prs is None:
        return {"status": "not-executed", "reason": why}
    file_set = set(files)
    hits = []
    for pr in prs:
        pr_files = {f["path"] for f in pr.get("files", [])}
        shared = file_set & pr_files
        ratio = len(shared) / len(file_set) if file_set else 0.0
        same_head = pr.get("headRefName") == branch_name
        if shared and (same_head or ratio >= 0.5):
            hits.append({"pr": pr["number"], "title": pr["title"],
                         "branch": pr.get("headRefName"),
                         "same_head_branch": same_head,
                         "shared_path_ratio": round(ratio, 3),
                         "shared_paths": sorted(shared)[:20]})
    hits.sort(key=lambda h: (h["same_head_branch"], h["shared_path_ratio"]), reverse=True)
    return {"count": len(hits), "pull_requests": hits[:5]}


def _unmerged_files(repo: Path, branch: str, trunk: str) -> list[str]:
    """Files present on the branch but entirely absent from the trunk."""
    proc = run_git(repo, ["diff", "--name-status", "--diff-filter=A",
                          f"{trunk}...{branch}"])
    if proc.returncode != 0:
        return []
    return [ln.split("\t", 1)[1] for ln in proc.stdout.splitlines()
            if ln.startswith("A\t")]


def classify(entry: dict, behind_threshold: int) -> tuple[str, str]:
    branch_name = entry["branch"].split("/", 1)[-1]
    merged = entry.get("merged_prs_touching", {})
    merged_hits = merged.get("pull_requests", []) if isinstance(merged, dict) else []
    strong = [h for h in merged_hits
              if h.get("same_head_branch") or h.get("shared_path_ratio", 0) >= 0.5]

    facts = []
    if entry["ahead_vs_master"] == 0:
        facts.append("every commit is already contained in the trunk")
    elif entry["patch_equivalent_upstream"] and not entry["patch_not_upstream"]:
        facts.append("every commit is already upstream by patch id")
    if merged_hits:
        best = merged_hits[0]
        facts.append(f"merged PR #{best['pr']} covers "
                     f"{int(best.get('shared_path_ratio', 0) * 100)}% of its files")
    if entry["unmerged_files"]:
        facts.append("carries files master does not have: "
                     + ", ".join(entry["unmerged_files"][:5]))
    facts.append(f"{entry['behind_master']} commits behind master")

    if entry["ahead_vs_master"] == 0:
        verdict = "merged-equivalent"
        action = "safe historical residue; delete only after human review"
    elif entry["patch_equivalent_upstream"] and not entry["patch_not_upstream"]:
        verdict = "merged-equivalent"
        action = "every commit is upstream by patch id; delete only after human review"
    elif entry["unmerged_files"] and strong:
        verdict = "superseded-with-unmerged-assets"
        action = ("content landed via merged PRs, but listed files were dropped by "
                  "those merges; port them deliberately or accept the loss")
    elif entry["unmerged_files"]:
        verdict = "unmerged-increment"
        action = "holds files master never received; inspect before discarding"
    elif strong:
        verdict = "superseded"
        action = "covered by merged PR(s); historical residue"
    elif entry["behind_master"] >= behind_threshold:
        verdict = "diverged-stale"
        action = "no merged-PR association and far behind master"
    else:
        verdict = "active-or-unknown"
        action = "not clearly stale; inspect before acting"
    return (verdict, "; ".join(facts) + f" — {action} (branch {branch_name})")


def _classify_branch(repo: Path, name: str, trunk: str, merged_lookback: int) -> dict:
    """Collect and classify one branch; raises ToolError if git cannot read it."""
    ahead = int(git_ok(repo, ["rev-list", "--count", f"{trunk}..{name}"]).strip() or 0)
    behind = int(git_ok(repo, ["rev-list", "--count", f"{name}..{trunk}"]).strip() or 0)
    files = _branch_files(repo, name, trunk)
    equiv, not_equiv = _cherry(repo, name, trunk)
    return {
        "branch": name,
        "ahead_vs_master": ahead,
        "behind_master": behind,
        "touched_files": files,
        "patch_equivalent_upstream": equiv,
        "patch_not_upstream": not_equiv,
        "unmerged_files": _unmerged_files(repo, name, trunk),
        "merged_prs_touching": _merged_prs_touching(repo, files,
                                                    merged_lookback,
                                                    name.split("/", 1)[-1]),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--behind-threshold", type=int, default=50,
                        help="how many commits behind master counts as stale")
    parser.add_argument("--branch", default=None,
                        help="restrict the report to one branch")
    parser.add_argument("--merged-lookback", type=int, default=100,
                        help="how many merged PRs to consider for associations")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        proc = run_git(Path.cwd(), ["rev-parse", "--show-toplevel"])
        if proc.returncode != 0:
            raise ToolError("not inside a git repository")
        repo = Path(proc.stdout.strip())
        trunk = trunk_ref(repo)
        names = _remote_branches(repo, args.branch, trunk)
    except ToolError as exc:
        print(f"stale-branch report failed: {exc}", file=sys.stderr)
        return exc.exit_code

    report = []
    for name in names:
        try:
            entry = _classify_branch(repo, name, trunk, args.merged_lookback)
        except ToolError as exc:
            # one unreadable branch must not abort the whole census: record
            # the failure and continue with the rest
            report.append({"branch": name, "classification": "read-failed",
                           "recommendation": f"{exc} — inspect manually"})
            continue
        verdict, recommendation = classify(entry, args.behind_threshold)
        entry["classification"] = verdict
        entry["recommendation"] = recommendation
        report.append(entry)

    summary: dict[str, int] = {}
    for entry in report:
        summary[entry["classification"]] = summary.get(entry["classification"], 0) + 1
    payload = {"branches": report, "summary": summary,
               "note": "advisory only: this tool never deletes branches or "
                       "closes/merges PRs"}

    if args.json:
        print(json.dumps(payload, indent=2))
        return 0

    lines = ["stale branch report (advisory — nothing is deleted or merged)",
             ""]
    for entry in report:
        lines.append(f"{entry['branch']}: {entry['classification']}")
        if entry["classification"] == "read-failed":
            lines.append(f"    {entry['recommendation']}")
            continue
        lines.append(f"    ahead {entry['ahead_vs_master']}, behind "
                     f"{entry['behind_master']}; patch-equivalent upstream "
                     f"{entry['patch_equivalent_upstream']}, not-equivalent "
                     f"{entry['patch_not_upstream']}")
        if entry["unmerged_files"]:
            lines.append("    unmerged files: " + ", ".join(entry["unmerged_files"]))
        lines.append(f"    {entry['recommendation']}")
    lines.append("")
    lines.append("summary: " + ", ".join(f"{k}={v}" for k, v in sorted(summary.items())))
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
