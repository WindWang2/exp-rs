#!/usr/bin/env python3
"""verification_ladder.py — layered local verification runner (task B,
Verification Platform 8.0).

One command that runs the repo's verification layers in resource-bounded,
resumable, machine-readable order — no online CI, no network:

    L0 compile-guards     header self-containment probes (sicnu_header_probes)
    L1 unit-core          observability/runtime unit suites
    L2 contract-known     contract fuzz + known-answer corpus + portability
    L3 integration-io     geospatial I/O contract suites (range cache, STAC,
                          atomic failures, raster contracts)
    L4 portability        platform/GDAL contracts (portable fault matrix,
                          external-process bridge)
    L5 stress-lifecycle   concurrency stress + POSIX fault injection +
                          worker host lifecycle
    L6 visual-optional    offscreen cartography/visual suites (skippable)
    L7 benchmarks         benchmark_quality7 + benchmark_scale8 (JSON out)
    L8 full-sweep         the whole ctest registration (bounded, long)

Design rules (mirrors TEST_INFRA.md + docs/verification/PLATFORM_EVIDENCE.md):
  * Test EXECUTABLES run directly (Catch2 exit code = lane verdict), never
    via the 3000-case ctest discovery, except L8 which uses ctest itself.
  * Environment governance from TEST_INFRA.md is applied here: offscreen
    Qt, compose IM, /usr/lib first on LD_LIBRARY_PATH, PYTHONHOME pinned to
    the configured interpreter's base prefix (CTestCustom.cmake policy).
  * Every lane item records one of: passed / failed / not-built / timeout /
    skipped. "not-built" and "skipped" are NEVER reported as pass.
  * Resumable: per-lane state persists in the state file; --resume skips
    lanes recorded passed in a previous invocation on the same build tree.
  * Bounded resources: tests run sequentially (one process at a time),
    builds use a bounded job count, every item has a hard timeout.

Usage:
    python3 scripts/verification_ladder.py --build-dir build \
        [--lanes L0,L1,L2] [--until L5] [--resume] [--json out.json] \
        [--build-jobs 8] [--timeout-scale 1.0] [--list] [--strict]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = "exp.verification.ladder.v1"

# ---------------------------------------------------------------------------
# Lane definitions. Each item: (label, executable-or-target, kind, timeout_s)
# kind: "target" (cmake --build target), "test" (run Catch2 executable),
#       "ctest" (ctest -U <regex>), "bench" (executable with --out JSON).
# Timeouts are generous upper bounds, not sleep-based waits: a healthy suite
# finishes far below them; the bound exists so a hang cannot eat the host.
# ---------------------------------------------------------------------------

LANES: dict[str, dict] = {
    "L0": {
        "title": "compile-guards",
        "description": "header self-containment probes (failure class F1)",
        "items": [("header_probes", "sicnu_header_probes", "target", 600)],
    },
    "L1": {
        "title": "unit-core",
        "description": "observability/runtime unit suites",
        "items": [
            ("trace_contract", "test_trace_contract", "test", 120),
            ("fault_registry", "test_fault_registry", "test", 120),
            ("diagnostic_report", "test_diagnostic_report", "test", 60),
            ("trace_chain_8", "test_trace_chain_8", "test", 300),
        ],
    },
    "L2": {
        "title": "contract-known",
        "description": "bounded contract fuzz + known-answer corpus + portability",
        "items": [
            ("fuzz_io", "test_contract_fuzz_io", "test", 180),
            ("fuzz_lang", "test_contract_fuzz_lang", "test", 180),
            ("fuzz_data", "test_contract_fuzz_data", "test", 180),
            ("known_answer_corpus", "test_known_answer_corpus", "test", 180),
            ("portability_contract", "test_portability_contract", "test", 180),
            ("fuzz_ipc", "test_contract_fuzz_ipc", "test", 180),
            ("known_answer_corpus_8", "test_known_answer_corpus_8", "test", 180),
            # Contract Platform 9.0 (unified contract projection guards):
            # implementation↔schema equality, command/help/action reference
            # graph, diagnostics census, capability floors, and the
            # mutation-proven scanners + snapshot freshness.
            ("contract_platform_9", "test_contract_platform_9", "test", 300),
            ("contract_projection_9", "test_contract_projection_9", "test", 300),
            ("command_contract_9", "test_command_contract_9", "test", 180),
            ("diagnostics_contract_9", "test_diagnostics_contract_9", "test", 180),
            ("capability_contract_9", "test_capability_contract_9", "test", 180),
        ],
    },
    "L3": {
        "title": "integration-io",
        "description": "geospatial I/O contract suites",
        "items": [
            ("io_uri", "test_io_uri", "test", 180),
            ("io_paths", "test_io_paths", "test", 180),
            ("io_range_cache", "test_io_range_cache", "test", 300),
            ("io_remote_range", "test_io_remote_range", "test", 300),
            ("io_remote_validator", "test_io_remote_validator", "test", 180),
            ("io_atomic_failures", "test_io_atomic_failures", "test", 300),
            ("io_raster_contract", "test_io_raster_contract", "test", 300),
            ("io_grid_descriptor", "test_io_grid_descriptor", "test", 180),
            ("io_stac", "test_io_stac", "test", 180),
        ],
    },
    "L4": {
        "title": "portability",
        "description": "portable fault matrix + platform bridges",
        "items": [
            ("fault_matrix", "test_fault_matrix", "test", 300),
            ("exprs_ipc", "test_exprs_ipc", "test", 300),
            ("fuzz_ops", "test_contract_fuzz_ops", "test", 300),
        ],
    },
    "L5": {
        "title": "stress-lifecycle",
        "description": "concurrency stress, POSIX fault injection, worker host",
        "items": [
            ("concurrency_stress", "test_concurrency_stress", "test", 600),
            ("fault_injection_posix", "test_fault_injection", "test", 300),
            ("worker_host", "test_worker_host", "test", 600),
        ],
    },
    "LS": {
        "title": "sanitizer",
        "description": "ASan-instrumented core suites in a separate sanitizer "
                       "build tree (pass --asan-build-dir; without one the "
                       "lane reports not-built — it is never silently green)",
        "optional": True,
        "items": [
            ("asan_trace_contract", "test_trace_contract", "asan-test", 300),
            ("asan_contract_projection_9", "test_contract_projection_9",
             "asan-test", 600),
            ("asan_fault_registry", "test_fault_registry", "asan-test", 300),
        ],
    },
    "L6": {
        "title": "visual-optional",
        "description": "offscreen visual suites (skippable without loss of honor)",
        "optional": True,
        "items": [
            ("mapspec_visual", "test_mapspec", "test", 600),
        ],
    },
    "L7": {
        "title": "benchmarks",
        "description": "micro + scale baselines (evidence, never a gate)",
        "items": [
            ("quality7", "benchmark_quality7", "bench", 600),
            ("scale8", "benchmark_scale8", "bench", 1800),
            ("contract9", "benchmark_contract9", "bench", 300),
        ],
    },
    "L8": {
        "title": "full-sweep",
        "description": "entire ctest registration (long; the ladder's backstop)",
        "optional": True,
        "items": [("ctest_all", ".*", "ctest", 14400)],
    },
}

LANE_ORDER = ["L0", "L1", "L2", "L3", "L4", "L5", "LS", "L6", "L7", "L8"]


def find_binary(build_dir: Path, name: str) -> Path | None:
    """Locate a test/bench executable in the build tree (tests/ first, then
    the build root and per-module dirs — matches the tree's output layouts)."""
    candidates = [
        build_dir / "tests" / name,
        build_dir / name,
        build_dir / "tests" / "support" / name,
        # multi-config generators (Visual Studio / Xcode) nest per-config dirs
        build_dir / "tests" / "Release" / name,
        build_dir / "tests" / "Debug" / name,
        build_dir / "Release" / name,
        build_dir / "Debug" / name,
    ]
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    return None


def build_target(build_dir: Path, target: str, jobs: int) -> tuple[str, str]:
    cmd = ["cmake", "--build", str(build_dir), "--target", target, "--parallel", str(jobs)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return "failed", (proc.stdout + proc.stderr)[-4000:]
    return "passed", ""


def governed_env(build_dir: Path) -> dict[str, str]:
    """TEST_INFRA.md environment policy, applied for direct binary runs."""
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_IM_MODULE"] = "compose"
    env["XMODIFIERS"] = "@im=none"
    # /usr/lib first (conda libxml2 shadowing policy, #730).
    ld = env.get("LD_LIBRARY_PATH", "")
    parts = [p for p in ld.split(os.pathsep) if p]
    if "/usr/lib" not in parts:
        parts.insert(0, "/usr/lib")
    env["LD_LIBRARY_PATH"] = os.pathsep.join(parts)
    # PYTHONHOME = base prefix of the CMake-configured interpreter; stdlib +
    # site-packages prepended (embedded Python init contract, #730).
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^Python_EXECUTABLE:FILEPATH=(.+)$", cache.read_text(), re.M)
        if match:
            py = Path(match.group(1).strip())
            if py.is_file():
                try:
                    probe = subprocess.run(
                        [str(py), "-c",
                         "import json,sys,sysconfig;print(json.dumps("
                         "{"
                         "'base': sys.base_prefix,"
                         "'stdlib': sysconfig.get_paths()['stdlib'],"
                         "'platstdlib': sysconfig.get_paths()['platstdlib'],"
                         "'purelib': sysconfig.get_paths()['purelib'],"
                         "'platlib': sysconfig.get_paths()['platlib']"
                         "}))"],
                        capture_output=True, text=True, timeout=30,
                    )
                    if probe.returncode == 0:
                        p = json.loads(probe.stdout)
                        env["PYTHONHOME"] = p["base"]
                        pythonpath = [p["stdlib"], p["platstdlib"], p["purelib"],
                                      p["platlib"]]
                        if env.get("PYTHONPATH"):
                            pythonpath.append(env["PYTHONPATH"])
                        env["PYTHONPATH"] = os.pathsep.join(
                            [x for x in pythonpath if x])
                        env["SICNU_PYTHON_EXECUTABLE"] = str(py)
                        env["PYTHONEXECUTABLE"] = str(py)
                except (subprocess.TimeoutExpired, ValueError, KeyError, OSError):
                    pass
    env.setdefault("LSAN_OPTIONS", "detect_leaks=0")
    return env


def run_executable(binary: Path, env: dict, timeout: float,
                   extra_args: list[str] | None = None) -> tuple[str, str, float]:
    started = time.monotonic()
    try:
        proc = subprocess.run(
            [str(binary)] + (extra_args or []),
            capture_output=True, text=True, timeout=timeout, env=env,
            cwd=str(binary.parent),
        )
    except subprocess.TimeoutExpired:
        return "timeout", f"exceeded {timeout:.0f}s budget", time.monotonic() - started
    except OSError as exc:
        return "failed", f"spawn error: {exc}", time.monotonic() - started
    elapsed = time.monotonic() - started
    if proc.returncode == 0:
        return "passed", "", elapsed
    tail = (proc.stdout + "\n" + proc.stderr)[-4000:]
    return "failed", tail, elapsed


def run_ctest(build_dir: Path, pattern: str, jobs: int, timeout: float,
              env: dict) -> tuple[str, str, float]:
    started = time.monotonic()
    cmd = ["ctest", "--test-dir", str(build_dir), "-R", pattern,
           "--output-on-failure", "-j", str(max(1, jobs)),
           "--timeout", str(int(timeout))]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout * 4, env=env)
    except subprocess.TimeoutExpired:
        return "timeout", f"exceeded {timeout * 4:.0f}s wall budget", time.monotonic() - started
    elapsed = time.monotonic() - started
    if proc.returncode == 0:
        return "passed", "", elapsed
    return "failed", (proc.stdout + proc.stderr)[-4000:], elapsed


def load_state(path: Path) -> dict:
    if path.is_file():
        try:
            return json.loads(path.read_text())
        except json.JSONDecodeError:
            pass
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build", help="CMake build tree")
    parser.add_argument("--lanes", default="L0,L1,L2,L3,L4,L5,L7",
                        help="comma list of lanes (default: everything except optional L6/L8)")
    parser.add_argument("--until", default=None, help="run lanes up to and including this one")
    parser.add_argument("--resume", action="store_true",
                        help="skip lane items recorded passed in the state file")
    parser.add_argument("--list", action="store_true", help="list lanes and exit")
    parser.add_argument("--json", default=None, help="write machine-readable results here")
    parser.add_argument("--build-jobs", type=int, default=min(8, os.cpu_count() or 2),
                        help="parallel jobs for build targets (bounded)")
    parser.add_argument("--ctest-jobs", type=int, default=1,
                        help="parallel jobs for the L8 ctest lane (default 1)")
    parser.add_argument("--timeout-scale", type=float, default=1.0,
                        help="multiply every item timeout (slow hosts)")
    parser.add_argument("--bench-out-dir", default=None,
                        help="where benchmark JSON lands (default <build-dir>/benchmarks)")
    parser.add_argument("--asan-build-dir", default=os.environ.get("SICNU_ASAN_BUILD_DIR"),
                        help="separate ASan-instrumented build tree for the LS lane "
                             "(env SICNU_ASAN_BUILD_DIR); without it LS items are "
                             "reported not-built")
    parser.add_argument("--strict", action="store_true",
                        help="non-passing ANY item (incl. skipped/not-built) fails the run")
    args = parser.parse_args()

    if args.list:
        for lane in LANE_ORDER:
            spec = LANES[lane]
            opt = " (optional)" if spec.get("optional") else ""
            print(f"{lane} {spec['title']}{opt}: {spec['description']}")
            for label, name, kind, _ in spec["items"]:
                print(f"    - {label}: {name} [{kind}]")
        return 0

    build_dir = Path(args.build_dir).resolve()
    if not (build_dir / "CMakeCache.txt").is_file():
        print(f"error: {build_dir} is not a configured CMake build tree", file=sys.stderr)
        return 2

    lanes = args.lanes.split(",")
    if args.until and args.until in LANE_ORDER:
        upto = LANE_ORDER.index(args.until)
        requested = set(lanes) | {args.until}
        lanes = [l for l in LANE_ORDER if LANE_ORDER.index(l) <= upto and l in requested]

    state_path = build_dir / "verification-ladder-state.json"
    state = load_state(state_path)
    try:
        current_sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                                     text=True, timeout=15).stdout.strip()
    except (subprocess.TimeoutExpired, OSError):
        current_sha = "unknown"
    env = governed_env(build_dir)
    bench_out = Path(args.bench_out_dir) if args.bench_out_dir else build_dir / "benchmarks"
    bench_out.mkdir(parents=True, exist_ok=True)

    results = {
        "schema": SCHEMA,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "build_dir": str(build_dir),
        "host": {"platform": sys.platform, "cpus": os.cpu_count()},
        "lanes": {},
    }
    overall_ok = True

    for lane in lanes:
        if lane not in LANES:
            print(f"error: unknown lane {lane}", file=sys.stderr)
            return 2
        spec = LANES[lane]
        lane_result = {"title": spec["title"], "items": []}
        lane_ok = True
        print(f"\n=== {lane} {spec['title']} — {spec['description']} ===")
        for label, name, kind, timeout_s in spec["items"]:
            timeout = timeout_s * args.timeout_scale
            item = {"lane": lane, "label": label, "name": name, "kind": kind,
                    "timeout_s": timeout}
            prior = state.get(f"{lane}:{label}")
            if args.resume and prior and prior.get("status") == "passed":
                # A recorded pass is only reusable for the SAME binary and
                # source state: a rebuilt executable invalidates the verdict.
                import hashlib
                binary = find_binary(build_dir, name) if kind in ("test", "bench") else None
                prior_stale = False
                if kind in ("test", "bench"):
                    if binary is None:
                        prior_stale = True
                    elif prior.get("identity") != hashlib.sha1(
                        f"{binary.stat().st_mtime_ns}:{current_sha}".encode()
                    ).hexdigest():
                        prior_stale = True
                if not prior_stale:
                    item.update(status="passed", note="resumed from state file",
                                elapsed_s=0.0)
                    print(f"  [resume-pass] {label}")
                    lane_result["items"].append(item)
                    continue

            if kind == "target":
                status, detail = build_target(build_dir, name, args.build_jobs)
                item.update(status=status, detail=detail[-2000:] if detail else "")
            elif kind == "asan-test":
                asan_dir = Path(args.asan_build_dir).resolve() if args.asan_build_dir else None
                binary = find_binary(asan_dir, name) if asan_dir else None
                if binary is None:
                    item.update(
                        status="not-built",
                        detail="no ASan build tree provided "
                               "(pass --asan-build-dir or set SICNU_ASAN_BUILD_DIR)"
                               if asan_dir is None else
                               f"{name} not compiled in {asan_dir}",
                    )
                else:
                    asan_env = dict(env)
                    asan_env.setdefault("ASAN_OPTIONS",
                                        "detect_leaks=1:abort_on_error=0")
                    status, detail, elapsed = run_executable(binary, asan_env,
                                                             timeout)
                    item.update(status=status, elapsed_s=round(elapsed, 1),
                                detail=detail[-2000:] if detail else "")
            elif kind in ("test", "bench"):
                binary = find_binary(build_dir, name)
                if binary is None:
                    item.update(status="not-built",
                                detail="executable missing from build tree (target never compiled)")
                else:
                    extra: list[str] = []
                    if kind == "bench":
                        extra = ["--out", str(bench_out / f"{name}.json")]
                    status, detail, elapsed = run_executable(binary, env, timeout, extra)
                    item.update(status=status, elapsed_s=round(elapsed, 1),
                                detail=detail[-2000:] if detail else "")
            elif kind == "ctest":
                status, detail, elapsed = run_ctest(build_dir, name, args.ctest_jobs,
                                                    timeout, env)
                item.update(status=status, elapsed_s=round(elapsed, 1),
                            detail=detail[-2000:] if detail else "")
            if item["status"] in ("failed", "timeout"):
                lane_ok = False
            elif item["status"] in ("skipped", "not-built") and args.strict:
                lane_ok = False
            print(f"  [{item['status']}] {label} ({item.get('elapsed_s', '-')}s)")
            lane_result["items"].append(item)
            entry = {"status": item["status"]}
            if kind in ("test", "bench"):
                import hashlib
                binary = find_binary(build_dir, name)
                if binary is not None:
                    entry["identity"] = hashlib.sha1(
                        f"{binary.stat().st_mtime_ns}:{current_sha}".encode()
                    ).hexdigest()
            state[f"{lane}:{label}"] = entry
            # persist per item: an interrupted run still resumes, and a
            # recorded pass is never older than the last completed item
            state_path.write_text(json.dumps(state, indent=1))
        lane_result["status"] = "passed" if lane_ok else "attention"
        results["lanes"][lane] = lane_result
        if not lane_ok:
            overall_ok = False
            # Keep running later lanes: a broad picture beats a first abort;
            # the exit code reports the honest aggregate.

    state["last_run_utc"] = datetime.now(timezone.utc).isoformat()
    state_path.write_text(json.dumps(state, indent=1))

    results["finished_utc"] = datetime.now(timezone.utc).isoformat()
    results["overall"] = "passed" if overall_ok else "attention"
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=1))
        print(f"\nresults JSON: {args.json}")

    print(f"\nOVERALL: {results['overall'].upper()}")
    for lane, lr in results["lanes"].items():
        bad = [i for i in lr["items"] if i["status"] != "passed"]
        mark = "ok" if not bad else "ATTENTION"
        print(f"  {lane} {lr['title']}: {mark}"
              + (f" -> {[i['label'] + ':' + i['status'] for i in bad]}" if bad else ""))
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())
