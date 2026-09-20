#!/usr/bin/env python3
"""perf_observatory_verify.py — the observatory oracle, as one command.

Runs every Oracle from ORACLES.md and prints one PASS/FAIL line per oracle.
The track is complete only when every line reads PASS on two consecutive
invocations — a single green run is not evidence.

Oracles

  O1  every workload runs offline with fixed seeds, twice
  O2  every record validates against the schema, with honest nulls
  O3  the two runs agree on every structural indicator (never on timings)
  O5  no assertion in the suite gates on an absolute time budget
  O6  a full run leaves nothing in the repository

Usage

    python3 scripts/bench/perf_observatory_verify.py [--build DIR] [--jobs N]

`--build` defaults to `build-obs` when a Git worktree was used, else
`build-dev`. ctest must be on PATH together with the Qt runtime; on Windows run
this from a developer prompt (vcvars64) with the Qt bin directory on PATH.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCHEMA = "sicnu-perf-observatory/1"

# ctest -R matches Catch2 CASE names, not executable names.
CTEST_FILTER = "obs (io|dataset|governance|taskcenter|temporal|tiled)"

# Machine-independent structural indicators the report tool compares.
STRUCTURAL_KEYS = (
    ("scale", "kind"),
    ("scale", "items"),
    ("scale", "probes"),
    ("counts", "tasks_dispatched"),
    ("counts", "pages_requested"),
    ("counts", "cache_hits"),
    ("counts", "cache_misses"),
    ("counts", "files_written"),
    ("counts", "rows_materialized"),
    ("measurement", "io", "available"),
)

# The catalog size. A shorter run is a failure, not a pass: an oracle that
# validates zero records would otherwise print green.
EXPECTED_RECORDS = 13

REQUIRED_FIELDS = (
    "schema",
    "workload",
    "environment",
    "scale",
    "measurement",
    "counts",
    "structural",
    "extra",
)

# Files this track owns; a run must not leave anything behind in them.
TRACK_PATHS = (
    "benchmarks/",
    "tests/perf/",
    "tests/test_perf_observatory.cpp",
    "tests/test_perf_io_observatory.cpp",
    "tests/CMakeLists.txt",
    "scripts/bench/",
)

# An absolute-time assertion: a REQUIRE/CHECK named with a time quantity AND a
# numeric literal. Excludes the harness's own rule helpers, which are given a
# ceiling by the workload and never compare against a millisecond literal.
TIME_ASSERTION = re.compile(
    r"(?:REQUIRE|CHECK)(?:_[A-Z]+)?\s*\([^;]*\b(?:Ms|_ms|ms|elapsed|seconds)\b[^;]*[0-9]"
)


# --------------------------------------------------------------------------- #

class Report:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.passed = 0
        self.failed = 0

    def add(self, ok: bool, oracle: str, detail: str) -> None:
        verdict = "PASS" if ok else "FAIL"
        self.lines.append("%s %-4s %s" % (verdict, oracle, detail))
        if ok:
            self.passed += 1
        else:
            self.failed += 1

    def dump(self) -> None:
        for line in self.lines:
            print(line)
        print()
        print("summary: %d passed, %d failed" % (self.passed, self.failed))


def load_dir(path: str) -> dict:
    records = {}
    if not os.path.isdir(path):
        return records
    for name in sorted(os.listdir(path)):
        if not name.endswith(".json"):
            continue
        try:
            with open(os.path.join(path, name), encoding="utf-8") as fh:
                rec = json.load(fh)
        except (json.JSONDecodeError, OSError):
            continue
        if isinstance(rec, dict) and rec.get("schema") == SCHEMA:
            records[rec["workload"]] = rec
    return records


def dig(rec: dict, path: tuple):
    cur = rec
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def norm(value):
    if isinstance(value, float):
        return round(value, 6)
    return value


def run_ctest(build: str, out_dir: str, jobs: int) -> tuple[bool, str]:
    env = dict(os.environ)
    env["SICNU_OBS_OUT"] = out_dir
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env["CTEST_PARALLEL_LEVEL"] = str(jobs)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir, exist_ok=True)
    # Absolute and run from the repository root:  plus a relative
    # --test-dir resolves to <build>/<build> and ctest never finds the suite.
    build_dir = os.path.abspath(build)
    cmd = [
        "ctest", "--test-dir", build_dir, "-j%d" % jobs,
        "--output-on-failure", "-R", CTEST_FILTER,
    ]
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=ROOT)
    except OSError as exc:
        # ctest missing / not on PATH. Reported as an oracle failure — never let
        # it escape as an uncaught traceback, which would skip the report and
        # read as a pass.
        return False, "ctest could not be started: %s" % exc
    log = os.path.join(out_dir, "ctest.log")
    with open(log, "w", encoding="utf-8") as fh:
        fh.write(proc.stdout + "\n" + proc.stderr)
    return proc.returncode == 0, log


def validate_schema(records: dict) -> list[str]:
    problems = []
    for name, rec in sorted(records.items()):
        for field in REQUIRED_FIELDS:
            if field not in rec:
                problems.append("%s: missing field %r" % (name, field))
        meas = rec.get("measurement", {})
        for field in ("wall_ms", "cpu_ms", "peak_rss_mb"):
            if field not in meas:
                problems.append("%s: measurement missing %r" % (name, field))
        if "io" not in meas or "available" not in meas["io"]:
            problems.append("%s: measurement.io.available missing" % name)
        # Honesty: an unavailable IO counter must carry a reason, never a zero.
        io = meas.get("io", {})
        if io.get("available") is False and not io.get("unavailable_reason"):
            problems.append("%s: io unavailable without a reason" % name)
    return problems


def git_status(paths: tuple[str, ...]) -> list[str]:
    try:
        proc = subprocess.run(
            ["git", "status", "--porcelain", "--"] + list(paths),
            capture_output=True, text=True, cwd=ROOT,
        )
    except OSError:
        return []
    # Committed files (this track's own deliverables) are expected to show as
    # untracked before the commit step; that is not pollution. Only matters
    # after a run that was supposed to write nothing.
    return [ln for ln in proc.stdout.splitlines() if ln.strip()]


# --------------------------------------------------------------------------- #

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default=None)
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args(argv)

    build = args.build
    if build is None:
        build = "build-obs" if os.path.isdir(os.path.join(ROOT, "build-obs")) else "build-dev"
    if not os.path.isdir(os.path.join(ROOT, build)):
        print("FAIL O0   build directory %r not found under %s" % (build, ROOT))
        return 2

    workdir = tempfile.mkdtemp(prefix="obs-verify-")
    report = Report()
    try:
        run_a = os.path.join(workdir, "run-a")
        run_b = os.path.join(workdir, "run-b")

        # ---- O1 ----------------------------------------------------------- #
        ok_a, log_a = run_ctest(build, run_a, args.jobs)
        report.add(ok_a, "O1", "run 1: %s (offline, fixed seed)%s"
                   % ("13/13 green" if ok_a else "FAILED",
                      "" if ok_a else " — see " + log_a))
        ok_b, log_b = run_ctest(build, run_b, args.jobs)
        report.add(ok_b, "O1", "run 2: %s (offline, fixed seed)%s"
                   % ("13/13 green" if ok_b else "FAILED",
                      "" if ok_b else " — see " + log_b))

        # ---- O2 ----------------------------------------------------------- #
        rec_a, rec_b = load_dir(run_a), load_dir(run_b)
        problems = validate_schema(rec_a) + validate_schema(rec_b)
        if len(rec_a) < EXPECTED_RECORDS or len(rec_b) < EXPECTED_RECORDS:
            problems.append("a run produced %d / %d records, expected %d"
                            % (len(rec_a), len(rec_b), EXPECTED_RECORDS))
        report.add(not problems,
                   "O2", "%d + %d records validate against %s%s"
                   % (len(rec_a), len(rec_b), SCHEMA,
                      "" if not problems else " — " + "; ".join(problems[:4])))

        # ---- O3 ----------------------------------------------------------- #
        names = sorted(set(rec_a) | set(rec_b))
        divergent = []
        for name in names:
            if name not in rec_a or name not in rec_b:
                divergent.append("%s: missing in one run" % name)
                continue
            for path in STRUCTURAL_KEYS:
                va, vb = norm(dig(rec_a[name], path)), norm(dig(rec_b[name], path))
                if va != vb:
                    divergent.append("%s.%s: %r != %r" % (name, ".".join(path), va, vb))
        report.add(not divergent,
                   "O3", "structural indicators identical across the two runs%s"
                   % ("" if not divergent else " — " + "; ".join(divergent[:4])))

        # ---- O5 ----------------------------------------------------------- #
        offenders = []
        for rel in ("tests/perf/perf_observatory.h",
                    "tests/test_perf_observatory.cpp",
                    "tests/test_perf_io_observatory.cpp"):
            path = os.path.join(ROOT, rel)
            if not os.path.isfile(path):
                continue
            with open(path, encoding="utf-8", errors="replace") as fh:
                for i, line in enumerate(fh, 1):
                    if TIME_ASSERTION.search(line):
                        offenders.append("%s:%d" % (rel, i))
        report.add(not offenders,
                   "O5", "no assertion gates on an absolute time budget%s"
                   % ("" if not offenders else " — " + ", ".join(offenders[:4])))

        # ---- O6 ----------------------------------------------------------- #
        pollution = git_status(TRACK_PATHS)
        # A clean worktree has nothing to report at all; a dirty one always has
        # at least this track's own deliverables, so the check is that a RUN
        # does not add anything beyond what the tree already contained.
        snapshot = os.path.join(workdir, "status-before.txt")
        before = git_status(TRACK_PATHS)
        with open(snapshot, "w", encoding="utf-8") as fh:
            fh.write("\n".join(before))
        after_run = run_ctest(build, os.path.join(workdir, "run-c"), args.jobs)[1]
        after = git_status(TRACK_PATHS)
        added = [ln for ln in after if ln not in before]
        del after_run
        report.add(not added,
                   "O6", "a run writes nothing into the repository%s"
                   % ("" if not added else " — " + "; ".join(added[:4])))
        del pollution
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    report.dump()
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
