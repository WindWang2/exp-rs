#!/usr/bin/env python3
# conformance.py — cross-platform launcher parity fixtures (lab 12.0, O5).
#
# Executes the POSIX launchers and locks their argument parsing and exit-code
# propagation contracts:
#
#   scripts/gen_samples.sh        two-token and --key=value spellings agree
#                                 with the Windows twin; the wrapper never
#                                 swallows the CLI's exit code; the forced
#                                 verify pass honors a custom --out (with
#                                 spaces/Unicode) and propagates its drift
#                                 exit (4); --help stays a successful no-op.
#   packaging/bundle/GRADE_ALL.sh same arguments/env/exit codes as
#                                 GRADE_ALL.cmd: usage → 2, missing binary →
#                                 1, CLI rc passthrough, offline env wired,
#                                 SICNU_BATCH_FLAGS forwarded.
#   packaging/bundle/VERIFY.sh    exit code of the canonical verifier is
#                                 re-exited verbatim (0 ok / 1 tamper /
#                                 2 cannot-verify).
#
# Windows-side .cmd behavior cannot be executed on a POSIX host; the .cmd
# twins are held to the same contract by construction (same arg shapes, same
# RC-preserve pattern) and reviewed statically — recorded as a known
# limitation in the PR.

import argparse
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FAILURES = []


def check(condition, label):
    if condition:
        print("  ok   %s" % label)
    else:
        print("  FAIL %s" % label)
        FAILURES.append(label)


def write_stub(path, body):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(body)
    mode = os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
    os.chmod(path, mode)


# --------------------------------------------------------------------------
# gen_samples.sh
# --------------------------------------------------------------------------

GEN_STUB = """#!/bin/sh
# Records each invocation into $GEN_STUB_LOG, creates the requested dir on
# generate calls, and applies $GEN_STUB_GEN_RC / $GEN_STUB_VERIFY_RC.
log="${GEN_STUB_LOG}"
echo "$*" >> "$log"
out=
prev=
for arg in "$@"; do
  if [ -n "$prev" ]; then out=$arg; prev=; continue; fi
  case "$arg" in
    --out=*) out=${arg#--out=} ;;
    --out) prev=1 ;;
  esac
done
case "$*" in
  *--verify*)
    [ -n "${GEN_STUB_VERIFY_RC}" ] && exit "${GEN_STUB_VERIFY_RC}"
    exit 0
    ;;
esac
[ -n "${GEN_STUB_GEN_RC}" ] && exit "${GEN_STUB_GEN_RC}"
[ -n "$out" ] && mkdir -p "$out"
exit 0
"""


def scenario_gen_samples(repo, gen_bin):
    print("scenario 1: gen_samples.sh argument + exit-code matrix")
    script = os.path.join(repo, "scripts", "gen_samples.sh")
    stub = os.path.join(tempfile.mkdtemp(prefix="genstub_"), "stub.sh")
    write_stub(stub, GEN_STUB)
    env = dict(os.environ, SICNU_GENERATE_SAMPLES=stub)

    # (a) exit propagation: generate failure (rc 1) is not swallowed.
    with tempfile.TemporaryDirectory(prefix="lp_gen1_") as root:
        proc = subprocess.run(
            [script, "--out", os.path.join(root, "a b")],
            env=dict(env, GEN_STUB_LOG=os.path.join(root, "log"),
                     GEN_STUB_GEN_RC="1"),
            capture_output=True, text=True,
        )
        check(proc.returncode == 1,
              "generate rc 1 propagates (got %d)" % proc.returncode)

    # (b) two-token + spaces/Unicode --out reaches the CLI and the verify.
    with tempfile.TemporaryDirectory(prefix="lp_gen2_") as root:
        target = os.path.join(root, "样 张 dir", "samples")
        log = os.path.join(root, "log")
        proc = subprocess.run(
            [script, "--out", target],
            env=dict(env, GEN_STUB_LOG=log),
            capture_output=True, text=True,
        )
        calls = open(log).read().splitlines() if os.path.exists(log) else []
        check(proc.returncode == 0, "two-token --out exits 0 (got %d)"
              % proc.returncode)
        check(any(c == "--out=%s" % target for c in calls),
              "two-token form rejoined to --out=<dir>")
        check(calls and calls[-1] == "--verify --out=%s" % target,
              "forced verify targets the custom --out")

    # (c) verify drift exit (4) propagates.
    with tempfile.TemporaryDirectory(prefix="lp_gen3_") as root:
        proc = subprocess.run(
            [script, "--out=%s" % os.path.join(root, "x")],
            env=dict(env, GEN_STUB_LOG=os.path.join(root, "log"),
                     GEN_STUB_VERIFY_RC="4"),
            capture_output=True, text=True,
        )
        check(proc.returncode == 4, "verify rc 4 propagates (got %d)"
              % proc.returncode)

    # (d) --help is a successful no-op (no forced verify).
    with tempfile.TemporaryDirectory(prefix="lp_gen4_") as root:
        proc = subprocess.run(
            [script, "--help"],
            env=dict(env, GEN_STUB_LOG=os.path.join(root, "log"),
                     GEN_STUB_VERIFY_RC="4"),
            capture_output=True, text=True,
        )
        check(proc.returncode == 0, "--help exits 0 despite hostile verify rc")

    # (e) trailing value-less --out is a wrapper usage error (exit 2).
    with tempfile.TemporaryDirectory(prefix="lp_gen5_") as root:
        proc = subprocess.run(
            [script, "--out"],
            env=dict(env, GEN_STUB_LOG=os.path.join(root, "log")),
            capture_output=True, text=True,
        )
        check(proc.returncode == 2, "value-less --out exits 2 (got %d)"
              % proc.returncode)

    # (f) real foundry binary: two-token form generates + verifies a real
    # sample set into a directory with spaces.
    if gen_bin and os.path.exists(gen_bin):
        with tempfile.TemporaryDirectory(prefix="lp_gen6_") as root:
            target = os.path.join(root, "samples with spaces")
            proc = subprocess.run(
                [script, "--out", target],
                env=dict(os.environ, SICNU_GENERATE_SAMPLES=gen_bin),
                capture_output=True, text=True,
                timeout=600,
            )
            check(proc.returncode == 0,
                  "real foundry two-token generate+verify exits 0 (got %d; %s)"
                  % (proc.returncode, proc.stderr[-200:]))
            check(os.path.isfile(os.path.join(target, "manifest.json")),
                  "real manifest written into spaced --out")
    else:
        print("  skip real-foundry lane (--gen-bin not provided)")


# --------------------------------------------------------------------------
# GRADE_ALL.sh
# --------------------------------------------------------------------------

CLI_STUB = """#!/usr/bin/env python3
# Records invocation contract into $CLI_STUB_REC and exits $CLI_STUB_RC.
import json, os, sys
rec = {
    "cwd": os.getcwd(),
    "args": sys.argv[1:],
    "SICNU_OFFLINE": os.environ.get("SICNU_OFFLINE"),
    "SICNU_LAB_RULES_DIR": os.environ.get("SICNU_LAB_RULES_DIR"),
    "PROJ_DATA": os.environ.get("PROJ_DATA"),
    "GDAL_DATA": os.environ.get("GDAL_DATA"),
    "QT_QPA_PLATFORM": os.environ.get("QT_QPA_PLATFORM"),
}
with open(os.environ["CLI_STUB_REC"], "w") as handle:
    json.dump(rec, handle)
sys.exit(int(os.environ.get("CLI_STUB_RC", "0")))
"""


def make_bundle(root, cli_body=None):
    bundle = os.path.join(root, "bundle")
    os.makedirs(os.path.join(bundle, "bin"), exist_ok=True)
    os.makedirs(os.path.join(bundle, "data", "labs", "grading"), exist_ok=True)
    os.makedirs(os.path.join(bundle, "data", "runtime", "proj"), exist_ok=True)
    os.makedirs(os.path.join(bundle, "data", "runtime", "gdal"), exist_ok=True)
    write_stub(os.path.join(bundle, "bin", "sicnu_geo_rs_cli"),
               cli_body or CLI_STUB)
    shutil.copy(os.path.join(root_marker_repo, "packaging", "bundle",
                             "GRADE_ALL.sh"),
                os.path.join(bundle, "GRADE_ALL.sh"))
    return bundle


root_marker_repo = None  # set by main()


def scenario_grade_all(repo):
    print("scenario 2: GRADE_ALL.sh contract (stub CLI)")
    with tempfile.TemporaryDirectory(prefix="lp_grade_") as root:
        bundle = make_bundle(root)
        script = os.path.join(bundle, "GRADE_ALL.sh")
        subs = os.path.join(root, "subm 2024")
        os.makedirs(subs, exist_ok=True)
        rec = os.path.join(root, "rec.json")

        # (a) happy path: args, defaults, env, rc passthrough.
        proc = subprocess.run(
            [script, subs], capture_output=True, text=True,
            env=dict(os.environ, CLI_STUB_REC=rec, CLI_STUB_RC="0"),
        )
        check(proc.returncode == 0, "stub rc 0 → wrapper 0 (got %d)"
              % proc.returncode)
        doc = json.load(open(rec))
        csv_target = os.path.join(subs, "grades.csv")
        check(doc["args"] == ["--offline", "lab", "--lab", "ndvi_basics",
                              "--batch", subs, "--csv", csv_target],
              "canonical args + default lab/csv (%s)" % doc["args"])
        check(doc["SICNU_OFFLINE"] == "1", "SICNU_OFFLINE=1 wired")
        check(doc["SICNU_LAB_RULES_DIR"] ==
              os.path.join(bundle, "data", "labs", "grading"),
              "SICNU_LAB_RULES_DIR points at bundle rules")
        check(doc["PROJ_DATA"] == os.path.join(bundle, "data", "runtime",
                                               "proj"),
              "PROJ_DATA wired into the bundle")
        check(doc["QT_QPA_PLATFORM"] == "offscreen", "offscreen default")

        # (b) explicit lab + csv + SICNU_BATCH_FLAGS, rc 1 passthrough.
        csv2 = os.path.join(root, "out", "grades.csv")
        proc = subprocess.run(
            [script, subs, "terrain_slope", csv2], capture_output=True,
            text=True,
            env=dict(os.environ, CLI_STUB_REC=rec, CLI_STUB_RC="1",
                     SICNU_BATCH_FLAGS="--json --html"),
        )
        check(proc.returncode == 1, "stub rc 1 → wrapper 1 (got %d)"
              % proc.returncode)
        doc = json.load(open(rec))
        check(doc["args"][3] == "terrain_slope" and doc["args"][7] == csv2,
              "explicit lab + csv forwarded")
        check(doc["args"][-2:] == ["--json", "--html"],
              "SICNU_BATCH_FLAGS forwarded (%s)" % doc["args"])

        # (c) usage errors: no args → 2, missing submissions dir → 2.
        proc = subprocess.run([script], capture_output=True, text=True,
                              env=dict(os.environ))
        check(proc.returncode == 2, "no args → 2 (got %d)" % proc.returncode)
        proc = subprocess.run([script, os.path.join(root, "nope")],
                              capture_output=True, text=True,
                              env=dict(os.environ))
        check(proc.returncode == 2, "missing submissions dir → 2")

        # (d) missing binary → 1.
        os.remove(os.path.join(bundle, "bin", "sicnu_geo_rs_cli"))
        proc = subprocess.run([script, subs], capture_output=True, text=True,
                              env=dict(os.environ))
        check(proc.returncode == 1, "missing CLI → 1 (got %d)"
              % proc.returncode)


# --------------------------------------------------------------------------
# VERIFY.sh
# --------------------------------------------------------------------------

def synthesize_bundle(root):
    """Minimal valid /1 bundle: one payload file + full manifest."""
    tools = os.path.join(root_marker_repo, "scripts",
                         "verify_bundle_manifest.py")
    payload_rel = "data/samples/landsat_sample.tif"
    payload_abs = os.path.join(root, payload_rel)
    os.makedirs(os.path.dirname(payload_abs), exist_ok=True)
    body = b"deterministic payload"
    with open(payload_abs, "wb") as handle:
        handle.write(body)
    import hashlib
    os.makedirs(os.path.join(root, "tools"), exist_ok=True)
    shutil.copy(tools, os.path.join(root, "tools", "verify_bundle_manifest.py"))
    # VERIFY.sh is copied in by the scenario after synthesis; hash the exact
    # bytes it will have so the manifest covers it (an unlisted file fails).
    verify_src = os.path.join(root_marker_repo, "packaging", "bundle",
                              "VERIFY.sh")
    verify_bytes = open(verify_src, "rb").read()
    tool_abs = os.path.join(root, "tools", "verify_bundle_manifest.py")
    tool_bytes = open(tool_abs, "rb").read()
    manifest = {
        "schema": "sicnu.offline_bundle/1",
        "bundle_version": "test",
        "created_utc": "2026-01-01T00:00:00Z",
        "size_ceiling_mb": 250,
        "required": ["data/samples/"],
        "files": [
            {"path": payload_rel, "bytes": len(body),
             "sha256": hashlib.sha256(body).hexdigest()},
            {"path": "VERIFY.sh", "bytes": len(verify_bytes),
             "sha256": hashlib.sha256(verify_bytes).hexdigest()},
            {"path": "tools/verify_bundle_manifest.py",
             "bytes": len(tool_bytes),
             "sha256": hashlib.sha256(tool_bytes).hexdigest()},
        ],
    }
    with open(os.path.join(root, "manifest.json"), "w") as handle:
        json.dump(manifest, handle, indent=2)


def scenario_verify(repo):
    print("scenario 3: VERIFY.sh exit-code propagation")
    script_src = os.path.join(root_marker_repo, "packaging", "bundle",
                              "VERIFY.sh")
    with tempfile.TemporaryDirectory(prefix="lp_verify_") as root:
        synthesize_bundle(root)
        # VERIFY.sh takes its own directory as the bundle root, so the check
        # runs against a shipped copy inside the synthesized bundle.
        script = os.path.join(root, "VERIFY.sh")
        shutil.copy(script_src, script)
        proc = subprocess.run([script], capture_output=True, text=True,
                              cwd=root)
        check(proc.returncode == 0, "valid bundle → 0 (got %d; %s)"
              % (proc.returncode, proc.stderr[-160:]))
        payload = os.path.join(root, "data", "samples",
                               "landsat_sample.tif")
        with open(payload, "ab") as handle:
            handle.write(b"tampered")
        proc = subprocess.run([script], capture_output=True, text=True,
                              cwd=root)
        check(proc.returncode == 1, "tampered bundle → 1 (got %d)"
              % proc.returncode)
        os.remove(os.path.join(root, "manifest.json"))
        proc = subprocess.run([script], capture_output=True, text=True,
                              cwd=root)
        check(proc.returncode == 2, "missing manifest → 2 (got %d)"
              % proc.returncode)


def main():
    global root_marker_repo
    argv = sys.argv[1:]
    gen_bin = None
    if "--gen-bin" in argv:
        i = argv.index("--gen-bin")
        gen_bin = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    repo = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
    if "--repo" in argv:
        i = argv.index("--repo")
        repo = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    root_marker_repo = repo
    scenario_gen_samples(repo, gen_bin)
    scenario_grade_all(repo)
    scenario_verify(repo)
    if FAILURES:
        print("FAIL: %d check(s) failed" % len(FAILURES))
        return 1
    print("all launcher-parity checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
