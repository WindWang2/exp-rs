#!/usr/bin/env python3
"""benchmark_harness.py — repeatable headless performance baseline (#660).

Drives the desktop binary in --mcp mode over stdio JSON-RPC and measures
wall-clock time plus peak RSS for a fixed set of representative workloads:

  * rs:spectral_index   (per-pixel, multi-band float32 input)
  * rs:qa_mask          (per-pixel bit interpretation, uint16 input)
  * rs:recode           (per-pixel label mapping, uint16 input)
  * rs:majority_filter  (neighborhood stencil, uint16 label input)
  * a two-step pipeline  (qa_mask -> majority_filter) via run_workflow

Inputs are generated deterministically in ENVI format (pure stdlib, GDAL
reads them natively), so every run of the same --size sees identical data.

Usage:
  python scripts/benchmark_harness.py --binary /path/to/sicnu_geo_rs \
      --out bench_baseline.json [--size 1024 1024] [--bands 4] [--repeat 3]

Peak RSS probing uses psutil when available (pip install psutil); without
it, elapsed times are recorded and peak_rss_mb is reported as null.

The result is a machine-readable baseline artifact with hardware metadata;
run it on the acceptance workstation and on the dev machine before and
after each performance ticket (issue #660).
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from array import array
from datetime import datetime, timezone

try:
    import psutil  # optional, for peak RSS probing
except ImportError:
    psutil = None


# ---------------------------------------------------------------------------
# Deterministic synthetic input rasters (ENVI: raw binary + .hdr sidecar)
# ---------------------------------------------------------------------------

def _write_envi(path_base: str, width: int, height: int, bands: int,
                fill) -> str:
    """Writes <path_base>.dat + <path_base>.hdr; returns the data path."""
    data_path = path_base + ".dat"
    hdr_path = path_base + ".hdr"
    probe = fill(0)
    dtype_code = 4 if probe.typecode == "f" else 12  # ENVI: 4=float32, 12=uint16
    with open(data_path, "wb") as f:
        for b in range(bands):
            row = fill(b)
            for _ in range(height):
                row.tofile(f)
    with open(hdr_path, "w", encoding="ascii") as f:
        f.write(f"samples = {width}\n")
        f.write(f"lines   = {height}\n")
        f.write(f"bands   = {bands}\n")
        f.write("header offset = 0\n")
        f.write("file type = ENVI Standard\n")
        f.write("data type = %d\n" % dtype_code)
        f.write("interleave = bsq\n")
        f.write("byte order = 0\n")
    return data_path


def make_float_input(path_base: str, width: int, height: int, bands: int):
    """Float32 reflectance-like values in [0, 1), band-correlated so NDVI-style
    ratios are non-trivial. LCG keeps data identical across runs/machines."""
    state = 123456789

    def next_rand():
        nonlocal state
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        return (state >> 11) / float(1 << 53)

    def fill(band):
        vals = array("f", [0.0]) * width
        for i in range(width):
            vals[i] = 0.05 + 0.9 * next_rand()
        return vals

    return _write_envi(path_base, width, height, bands, fill)


def make_label_input(path_base: str, width: int, height: int, classes: int):
    """Uint16 label raster with `classes` distinct ids (1..classes)."""
    state = 987654321

    def next_class():
        nonlocal state
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        return 1 + (state >> 33) % classes

    def fill(_band):
        return array("H", [next_class() for _ in range(width)])

    return _write_envi(path_base, width, height, 1, fill)


def make_qa_input(path_base: str, width: int, height: int):
    """Uint16 QA flag raster mixing clear (0) and flagged bit patterns."""
    patterns = [0, 0, 0, 1 << 0, 1 << 1, 1 << 2, 1 << 3, (1 << 1) | (1 << 3)]
    state = 555555555

    def next_pattern():
        nonlocal state
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        return patterns[(state >> 33) % len(patterns)]

    def fill(_band):
        return array("H", [next_pattern() for _ in range(width)])

    return _write_envi(path_base, width, height, 1, fill)


# ---------------------------------------------------------------------------
# Minimal MCP stdio JSON-RPC client
# ---------------------------------------------------------------------------

class McpClient:
    """Newline-delimited JSON-RPC over the binary's --mcp stdio transport."""

    def __init__(self, binary: str, cwd: str):
        self.proc = subprocess.Popen(
            [binary, "--mcp"],
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self._next_id = 1

    def _send(self, payload: dict):
        self.proc.stdin.write(json.dumps(payload) + "\n")
        self.proc.stdin.flush()

    def request(self, method: str, params: dict | None = None,
                timeout: float = 30.0) -> dict:
        rid = self._next_id
        self._next_id += 1
        msg = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("MCP stdout closed while waiting for %s" % method)
            line = line.strip()
            if not line:
                continue
            try:
                reply = json.loads(line)
            except json.JSONDecodeError:
                continue
            if reply.get("id") == rid:
                if "error" in reply:
                    raise RuntimeError("MCP error for %s: %s" % (method, reply["error"]))
                return reply.get("result", {})
        raise TimeoutError("timed out waiting for %s reply" % method)

    def notify(self, method: str, params: dict | None = None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)

    def initialize(self):
        self.request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "benchmark-harness", "version": "1.0"},
        })
        self.notify("notifications/initialized")

    def peak_rss_bytes(self) -> int:
        if psutil is None:
            return 0
        try:
            total = psutil.Process(self.proc.pid).memory_info().rss
            for child in psutil.Process(self.proc.pid).children(recursive=True):
                try:
                    total += child.memory_info().rss
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            return total
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return 0

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


# ---------------------------------------------------------------------------
# Workload execution
# ---------------------------------------------------------------------------

TERMINAL = {"completed", "failed", "canceled"}


def _poll(client: McpClient, execution_id: str, timeout_s: float) -> dict:
    """Polls get_execution_status until terminal, sampling peak RSS."""
    deadline = time.monotonic() + timeout_s
    peak = client.peak_rss_bytes()
    last = {}
    while time.monotonic() < deadline:
        last = client.request("tools/call", {
            "name": "get_execution_status",
            "arguments": {"execution_id": execution_id},
        })
        payload = _tool_payload(last)
        status = payload.get("status", "running")
        peak = max(peak, client.peak_rss_bytes())
        if status in TERMINAL:
            return {"status": status, "peak_rss_bytes": peak, "payload": payload}
        if status == "cancelling":
            continue
        time.sleep(0.05)
    return {"status": "timeout", "peak_rss_bytes": peak, "payload": last}


def _tool_payload(tool_result: dict) -> dict:
    """tools/call wraps payloads in content[0].text JSON; unwrap defensively."""
    content = tool_result.get("content")
    if isinstance(content, list) and content:
        text = content[0].get("text", "")
        try:
            return json.loads(text)
        except (json.JSONDecodeError, TypeError):
            return {}
    return {k: v for k, v in tool_result.items() if k != "content"}


def run_operator_workload(client: McpClient, operator_id: str, params: dict,
                          timeout_s: float) -> dict:
    digest = hashlib.sha256(
        json.dumps(params, sort_keys=True).encode("utf-8")).hexdigest()[:16]
    start = time.perf_counter()
    result = client.request("tools/call", {
        "name": "execute_operator",
        "arguments": {"operator_id": operator_id, "parameters": params},
    })
    payload = _tool_payload(result)
    execution_id = payload.get("execution_id", "")
    if not execution_id:
        return {"status": "submit_failed", "elapsed_s": 0.0,
                "peak_rss_bytes": 0, "params_digest": digest, "payload": payload}
    outcome = _poll(client, execution_id, timeout_s)
    outcome["elapsed_s"] = time.perf_counter() - start
    outcome["params_digest"] = digest
    return outcome


def run_pipeline_workload(client: McpClient, pipeline: dict,
                          timeout_s: float) -> dict:
    digest = hashlib.sha256(
        json.dumps(pipeline, sort_keys=True).encode("utf-8")).hexdigest()[:16]
    start = time.perf_counter()
    result = client.request("tools/call", {
        "name": "run_workflow",
        "arguments": {"pipeline": pipeline, "auto_load": False},
    })
    payload = _tool_payload(result)
    pipeline_id = payload.get("pipeline_id")
    if pipeline_id is None:
        return {"status": "submit_failed", "elapsed_s": 0.0,
                "peak_rss_bytes": 0, "params_digest": digest, "payload": payload}
    deadline = time.monotonic() + timeout_s
    peak = client.peak_rss_bytes()
    last = {}
    while time.monotonic() < deadline:
        last = client.request("tools/call", {
            "name": "get_workflow_status",
            "arguments": {"pipeline_id": pipeline_id},
        })
        step_payloads = _tool_payload(last)
        peak = max(peak, client.peak_rss_bytes())
        # Aggregate terminal state reported directly by the workflow view.
        if step_payloads.get("status") in TERMINAL:
            return {"status": step_payloads["status"],
                    "elapsed_s": time.perf_counter() - start,
                    "peak_rss_bytes": peak, "params_digest": digest,
                    "payload": step_payloads}
        steps = step_payloads.get("steps", [])
        statuses = [s.get("status") for s in steps.values()] if isinstance(steps, dict) \
            else [s.get("status") for s in steps]
        if statuses and all(s in TERMINAL for s in statuses):
            ok = all(s == "completed" for s in statuses)
            return {"status": "completed" if ok else "failed",
                    "elapsed_s": time.perf_counter() - start,
                    "peak_rss_bytes": peak, "params_digest": digest,
                    "payload": step_payloads}
        time.sleep(0.05)
    return {"status": "timeout", "elapsed_s": time.perf_counter() - start,
            "peak_rss_bytes": peak, "params_digest": digest, "payload": last}


# ---------------------------------------------------------------------------
# Harness driver
# ---------------------------------------------------------------------------

def build_workloads(workdir: str, width: int, height: int, bands: int):
    """Creates inputs and returns the workload list (name + callable params)."""
    refl = make_float_input(os.path.join(workdir, "bench_reflectance"),
                            width, height, bands)
    labels = make_label_input(os.path.join(workdir, "bench_labels"),
                              width, height, 5)
    qa = make_qa_input(os.path.join(workdir, "bench_qa"), width, height)

    out_dir = os.path.join(workdir, "out")
    os.makedirs(out_dir, exist_ok=True)
    out = lambda name: os.path.join(out_dir, name)  # noqa: E731

    operator_workloads = [
        ("spectral_index", "rs:spectral_index", {
            "input": refl, "output": out("ndvi.tif"), "index": "NDVI"}),
        ("qa_mask", "rs:qa_mask", {
            "input": qa, "output": out("mask.tif"),
            "source": "generic_bitmask", "bits": 8}),
        ("recode", "rs:recode", {
            "input": labels, "output": out("recoded.tif"),
            "recode_map": json.dumps({"1": 2, "2": 3, "3": 1, "4": 4, "5": 1})}),
        ("majority_filter", "rs:majority_filter", {
            "input": labels, "output": out("majority.tif"), "kernel": 3}),
    ]

    pipeline = {
        "id": "bench-pipeline",
        "name": "Benchmark two-step pipeline",
        "steps": [
            {"id": "mask", "title": "QA mask", "operator": "rs:qa_mask",
             "params": {"input": qa, "output": out("pipe_mask.tif"),
                        "source": "generic_bitmask", "bits": 8},
             "inputs": []},
            {"id": "smooth", "title": "Majority filter", "operator": "rs:majority_filter",
             "params": {"output": out("pipe_majority.tif"), "kernel": 3},
             "inputs": [{"fromStepId": "mask", "fromPort": "output",
                         "toPort": "input"}]},
        ],
    }
    return operator_workloads, pipeline


def host_metadata() -> dict:
    info = {
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cores": os.cpu_count(),
        "memory_probe": "psutil" if psutil else "unavailable",
    }
    if psutil:
        info["ram_total_bytes"] = psutil.virtual_memory().total
    return info


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", required=True, help="path to the desktop binary")
    ap.add_argument("--out", required=True, help="baseline JSON artifact path")
    ap.add_argument("--size", nargs=2, type=int, default=[1024, 1024],
                    metavar=("WIDTH", "HEIGHT"))
    ap.add_argument("--bands", type=int, default=4)
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=1800.0,
                    help="per-workload timeout in seconds")
    args = ap.parse_args()

    if shutil.which(args.binary) is None and not os.path.isfile(args.binary):
        print("binary not found: %s" % args.binary, file=sys.stderr)
        return 2

    workdir = tempfile.mkdtemp(prefix="exp-rs-bench-")
    operator_workloads, pipeline = build_workloads(
        workdir, args.size[0], args.size[1], args.bands)

    records = []
    client = McpClient(args.binary, cwd=workdir)
    try:
        client.initialize()
        for name, operator_id, params in operator_workloads:
            for r in range(args.repeat):
                outcome = run_operator_workload(client, operator_id, params,
                                                args.timeout)
                outcome.update({"name": name, "operator": operator_id, "repeat": r})
                records.append(outcome)
                print("%-16s r%d: %-9s %.3fs  peak=%s" % (
                    name, r, outcome["status"], outcome.get("elapsed_s", 0.0),
                    _fmt_mb(outcome.get("peak_rss_bytes"))))
        outcome = run_pipeline_workload(client, pipeline, args.timeout)
        outcome.update({"name": "pipeline_mask_majority", "operator": "run_workflow",
                        "repeat": 0})
        records.append(outcome)
        print("%-16s r0: %-9s %.3fs  peak=%s" % (
            "pipeline", outcome["status"], outcome.get("elapsed_s", 0.0),
            _fmt_mb(outcome.get("peak_rss_bytes"))))
    finally:
        client.close()

    baseline = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "host": host_metadata(),
        "binary": os.path.abspath(args.binary),
        "raster": {"width": args.size[0], "height": args.size[1],
                   "bands": args.bands},
        "workloads": records,
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(baseline, f, indent=2)
    print("baseline written: %s" % args.out)

    failed = [r for r in records if r.get("status") != "completed"]
    return 1 if failed else 0


def _fmt_mb(rss_bytes) -> str:
    if not rss_bytes:
        return "n/a"
    return "%.1fMB" % (rss_bytes / (1024.0 * 1024.0))


if __name__ == "__main__":
    sys.exit(main())
