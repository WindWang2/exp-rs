#!/usr/bin/env python3
"""collect_readiness.py — release-readiness report (task I, Verification
Platform 8.0).

Aggregates LOCAL evidence into a machine-readable + human-readable report:

  * compiled capabilities   — which verification/bench executables exist in
                              the build tree (compiled, not necessarily run)
  * executed evidence       — per-lane/per-item verdicts from the
                              verification ladder's results JSON
                              (passed / failed / not-built / timeout /
                              skipped — "not run" is NEVER reported as pass)
  * benchmark snapshots     — the recorded quality7/scale8 JSON artifacts
  * compatibility caveats   — documented platform gaps (from the ladder host
                              + the docs) carried into the report verbatim

Usage:
    python3 scripts/collect_readiness.py --build-dir build \
        [--ladder-results ladder.json] [--out-md docs/verification/READINESS.md] \
        [--out-json docs/verification/READINESS.json]

Exit code 0 always (this is a reporter, not a gate); the report's own
"overall" field is the honest verdict.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = "exp.readiness.report.v1"

# The verification platform's named capabilities. Each maps to the artifact
# whose PRESENCE means "compiled here" and whose ladder verdict means
# "executed here". Derived from scripts/verification_ladder.py LANES.
CAPABILITIES = {
    "header-self-containment": ("sicnu_header_probes", "L0:header_probes"),
    "trace-contract": ("test_trace_contract", "L1:trace_contract"),
    "fault-registry": ("test_fault_registry", "L1:fault_registry"),
    "diagnostic-report": ("test_diagnostic_report", "L1:diagnostic_report"),
    "fuzz-resource-uri": ("test_contract_fuzz_io", "L2:fuzz_io"),
    "fuzz-condition-ast": ("test_contract_fuzz_lang", "L2:fuzz_lang"),
    "fuzz-dataset-manifest": ("test_contract_fuzz_data", "L2:fuzz_data"),
    "known-answer-corpus": ("test_known_answer_corpus", "L2:known_answer_corpus"),
    "portability-contract": ("test_portability_contract", "L2:portability_contract"),
    "fuzz-worker-ipc-splits": ("test_contract_fuzz_ipc", "L2:fuzz_ipc"),
    "fuzz-operator-schemas": ("test_contract_fuzz_ops", "L4:fuzz_ops"),
    "known-answer-corpus-8": ("test_known_answer_corpus_8", "L2:known_answer_corpus_8"),
    "trace-chain-8": ("test_trace_chain_8", "L1:trace_chain_8"),
    "io-uri": ("test_io_uri", "L3:io_uri"),
    "io-paths": ("test_io_paths", "L3:io_paths"),
    "io-range-cache": ("test_io_range_cache", "L3:io_range_cache"),
    "io-remote-range": ("test_io_remote_range", "L3:io_remote_range"),
    "io-remote-validator": ("test_io_remote_validator", "L3:io_remote_validator"),
    "io-atomic-failures": ("test_io_atomic_failures", "L3:io_atomic_failures"),
    "io-raster-contract": ("test_io_raster_contract", "L3:io_raster_contract"),
    "io-grid-descriptor": ("test_io_grid_descriptor", "L3:io_grid_descriptor"),
    "io-stac": ("test_io_stac", "L3:io_stac"),
    "portable-fault-matrix": ("test_fault_matrix", "L4:fault_matrix"),
    "sdk-ipc-contract": ("test_exprs_ipc", "L4:exprs_ipc"),
    "concurrency-stress": ("test_concurrency_stress", "L5:concurrency_stress"),
    "posix-fault-injection": ("test_fault_injection", "L5:fault_injection_posix"),
    "worker-host-lifecycle": ("test_worker_host", "L5:worker_host"),
    "visual-cartography": ("test_mapspec", "L6:mapspec_visual"),
    "bench-quality7": ("benchmark_quality7", "L7:quality7"),
    "bench-scale8": ("benchmark_scale8", "L7:scale8"),
}

# Documented, honest compatibility caveats — copied into every report so the
# document never silently equates "not run here" with "supported".
PLATFORM_CAVEATS = [
    "Windows/MSVC: documented from dev-workstation evidence "
    "(docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host "
    "unless it is Windows.",
    "macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed "
    "on this host.",
    "Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow "
    "the CMake option surface of this build tree; absent targets are "
    "reported as not-built, never as passing.",
]


def binary_path(build_dir: Path, name: str) -> Path | None:
    # sicnu_header_probes is an aggregate of OBJECT libraries (compile-only
    # guards, no executable) — detect its compiled objects instead.
    if name == "sicnu_header_probes":
        probes = sorted(build_dir.glob("tests/CMakeFiles/header_probe_*.dir/*/*.o"))
        return probes[0] if probes else None
    for candidate in (build_dir / "tests" / name, build_dir / name,
                      build_dir / "tests" / "Release" / name,
                      build_dir / "tests" / "Debug" / name,
                      build_dir / "Release" / name,
                      build_dir / "Debug" / name):
        if candidate.is_file() and candidate.exists():
            return candidate
    return None


def git_sha() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                              text=True, timeout=15).stdout.strip()
    except (subprocess.TimeoutExpired, OSError):
        return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--ladder-results", default=None,
                        help="verification_ladder.py --json output")
    parser.add_argument("--out-md", default="docs/verification/READINESS.md")
    parser.add_argument("--out-json", default="docs/verification/READINESS.json")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    ladder = {}
    if args.ladder_results and Path(args.ladder_results).is_file():
        ladder = json.loads(Path(args.ladder_results).read_text())

    # Flatten the ladder results into "LX:label" -> status.
    verdicts: dict[str, dict] = {}
    for lane, lane_result in (ladder.get("lanes") or {}).items():
        for item in lane_result.get("items", []):
            verdicts[f"{lane}:{item['label']}"] = item

    bench = {}
    for name, spellings in (("quality7", ("quality7.json", "benchmark_quality7.json")),
                            ("scale8", ("scale8.json", "benchmark_scale8.json"))):
        for spelling in spellings:
            candidate = build_dir / "benchmarks" / spelling
            if candidate.is_file():
                try:
                    bench[name] = json.loads(candidate.read_text())
                except json.JSONDecodeError:
                    bench[name] = {"error": "unreadable JSON"}
                break

    capabilities = []
    counts = {"compiled": 0, "passed": 0, "failed": 0, "not_built": 0,
              "skipped": 0, "timeout": 0, "no_evidence": 0}
    for capability, (binary, verdict_key) in CAPABILITIES.items():
        path = binary_path(build_dir, binary)
        compiled = path is not None
        if compiled:
            counts["compiled"] += 1
        item = verdicts.get(verdict_key) if verdict_key else None
        if item:
            status = item.get("status", "no_evidence")
        elif verdict_key and ladder:
            status = "not_built" if not compiled else "skipped"
        else:
            status = "compiled" if compiled else "not_built"
        if status == "passed":
            counts["passed"] += 1
        elif status in ("failed",):
            counts["failed"] += 1
        elif status in ("not-built", "not_built"):
            counts["not_built"] += 1
        elif status in ("skipped", "no_evidence", "compiled"):
            counts["skipped" if status == "skipped" else "no_evidence"] += 1
        elif status == "timeout":
            counts["timeout"] += 1
        capabilities.append({
            "capability": capability,
            "artifact": binary,
            "compiled_here": compiled,
            "executed_status": status,
        })

    # "ready" requires EVERY named capability to have executed and passed on
    # this host; anything else is honestly downgraded (not-run is never pass).
    not_passed = counts["failed"] + counts["timeout"] + counts["not_built"] \
        + counts["skipped"] + counts["no_evidence"]
    if counts["passed"] == len(capabilities) and not_passed == 0:
        overall = "ready"
    elif counts["failed"] or counts["timeout"]:
        overall = "attention"
    else:
        overall = "insufficient-evidence"

    report = {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "git_sha": git_sha(),
        "build_dir": str(build_dir),
        "host": {"platform": sys.platform},
        "counts": counts,
        "overall": overall,
        "principle": "not-run is never pass; every capability names its evidence",
        "capabilities": capabilities,
        "benchmarks": bench,
        "platform_caveats": PLATFORM_CAVEATS,
        "ladder_run_utc": ladder.get("finished_utc") if ladder else None,
    }

    out_json = Path(args.out_json)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(report, indent=1))

    lines = [
        "# Release Readiness — local verification evidence",
        "",
        f"* Generated (UTC): {report['generated_utc']}",
        f"* Git SHA: `{report['git_sha']}`",
        f"* Host: {report['host']['platform']}",
        f"* **Overall: {overall.upper()}**",
        f"* Counts: {json.dumps(counts)}",
        "",
        "| Capability | Artifact | Compiled here | Executed status |",
        "|---|---|---|---|",
    ]
    for cap in capabilities:
        lines.append(f"| {cap['capability']} | `{cap['artifact']}` "
                     f"| {'yes' if cap['compiled_here'] else 'NO'} "
                     f"| {cap['executed_status']} |")
    lines += ["", "## Benchmarks (evidence snapshots, never gates)", ""]
    if bench:
        for name, data in bench.items():
            measurements = data.get("measurements", [])
            lines.append(f"### {name} (schema {data.get('schema', '?')})")
            lines.append("")
            for m in measurements:
                lines.append(f"* `{m.get('name')}`: {m.get('ops_per_s')} ops/s "
                             f"({m.get('iterations')} iters)")
            lines.append("")
    else:
        lines.append("_No benchmark artifacts found in the build tree._")
        lines.append("")
    lines += ["## Platform caveats", ""]
    lines += [f"* {c}" for c in PLATFORM_CAVEATS]
    lines += ["", "> This report equates nothing: `not-built`, `skipped`, `timeout`, "
              "`failed` and `passed` are distinct verdicts and stay distinct."]
    Path(args.out_md).write_text("\n".join(lines) + "\n")

    print(f"readiness: overall={overall} counts={json.dumps(counts)}")
    print(f"  {out_json}")
    print(f"  {args.out_md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
