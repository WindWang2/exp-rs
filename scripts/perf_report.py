#!/usr/bin/env python3
"""perf_report.py — merge/diff Execution & Data Plane 3.0 benchmark records.

Collects the per-workload JSON files written by test_execution_benchmarks
(SICNU_EXEC_BENCH_OUT=<dir>) and either:

  * merges one directory into a single report:
      python3 scripts/perf_report.py merge <dir> --out report.json
  * diffs two collections (trend report, machine-relative):
      python3 scripts/perf_report.py diff <before_dir> <after_dir>

The diff prints wall/cpu deltas side by side and flags regressions beyond a
noise threshold (default 25% — benchmarks are trends, not gates).
"""

import argparse
import glob
import json
import os
import sys


def load_dir(path: str) -> dict:
    records = {}
    for f in sorted(glob.glob(os.path.join(path, "*.json"))):
        try:
            with open(f, encoding="utf-8") as fh:
                rec = json.load(fh)
            if rec.get("schema") == "execution-bench/1":
                records[rec["workload"]] = rec
        except (json.JSONDecodeError, OSError):
            continue
    return records


def merge(args) -> int:
    records = load_dir(args.dir)
    report = {"schema": "execution-bench-report/1", "workloads": records}
    out = args.out or os.path.join(args.dir, "report.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, sort_keys=True)
    print("merged %d workloads -> %s" % (len(records), out))
    return 0


def _fmt_delta(before: float, after: float, unit: str) -> str:
    if not before:
        return "%10.1f%s (new)" % (after, unit)
    delta = (after - before) / before * 100.0
    arrow = "↑" if delta > 0 else "↓"
    return "%10.1f%s → %10.1f%s  (%s%.1f%%)" % (before, unit, after, unit, arrow, delta)


def diff(args) -> int:
    before = load_dir(args.before)
    after = load_dir(args.after)
    names = sorted(set(before) | set(after))
    if not names:
        print("no benchmark records found", file=sys.stderr)
        return 2
    regressions = []
    for name in names:
        b, a = before.get(name), after.get(name)
        print("== %s" % name)
        if not b or not a:
            print("   %s" % ("missing in AFTER" if b else "missing in BEFORE"))
            continue
        print("   wall  %s" % _fmt_delta(b["wall_ms"], a["wall_ms"], "ms"))
        print("   cpu   %s" % _fmt_delta(b["cpu_ms"], a["cpu_ms"], "ms"))
        print("   rssd  %s" % _fmt_delta(b["peak_rss_mb"], a["peak_rss_mb"], "MB"))
        if b["wall_ms"] > 0:
            wall_delta = (a["wall_ms"] - b["wall_ms"]) / b["wall_ms"]
            if wall_delta * 100.0 > args.threshold:
                regressions.append((name, wall_delta * 100.0))
    if regressions:
        print("\nREGRESSIONS beyond +%.0f%%:" % args.threshold)
        for name, pct in regressions:
            print("  %-34s +%.1f%% wall" % (name, pct))
    else:
        print("\nno wall regressions beyond +%.0f%%" % args.threshold)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("merge", help="merge one bench dir into a report")
    m.add_argument("dir")
    m.add_argument("--out", default=None)
    m.set_defaults(func=merge)
    d = sub.add_parser("diff", help="diff two bench dirs")
    d.add_argument("before")
    d.add_argument("after")
    d.add_argument("--threshold", type=float, default=25.0,
                   help="wall regression report threshold in percent")
    d.set_defaults(func=diff)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
