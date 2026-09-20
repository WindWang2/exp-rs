#!/usr/bin/env python3
"""review_pack.py — collect reproducible PR evidence and draft a PR body.

Runs read-only git queries against base..head and writes a pack containing:
the baseline/head SHAs and commit list, the diff stat, changed public headers
(the changed API surface), changed tests, `git diff --check` verdict, an
overlap scan of the changed files (open PRs / remote branches / sibling
worktrees), and a PR-body draft with every required section. The pack is
deterministic: all volatile data sits on one `Generated:` line, so two runs
are byte-identical once that line is stripped.

Usage:
    python scripts/dev/review_pack.py --base origin/master --head HEAD \\
        --title "feat(dev): agent worktree tooling" --out PR_PACK.md
    python scripts/dev/review_pack.py --json          # payload only

Exit codes: 0 pack written (or printed), 1 git/gh failure, 2 self-check
failure (a required section is missing — the pack is incomplete and is NOT
written).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_FAILURE, EXIT_REFUSED, ToolError, emit, gh_json, git_ok, run_git,
)
from overlap_scan import collect_overlap_evidence  # noqa: E402

REQUIRED_SECTIONS = [
    "Baseline", "Scope", "Non-goals", "Design", "Issue mapping", "Tests",
    "Resources", "Review disposition", "Known limitations", "Conflict hotspots",
]


def _group(files: list[str]) -> dict[str, list[str]]:
    def is_header(f: str) -> bool:
        return f.endswith((".h", ".hpp"))
    def is_tooling_test(f: str) -> bool:
        return f.startswith("scripts/") and "/tests/" in f
    def is_cpp_test(f: str) -> bool:
        return f.startswith("tests/")
    def is_public_api(f: str) -> bool:
        return is_header(f) and (f.startswith("src/") or f.startswith("include/"))
    def is_doc(f: str) -> bool:
        return f.endswith((".md", ".txt")) or f.startswith("docs/")
    def is_tooling(f: str) -> bool:
        return f.startswith("scripts/")
    groups: dict[str, list[str]] = {"sources": [], "public_headers": [],
                                    "tests": [], "dev_tooling": [],
                                    "docs": [], "other": []}
    for f in files:
        if is_public_api(f):
            groups["public_headers"].append(f)
        elif is_tooling_test(f) or is_cpp_test(f):
            groups["tests"].append(f)
        elif is_tooling(f):
            groups["dev_tooling"].append(f)
        elif is_doc(f):
            groups["docs"].append(f)
        elif f.startswith("src/"):
            groups["sources"].append(f)
        else:
            groups["other"].append(f)
    return {k: sorted(v) for k, v in groups.items() if v}


def _diff_check(repo: Path, base: str, head: str) -> dict:
    proc = run_git(repo, ["diff", "--check", f"{base}...{head}"])
    return {"clean": proc.returncode == 0 and not proc.stdout.strip(),
            "output": proc.stdout.strip()[:2000],
            "rc": proc.returncode}


def collect(repo: Path, base: str, head: str) -> dict:
    base_sha = git_ok(repo, ["rev-parse", base]).strip()
    head_sha = git_ok(repo, ["rev-parse", head]).strip()
    base_subject = git_ok(repo, ["log", "-1", "--format=%s", base]).strip()
    head_subject = git_ok(repo, ["log", "-1", "--format=%s", head]).strip()
    log = git_ok(repo, ["log", "--oneline", f"{base}..{head}"]).strip()
    stat = git_ok(repo, ["diff", "--stat", f"{base}...{head}"]).strip()
    numstat = git_ok(repo, ["diff", "--numstat", f"{base}...{head}"]).strip()
    files = [ln.split("\t")[-1] for ln in numstat.splitlines() if ln.strip()]
    changed: dict = {
        "base": {"ref": base, "sha": base_sha, "subject": base_subject},
        "head": {"ref": head, "sha": head_sha, "subject": head_subject},
        "commits": log.splitlines(),
        "diff_stat": stat.splitlines(),
        "changed_files": files,
        "groups": _group(files),
        "diff_check": _diff_check(repo, base, head),
    }
    changed["overlap"] = collect_overlap_evidence(repo, files, base, 20)
    prs, why = gh_json(repo, ["pr", "list", "--state", "open", "--limit", "100",
                              "--json", "number,title"])
    changed["open_prs"] = prs if prs is not None else {"status": "not-executed",
                                                       "reason": why}
    return changed


def render_pr_body(data: dict, title: str) -> str:
    g = data["groups"]
    lines = []
    lines.append(f"# {title}")
    lines.append("")
    lines.append(f"Generated: {datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}")
    lines.append("")
    lines.append("## Baseline")
    lines.append("")
    lines.append(f"- base `{data['base']['ref']}` @ `{data['base']['sha']}` "
                 f"({data['base']['subject']})")
    lines.append(f"- head `{data['head']['ref']}` @ `{data['head']['sha']}` "
                 f"({data['head']['subject']})")
    lines.append(f"- commits in range: {len(data['commits'])}")
    lines.append("")
    lines.append("## Scope")
    lines.append("")
    for group, files in g.items():
        lines.append(f"- {group} ({len(files)}): "
                     + ", ".join(f"`{f}`" for f in files[:12])
                     + (" …" if len(files) > 12 else ""))
    lines.append("")
    lines.append("## Non-goals")
    lines.append("")
    lines.append("<!-- TODO: what this PR deliberately does not do -->")
    lines.append("")
    lines.append("## Design")
    lines.append("")
    lines.append("<!-- TODO: design notes and alternatives rejected -->")
    lines.append("")
    lines.append("## Issue mapping")
    lines.append("")
    lines.append("<!-- TODO: issue numbers this PR closes or relates to -->")
    lines.append("")
    lines.append("## Tests")
    lines.append("")
    lines.append("<!-- TODO: exact commands and their output, run twice -->")
    lines.append("")
    lines.append("## Resources")
    lines.append("")
    lines.append("<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->")
    lines.append("")
    lines.append("## Review disposition")
    lines.append("")
    lines.append("<!-- TODO: reviewer findings and their fix state -->")
    lines.append("")
    lines.append("## Known limitations")
    lines.append("")
    lines.append("<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->")
    lines.append("")
    lines.append("## Conflict hotspots")
    lines.append("")
    ov = data["overlap"]
    lines.append(f"overlap evidence for this diff's {len(data['changed_files'])} paths:")
    for key in ("open_prs", "merged_prs", "remote_branches", "sibling_worktrees"):
        items = ov.get(key)
        if isinstance(items, dict):
            lines.append(f"- {key}: not-executed ({items.get('reason')})")
        else:
            lines.append(f"- {key}: {len(items)}")
            for item in items:
                names = item.get("branch") or item.get("worktree") or item.get("pr")
                lines.append(f"  - `{names}` touches {len(item['overlapping_paths'])} "
                             "of these paths")
    lines.append("")
    lines.append("---")
    lines.append("")
    lines.append("## Diff stat")
    lines.append("")
    lines.append("```")
    lines.extend(data["diff_stat"])
    lines.append("```")
    lines.append("")
    lines.append("## Commits")
    lines.append("")
    lines.append("```")
    lines.extend(data["commits"])
    lines.append("```")
    return "\n".join(lines) + "\n"


def _self_check(body: str) -> list[str]:
    missing = [s for s in REQUIRED_SECTIONS if f"## {s}" not in body]
    return missing


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", default="origin/master")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--title", default="(title pending)")
    parser.add_argument("--out", default=None, help="write the pack to this file")
    parser.add_argument("--json", action="store_true", help="print the raw payload")
    args = parser.parse_args(argv)

    try:
        proc = run_git(Path.cwd(), ["rev-parse", "--show-toplevel"])
        if proc.returncode != 0:
            raise ToolError("not inside a git repository", EXIT_REFUSED)
        repo = Path(proc.stdout.strip())
        data = collect(repo, args.base, args.head)
    except ToolError as exc:
        print(f"review pack failed: {exc}", file=sys.stderr)
        return exc.exit_code

    body = render_pr_body(data, args.title)
    missing = _self_check(body)
    if missing:
        print(f"refused: pack is incomplete, missing sections: {missing}",
              file=sys.stderr)
        return EXIT_REFUSED

    payload = dict(data)
    payload["pr_body"] = body
    payload["required_sections_present"] = True
    if args.out:
        Path(args.out).write_text(body, encoding="utf-8")
        print(f"review pack written to {args.out} "
              f"({len(data['changed_files'])} changed files, "
              f"{len(data['commits'])} commits, "
              f"diff --check {'clean' if data['diff_check']['clean'] else 'DIRTY'})")
    elif not args.json:
        print(body)
    if args.json:
        emit(payload, True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
