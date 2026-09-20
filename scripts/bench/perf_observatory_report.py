#!/usr/bin/env python3
"""perf_observatory_report.py — merge / validate / compare / report observatory records.

The Performance Observatory writes one `sicnu-perf-observatory/1` JSON record
per workload (see `tests/perf/perf_observatory.h`). This tool is the consumer
side of that contract:

  validate <dir>                 schema + required-field check (CI gate)
  merge    <dir> --out f.json    fold a collection into one report
  compare  <before> <after>      machine-independent structural comparison
  report   <dir> --out f.md      Markdown that keeps MEASURED numbers,
                                 INFERRED mechanisms and RECOMMENDED fixes
                                 strictly apart

Design rules that fall out of the harness contract:

  * Structural indicators (scale, counts, complexity rungs, filesystem
    operation counts) must be IDENTICAL across runs on the same build. They
    are what `compare` gates on.
  * Timings (wall/cpu/rss/io) are machine-relative and never asserted equal;
    they are reported as deltas, never as gates.
  * A metric that is `null` with a reason stays null everywhere. No zeros are
    ever substituted for "unavailable".
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sys

SCHEMA = "sicnu-perf-observatory/1"

# Fields every record must carry for the schema to be usable by the report tool.
REQUIRED = (
    "schema",
    "generated_at",
    "workload",
    "environment",
    "scale",
    "measurement",
    "counts",
    "structural",
    "extra",
)

# Machine-independent structural indicators. Two runs on the same build must
# agree on these; `compare` gates on exactly this set.
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


def load_dir(path: str) -> dict:
    """Reads every `*.json` in `path`; keeps only well-formed observatory records."""
    records = {}
    for fname in sorted(glob.glob(os.path.join(path, "*.json"))):
        try:
            with open(fname, encoding="utf-8") as fh:
                rec = json.load(fh)
        except (json.JSONDecodeError, OSError):
            continue
        if isinstance(rec, dict) and rec.get("schema") == SCHEMA:
            records[rec["workload"]] = rec
    return records


def _dig(rec: dict, path: tuple):
    cur = rec
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def _norm(value):
    """Normalizes a structural value for comparison (floats compare as rounded)."""
    if isinstance(value, float):
        return round(value, 6)
    return value


# --------------------------------------------------------------------------- #
# validate
# --------------------------------------------------------------------------- #

def cmd_validate(args) -> int:
    records = load_dir(args.dir)
    problems = []
    if not records:
        print("validate: no observatory records found in %s" % args.dir)
        return 2
    for name, rec in sorted(records.items()):
        for field in REQUIRED:
            if field not in rec:
                problems.append("%s: missing field %r" % (name, field))
        env = rec.get("environment", {})
        for field in ("os", "compiler", "build_type", "cores"):
            if field not in env:
                problems.append("%s: environment missing %r" % (name, field))
        meas = rec.get("measurement", {})
        for field in ("wall_ms", "cpu_ms", "peak_rss_mb"):
            if field not in meas:
                problems.append("%s: measurement missing %r" % (name, field))
        if "io" not in meas or "available" not in meas["io"]:
            problems.append("%s: measurement.io.available missing" % name)
    for problem in problems:
        print("  SCHEMA  %s" % problem)
    print("validate: %d records, %d schema problems (schema %s)"
          % (len(records), len(problems), SCHEMA))
    return 1 if problems else 0


# --------------------------------------------------------------------------- #
# merge
# --------------------------------------------------------------------------- #

def cmd_merge(args) -> int:
    records = load_dir(args.dir)
    if not records:
        print("merge: no records in %s" % args.dir, file=sys.stderr)
        return 2
    report = {
        "schema": SCHEMA + "-report/1",
        "source_dir": os.path.abspath(args.dir),
        "workloads": records,
    }
    out = args.out or os.path.join(args.dir, "report.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, sort_keys=True)
    print("merge: %d workloads -> %s" % (len(records), out))
    return 0


# --------------------------------------------------------------------------- #
# compare — the machine-independent gate
# --------------------------------------------------------------------------- #

def cmd_compare(args) -> int:
    before = load_dir(args.before)
    after = load_dir(args.after)
    if not before or not after:
        print("compare: empty collection (before=%d after=%d)" % (len(before), len(after)),
              file=sys.stderr)
        return 2

    names = sorted(set(before) | set(after))
    identical = 0
    divergent = []
    only_in = []
    for name in names:
        b, a = before.get(name), after.get(name)
        if not b or not a:
            only_in.append((name, "missing in " + ("AFTER" if b else "BEFORE")))
            continue
        deltas = []
        for path in STRUCTURAL_KEYS:
            vb, va = _norm(_dig(b, path)), _norm(_dig(a, path))
            if vb != va:
                deltas.append("%s: %r != %r" % (".".join(path), vb, va))
        if deltas:
            divergent.append((name, deltas))
        else:
            identical += 1

    print("STRUCTURAL: %d identical, %d divergent" % (identical, len(divergent)))
    for name, deltas in divergent:
        print("  DIVERGENT %s" % name)
        for d in deltas:
            print("           %s" % d)
    for name, why in only_in:
        print("  ONLY-IN   %s (%s)" % (name, why))

    # Timing deltas are informational: reported, never gated.
    if args.trend:
        for name in sorted(set(before) & set(after)):
            b, a = before[name], after[name]
            bw = b["measurement"]["wall_ms"]
            aw = a["measurement"]["wall_ms"]
            if not bw:
                continue
            pct = (aw - bw) / bw * 100.0
            print("  trend %-42s wall %8.1f -> %8.1f ms (%+.1f%%)"
                  % (name, bw, aw, pct))

    if divergent:
        return 1
    return 0


# --------------------------------------------------------------------------- #
# report — the human deliverable
# --------------------------------------------------------------------------- #

def _fmt(value, unit=""):
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return "%.2f%s" % (value, unit)
    return "%s%s" % (value, unit)


def cmd_report(args) -> int:
    records = load_dir(args.dir)
    if not records:
        print("report: no records in %s" % args.dir, file=sys.stderr)
        return 2

    lines = []
    w = lines.append
    w("# Performance Observatory report")
    w("")
    w("Schema `%s`. Every number below belongs to exactly one of three" % SCHEMA)
    w("buckets, and the bucket is named next to it:")
    w("")
    w("- **MEASURED** — produced by the harness; reproducible on this machine.")
    w("- **INFERRED** — a mechanism read off the source; NOT a measurement.")
    w("- **RECOMMENDED** — a candidate change; IMPLICIT dependence on a")
    w("  measurement that does not exist yet is a bug, not a plan.")
    w("")
    w("Source: `%s`, %d workloads." % (os.path.abspath(args.dir), len(records)))
    w("")

    # --- environment -------------------------------------------------------
    any_rec = next(iter(records.values()))
    env = any_rec.get("environment", {})
    w("## Environment (MEASURED)")
    w("")
    w("| item | value |")
    w("|---|---|")
    w("| os | %s |" % _fmt(env.get("os")))
    w("| compiler | %s |" % _fmt(env.get("compiler")))
    w("| build type | %s |" % _fmt(env.get("build_type")))
    w("| cores | %s |" % _fmt(env.get("cores")))
    w("| cpu model | %s |" % _fmt(env.get("cpu_model")))
    w("")
    w("Timings below are **machine-relative**. They are recorded, never used")
    w("as a gate; the gates are the structural counters and complexity")
    w("exponents.")
    w("")

    # --- per workload ------------------------------------------------------
    w("## Workloads")
    for name in sorted(records):
        rec = records[name]
        m = rec.get("measurement", {})
        c = rec.get("counts", {})
        s = rec.get("structural", {})
        cx = rec.get("complexity") or {}
        w("")
        w("### %s" % name)
        w("")
        w("| metric | value |")
        w("|---|---|")
        w("| scale | %s / items=%s / probes=%s |" %
          (rec.get("scale", {}).get("kind"), _fmt(rec.get("scale", {}).get("items")),
           _fmt(rec.get("scale", {}).get("probes"))))
        w("| wall (MEASURED) | %s ms |" % _fmt(m.get("wall_ms")))
        w("| cpu (MEASURED) | %s ms |" % _fmt(m.get("cpu_ms")))
        w("| peak rss delta (MEASURED) | %s MB |" % _fmt(m.get("peak_rss_mb")))
        io = m.get("io", {})
        if io.get("available"):
            w("| read (MEASURED) | %s bytes |" % _fmt(m.get("read_bytes")))
            w("| write (MEASURED) | %s bytes |" % _fmt(m.get("write_bytes")))
        else:
            w("| io counters | unavailable: %s |" % _fmt(io.get("unavailable_reason")))
        for key, label in (("tasks_dispatched", "tasks dispatched"),
                           ("pages_requested", "requests issued"),
                           ("cache_hits", "cache hits"),
                           ("cache_misses", "cache misses"),
                           ("files_written", "artifacts written"),
                           ("rows_materialized", "rows materialized")):
            w("| %s (STRUCTURAL) | %s |" % (label, _fmt(c.get(key))))
        if cx and cx.get("exponent") is not None:
            pts = ", ".join("%s->%s" % (p.get("n"), _fmt(p.get("ms"), " ms"))
                            for p in cx.get("points", []))
            w("| complexity (MEASURED) | exponent %.2f (%s): %s |"
              % (cx["exponent"], cx.get("model", "?"), pts))
        w("")
        if s:
            w("Structural assertions:")
            w("")
            for key in sorted(s):
                w("- `%s` = %s" % (key, _fmt(s[key])))
            w("")

    w("## Findings")
    w("")
    hotspots = [n for n, r in records.items()
                if (r.get("complexity") or {}).get("exponent", 0) > 1.5
                or "hotspot" in (r.get("structural") or {})]
    if hotspots:
        for name in sorted(hotspots):
            rec = records[name]
            s = rec.get("structural", {})
            cx = rec.get("complexity") or {}
            w("### %s" % name)
            w("")
            if cx.get("exponent") is not None:
                w("- MEASURED: complexity exponent %.2f (%s) over the recorded ladder."
                  % (cx["exponent"], cx.get("model", "?")))
            if "hotspot" in s:
                w("- MEASURED: differential experiment isolating the costly branch.")
                w("- INFERRED: `%s`." % s["hotspot"])
            w("- RECOMMENDED: see `benchmarks/` baseline notes for the fix and")
            w("  the expected effect. A recommendation without a measurement")
            w("  above it is not evidence and is not made here.")
            w("")
    else:
        w("No workload exceeded the super-linear exponent threshold in this")
        w("collection; nothing is claimed.")
        w("")

    w("## Reproduction")
    w("")
    w("```")
    w("cmake --build <build> --target test_perf_observatory test_perf_io_observatory")
    w("SICNU_OBS_OUT=<dir> ctest --test-dir <build> -R 'obs (io|dataset|governance|taskcenter|temporal|tiled)' --output-on-failure")
    w("python3 scripts/bench/perf_observatory_report.py validate <dir>")
    w("python3 scripts/bench/perf_observatory_report.py compare <run1> <run2>")
    w("```")
    w("")
    w("Scale selection: `SICNU_OBS_SCALE=small|mid|scale` (default `small`).")

    out = args.out
    text = "\n".join(lines) + "\n"
    if out:
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("report: wrote %s (%d workloads)" % (out, len(records)))
    else:
        sys.stdout.write(text)
    return 0


# --------------------------------------------------------------------------- #

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("validate")
    p.add_argument("dir")
    p.set_defaults(func=cmd_validate)

    p = sub.add_parser("merge")
    p.add_argument("dir")
    p.add_argument("--out")
    p.set_defaults(func=cmd_merge)

    p = sub.add_parser("compare")
    p.add_argument("before")
    p.add_argument("after")
    p.add_argument("--trend", action="store_true",
                   help="also print (non-gated) timing deltas")
    p.set_defaults(func=cmd_compare)

    p = sub.add_parser("report")
    p.add_argument("dir")
    p.add_argument("--out")
    p.set_defaults(func=cmd_report)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
