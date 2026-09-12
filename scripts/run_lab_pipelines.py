#!/usr/bin/env python3
# scripts/run_lab_pipelines.py — D3 lab track headless run harness.
#
# Runs the four lab pipelines through build/sicnu_geo_rs_cli --pipeline with
# QT_QPA_PLATFORM=offscreen, then harvests per-step result payloads from the
# newest workflow checkpoints (resume machinery, #668) into the sidecar files
# scripts/verify_lab_outputs.py expects:
#   data/labs/_tmp/out/lab10/ppi_result.json      (rs:endmember_extraction)
#   data/labs/_tmp/out/lab11/threshold_stats.json (rs:threshold_raster)
#
# Usage: python3 scripts/run_lab_pipelines.py [--cli build/sicnu_geo_rs_cli]

import argparse
import glob
import json
import os
import subprocess
import sys
import time

PIPELINES = [
    ("temporal", "data/labs/pipelines/lab8_temporal_analysis.pipeline.json"),
    ("sar", "data/labs/pipelines/lab9_sar_processing.pipeline.json"),
    ("hyperspectral", "data/labs/pipelines/lab10_hyperspectral_analysis.pipeline.json"),
    ("cartography", "data/labs/pipelines/lab11_cartographic_mapping.pipeline.json"),
]


def newest_checkpoint(before_ts, operator_id):
    """Newest stepPlans[].resultPayload for operator_id among checkpoint run
    records (the checkpoint manager lives in ~/.rs_studio/checkpoints/history)."""
    best = None
    pattern = os.path.expanduser("~/.rs_studio/checkpoints/**/checkpoint_*.json")
    for ckpt in glob.glob(pattern, recursive=True):
        if os.path.getmtime(ckpt) < before_ts:
            continue
        try:
            doc = json.load(open(ckpt))
        except (json.JSONDecodeError, OSError):
            continue
        for plan in doc.get("stepPlans", []):
            payload = plan.get("resultPayload")
            if not payload:
                continue
            if plan.get("operatorId") == operator_id:
                cand = (os.path.getmtime(ckpt), ckpt, payload)
                if best is None or cand[0] > best[0]:
                    best = cand
    return best


def run_one(cli, pipeline, env, log_dir, name):
    ts = time.time()
    print(f"=== [{name}] {pipeline}")
    proc = subprocess.run(
        [cli, "--pipeline", pipeline],
        env=env, capture_output=True, text=True,
        timeout=1800,
    )
    log = os.path.join(log_dir, f"run_{name}.log")
    with open(log, "w") as fh:
        fh.write(proc.stdout)
        fh.write(proc.stderr)
    ok = proc.returncode == 0 and "Pipeline succeeded" in proc.stdout
    print(f"    exit={proc.returncode} success_line={'yes' if ok else 'NO'} log={log}")
    return ok, ts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default="build/sicnu_geo_rs_cli")
    args = ap.parse_args()

    if not os.path.exists(args.cli):
        sys.exit(f"CLI binary not found: {args.cli} (build first)")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    log_dir = ".planning/lab-content-expansion/logs"
    os.makedirs(log_dir, exist_ok=True)
    for d in ("lab8", "lab9", "lab10", "lab11"):
        os.makedirs(f"data/labs/_tmp/out/{d}", exist_ok=True)

    failed = []
    for name, pipeline in PIPELINES:
        ok, ts = run_one(args.cli, pipeline, env, log_dir, name)
        if not ok:
            failed.append(name)
            continue

        if name == "hyperspectral":
            hit = newest_checkpoint(ts, "rs:endmember_extraction")
            if hit:
                out = "data/labs/_tmp/out/lab10/ppi_result.json"
                json.dump(hit[2], open(out, "w"), indent=1)
                print(f"    harvested {out} from {os.path.basename(hit[1])}")
            else:
                print("    WARN: no PPI checkpoint payload found")
        if name == "cartography":
            hit = newest_checkpoint(ts, "rs:threshold_raster")
            if hit:
                out = "data/labs/_tmp/out/lab11/threshold_stats.json"
                json.dump(hit[2], open(out, "w"), indent=1)
                print(f"    harvested {out} from {os.path.basename(hit[1])}")
            else:
                print("    WARN: no threshold checkpoint payload found")

    if failed:
        print(f"\nFAILED pipelines: {failed}")
        return 1
    print("\nAll four pipelines succeeded.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
