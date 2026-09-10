# Trace Architecture — Verification 7.0

## Goal

Answer "where did one complex task spend time / where did it fail" with a
machine-readable event chain instead of grep over logs:

```
Pi / Harness  →  Workflow  →  TaskCenter  →  JobEngine  →  Worker
                                                     →  Operator
                                                     →  OutputCommitter
                                                     →  Data / Experiment
```

## Correlation model

- **Ids are created once at their origin and propagated by value** — a child
  carries the parent's ids plus its own (`TraceContext.with*`, header-only).
- Id shape: 26-char Crockford-base32 (ULID-style): 50-bit ms epoch + 80-bit
  process-unique monotonic payload. Sortable within a process, safe in
  filenames, readable on the wire. `TraceIdGenerator::resetForTests` gives
  known-answer sequences for tests only.
- Entry surfaces already carry a correlation id (`ExecutionPlane::
  ExecutionRequest::correlationId` — agent turn / JSON-RPC id). The trace
  binds that id to the run at submit time; adapters never mint parallel ids.

## Record schema (`exp.trace.v1`, NDJSON — one JSON object per line)

```json
{"schema":"exp.trace.v1","ts":1690000000000,"run":"…","task":"…","job":"…",
 "worker":"…","operator":"rs:ndvi","artifact":"…","event":"committed",
 "phase":"end","status":"ok","detail":"…","duration_us":1500}
```

Empty fields are omitted. `encodeNdjson` is total (escapes control bytes,
never emits a raw newline).

## Where events come from (adapters, thin and additive)

| Layer | Event source | Notes |
|---|---|---|
| Workflow | run lifecycle transitions | run id minted here (or at harness submit) |
| TaskCenter | submit / admission (WaitingResource) / dispatch / terminal transitions | mirrors TaskStatus; the telemetry events already exist — the adapter maps them onto the trace context |
| JobEngine | Listener (already broadcast: JobRecord snapshots) | job ids match worker protocol jobId |
| Worker | worker_protocol frames carry jobId | frames logged verbatim at trace level |
| OutputCommitter | publish / rollback / register | artifact id = registered AssetId |
| Data/Experiment | asset registered / experiment appended | closes the chain |

The adapters are thin inline blocks wired at the seams that ALREADY
broadcast state — `ExecutionPlane::submit` + the completion callback
(`execution_plane.cpp`) and `JobEngine::runOperatorJob` (`job_engine.cpp`,
execution_start/end via an RAII guard); no second event bus, no polling,
no separate `trace_adapters.*` files.

## Sinks & budgets

- **Disabled by default.** `Trace::publish` with no sink = one relaxed atomic
  load (same hot-path contract as `ExecutionTelemetry`); both adapter sites
  additionally gate on `Trace::enabled()` before building strings. Measured
  evidence in `benchmarks/quality7.json` (`trace_emit_disabled`).
- `RingTraceSink` — bounded in-memory ring (tests, GUI inspector).
- `FileTraceSink` — NDJSON append via a single writer thread; bounded queue
  (drop-oldest + dropped counter — honest accounting, bounded memory);
  size-based rotation (`maxBytes`, `maxFiles` history); no fsync (evidence
  stream, not the commit path).
- Host bootstrap: `SICNU_TRACE=1`, `SICNU_TRACE_DIR=…`, `SICNU_TRACE_MAX_MB=…`
  via `installFileSinkFromEnv()`.

## Diagnostics tie-in

`DiagnosticReport` (`exp.diag.v1`) carries the same run/task/job ids,
recoverability (`none|manual|transient|unknown`), suggested action, causal
chain, and artifact references. Origin codes are preserved verbatim — unknown
codes stay honest (`recoverability: unknown`), never renamed, never swallowed
(the `DiagnosticCatalog` rule).

## Contract tests

- `test_trace_contract` — id format/uniqueness/sortability under 8-thread
  concurrency, deterministic mode, encoder escaping, ring bounds, file
  rotation bound (history-bounded, newest kept), drop accounting with
  written + dropped == pushed coherence, disabled hot path.
- `test_diagnostic_report` — envelope fields, verbatim unknown codes,
  escaping, one-line guarantee.
