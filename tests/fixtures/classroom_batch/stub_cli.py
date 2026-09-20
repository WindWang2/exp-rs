#!/usr/bin/env python3
# stub_cli.py — deterministic fake `sicnu_geo_rs_cli lab --grade` worker for
# classroom-batch fixtures. Behavior is selected by keywords in the artifact
# file name (see BEHAVIORS below); transcripts mimic sicnu.lab.grade/1.
#
# Optional env:
#   STUB_PROBE_DIR  — if set, a "<pid>.open" probe file is created on entry and
#                     removed before exit (concurrency-cap observation).
#   STUB_WORK_MS    — simulated work per call (default 0).

import json
import os
import signal
import sys
import time


def emit(verdict, score, deductions=None):
    doc = {
        "schema": "sicnu.lab.grade/1",
        "digest": "0" * 64,
        "report": {
            "verdict": verdict,
            "score": score,
            "passingScore": 60.0,
            "deductions": deductions or [],
        },
    }
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(doc, handle)


def fail_usage(message):
    sys.stderr.write("lab: %s\n" % message)
    sys.exit(2)


argv = sys.argv[1:]
if "--artifact" not in argv or "--out" not in argv:
    fail_usage("stub needs --artifact and --out")
out_path = argv[argv.index("--out") + 1]
artifact = argv[argv.index("--artifact") + 1]
name = os.path.basename(artifact).lower()

probe_dir = os.environ.get("STUB_PROBE_DIR")
if probe_dir:
    os.makedirs(probe_dir, exist_ok=True)
    with open(os.path.join(probe_dir, "%d.open" % os.getpid()), "w"):
        pass

work_ms = int(os.environ.get("STUB_WORK_MS", "0"))
if work_ms:
    time.sleep(work_ms / 1000.0)

try:
    if "pass" in name:
        emit("pass", 100.0)
        sys.exit(0)
    if "fail" in name:
        emit("fail", 40.0, [{"assertion_id": "stats.mean", "weight": 30}])
        sys.exit(1)
    if "corrupt" in name:
        sys.stderr.write("lab: cannot open %s as a raster\n" % name)
        sys.exit(3)
    if "usage" in name:
        fail_usage("unknown lab 'nope' (stub)")
    if "crash" in name:
        sys.stderr.write("stub: simulating SIGSEGV\n")
        os.kill(os.getpid(), signal.SIGSEGV)
        time.sleep(5)
        sys.exit(-1)
    if "hang" in name:
        time.sleep(300)
        sys.exit(0)
    fail_usage("stub: no behavior keyword in %r" % name)
finally:
    if probe_dir:
        try:
            os.remove(os.path.join(probe_dir, "%d.open" % os.getpid()))
        except OSError:
            pass
