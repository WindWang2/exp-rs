#!/usr/bin/env python3
"""ci_master_status.py — offline CI run classification for a saved
`gh run list --json ...` dump (#1491, R13-I WP-02).

The problem #1491 named: master went days without a completed CI result
because cancelled/pending runs are visually indistinguishable from real
signals in a raw run list, and a failure's STAGE (configure vs build vs
test vs infra) decides who must act. This tool classifies a saved run dump
offline — no polling, no waiting on live CI:

  * cancelled / timed_out / skipped / pending are NEVER counted as pass
  * failures are stage-classified from the failed job's step names when a
    per-run jobs dump is available (else failed-unknown, honestly)
  * --gate exits 1 unless the latest completed push run on the default
    branch classified "passed" (a cancelled wave yields NO completed run
    and therefore fails the gate — absence of evidence is not evidence of
    health)

Inputs:
  --runs runs.json     array from `gh run list --json databaseId,status,
                       conclusion,event,createdAt,headSha,headBranch`
  --jobs-dir DIR       optional per-run files DIR/<databaseId>.json holding
                       `gh api .../runs/<id>/jobs` output (used only to
                       stage-classify failures)

Usage: ci_master_status.py --runs runs.json [--jobs-dir dir] [--gate]
           [--branch master] [--event push] [--out-json summary.json]
Exit: 0 (reporter) / with --gate: 0 only when the gate holds; 2 on
malformed input (fail-closed on unreadable evidence).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = "exp.ci-status.v1"

_NON_RESULTS = {"queued", "in_progress", "waiting", "pending", "requested"}

_INFRA_TOKENS = ("checkout", "cache", "install", "vcpkg", "qt", "brew",
                 "disk", "msvc", "setup", "set up", "node", "choco",
                 "free disk", "post ")
_CONFIGURE_TOKENS = ("configure",)
# "compil" covers compile/compilation/compiling. Build is checked BEFORE
# test: the tier-1 step is literally named "Compile Core & Test Targets" and
# must classify as build, not test.
_BUILD_TOKENS = ("build", "compil")
# Short ambiguous tokens match on word boundaries: "Attest provenance" must
# not classify as a test failure via the substring "test" (review P3-8).
_TEST_TOKENS = ("ctest", "test", "execute")
_TEST_RE = re.compile(r"\b(?:" + "|".join(_TEST_TOKENS) + r")\b")


# When several jobs fail in one run, the run-level label picks the most
# action-relevant stage (a required-lane test failure outranks a platform
# build seam); the per-job detail is kept in the record's failed_jobs field.
# A job that timed out with no failed step is a hang ("timeout"), not infra.
_STAGE_PRIORITY = ["failed-test", "failed-build", "failed-configure", "infra",
                   "failed-unknown", "timeout"]


def classify_failure_stage(jobs: list[dict]) -> tuple[str, list[dict]]:
    """Stage-classify every failed job; return (run_label, per-job detail)."""
    detail = []
    for job in jobs:
        if job.get("conclusion") not in ("failure", "timed_out"):
            continue
        # No recorded failed step: a timed-out job is a hang, anything else
        # means the runner died mid-flight (infra).
        stage = "timeout" if job.get("conclusion") == "timed_out" else "infra"
        for step in job.get("steps") or []:
            if step.get("conclusion") in ("failure", "timed_out"):
                name = (step.get("name") or "").lower()
                if any(t in name for t in _CONFIGURE_TOKENS):
                    stage = "failed-configure"
                elif any(t in name for t in _BUILD_TOKENS):
                    stage = "failed-build"
                elif _TEST_RE.search(name):
                    stage = "failed-test"
                elif any(t in name for t in _INFRA_TOKENS):
                    stage = "infra"
                else:
                    stage = "failed-unknown"
                break
        detail.append({"job": job.get("name"), "class": stage})
    if not detail:
        return "failed-unknown", []
    detail.sort(key=lambda d: _STAGE_PRIORITY.index(d["class"]))
    return detail[0]["class"], detail


def classify_run(run: dict, jobs_dir: Path | None) -> tuple[str, list[dict]]:
    status = run.get("status")
    conclusion = run.get("conclusion")
    if status in _NON_RESULTS or (status != "completed" and not conclusion):
        return "pending", []
    if conclusion == "success":
        return "passed", []
    if conclusion == "cancelled":
        return "cancelled", []
    if conclusion == "timed_out":
        return "timeout", []
    if conclusion in ("skipped", "neutral", "stale"):
        return "skipped", []
    if conclusion in ("action_required", "startup_failure"):
        return "infra", []
    if conclusion == "failure":
        jobs = None
        if jobs_dir:
            jf = jobs_dir / f"{run.get('databaseId')}.json"
            if jf.is_file():
                try:
                    jobs = json.loads(jf.read_text()).get("jobs") or []
                except (json.JSONDecodeError, OSError):
                    jobs = []
        if jobs is None:
            return "failed-unknown", []
        return classify_failure_stage(jobs)
    return "unknown", []


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", required=True)
    ap.add_argument("--jobs-dir", default=None)
    ap.add_argument("--branch", default="master")
    ap.add_argument("--event", default="push")
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--out-json", default=None)
    args = ap.parse_args()

    try:
        runs = json.loads(Path(args.runs).read_text())
    except (json.JSONDecodeError, OSError) as e:
        print(f"ci_master_status: unreadable runs evidence: {e}", file=sys.stderr)
        return 2
    if not isinstance(runs, list):
        print("ci_master_status: runs evidence must be a JSON array", file=sys.stderr)
        return 2

    jobs_dir = Path(args.jobs_dir) if args.jobs_dir else None
    records = []
    for run in runs:
        if not isinstance(run, dict) or "databaseId" not in run:
            print("ci_master_status: run entry missing databaseId", file=sys.stderr)
            return 2
        cls, failed_jobs = classify_run(run, jobs_dir)
        rec = {
            "id": run.get("databaseId"),
            "event": run.get("event"),
            "branch": run.get("headBranch"),
            "sha": (run.get("headSha") or "")[:8],
            "createdAt": run.get("createdAt"),
            "class": cls,
        }
        if failed_jobs:
            rec["failed_jobs"] = failed_jobs
        records.append(rec)

    counts = Counter(r["class"] for r in records)
    scoped = [r for r in records
              if r["event"] == args.event and r["branch"] == args.branch]
    # Parse timestamps when possible (hand-edited dumps may mix offsets);
    # fall back to the raw RFC3339 string, which sorts correctly for gh's
    # canonical Z-suffixed output (review P3-8).
    def _sort_key(rec):
        raw = rec.get("createdAt") or ""
        try:
            ts = datetime.fromisoformat(raw.replace("Z", "+00:00")) \
                .astimezone(timezone.utc).timestamp()
        except ValueError:
            ts = 0.0
        return (ts, raw)
    scoped.sort(key=_sort_key, reverse=True)
    # "skipped" counts as completed here: a skipped run DID reach a terminal
    # state, it just never gates anything (never class "passed").
    completed = [r for r in scoped if r["class"] not in ("pending", "cancelled")]
    latest_completed = completed[0] if completed else None
    gate_ok = bool(latest_completed and latest_completed["class"] == "passed")

    summary = {
        "schema": SCHEMA,
        "branch": args.branch,
        "event": args.event,
        "runs": len(records),
        "counts": dict(sorted(counts.items())),
        "latest_completed": latest_completed,
        "gate": "pass" if gate_ok else "fail",
    }
    print(f"runs={len(records)} counts={dict(sorted(counts.items()))}")
    if latest_completed:
        r = latest_completed
        print(f"latest completed {args.event}->{args.branch}: "
              f"{r['class']} (id={r['id']} sha={r['sha']} at={r['createdAt']})")
        for fj in r.get("failed_jobs", []):
            print(f"  failed job: {fj['job']} -> {fj['class']}")
    else:
        print(f"latest completed {args.event}->{args.branch}: NONE "
              f"(cancelled/pending waves leave no evidence — that IS the finding)")
    print(f"gate: {'PASS' if gate_ok else 'FAIL'}")
    if args.out_json:
        Path(args.out_json).write_text(json.dumps(summary, indent=1) + "\n")
    if args.gate:
        return 0 if gate_ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
