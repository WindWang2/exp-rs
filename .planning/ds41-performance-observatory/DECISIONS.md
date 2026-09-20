# ds41-performance-observatory — DECISIONS

## D1 — New harness instead of extending `test_execution_benchmarks`

`test_execution_benchmarks.cpp` is the best existing harness but is
`if(NOT WIN32)`-guarded in `tests/CMakeLists.txt:2876` because of the POSIX
`mini_cog_server.h` remote-read fixtures, and it mixes kernel/DAG/model/UI
workloads under one ad-hoc schema. This track is developed on Windows/MSVC, so it
cannot iterate on that target.

**Decision:** add a *new*, minimal, fully portable target
`tests/test_perf_observatory.cpp` + a shared `tests/perf/*.h`, and leave the
existing harnesses untouched. **Alternative rejected:** patching the NOT(WIN32)
guard (would change an unrelated test's CI coverage) or adding POSIX-only code
here.

## D2 — One target, scale by environment variable, not by separate targets

`SICNU_OBS_SCALE=small|mid|scale` picks fixture sizes inside each workload
rather than creating three targets. This matches the existing
`SICNU_BENCH_LARGE` / `SICNU_WS3_STRESS` convention, keeps `ctest` fast by
default, and keeps the recorded `scale` block explicit so a reader always knows
which size produced a number.

## D3 — Structural regression rules, never absolute time budgets

Absolute milliseconds are machine-, load- and debug/release-dependent. The repo
already encodes this policy (`benchmarks/ui-scale-5.0.md`: "never CI-enforced as
flaky gates"). So the observatory gates on:
(a) resource counts (filesystem operations, DB statements, tasks dispatched) —
  machine independent;
(b) complexity: cost(2N)/cost(N) bounded — catches O(N²) regressions while
  tolerating constant-factor noise;
(c) memory upper bounds in structural units (pages materialized, rows in
  memory), plus a generous absolute MB sanity ceiling;
(d) semantic equivalence against a reference value.

## D4 — Complexity measured, not asserted from source reading

To prove O(N²) in `DataManager`, the harness times the *same* workload at N and
2N in one process and computes the empirical exponent
`log2(t(2N)/t(N))`. A linear implementation gives ≈1.0; the current one gives
≈2.0. This is the evidence standard for every hotspot claim.

**Alternative rejected:** source-only reasoning (not objective), or
`strace -c` (Linux-only; this track's dev machine is Windows).

## D5 — Counters instead of new production instrumentation

Rather than adding a query counter to `GovernanceStore`/`WorkspaceCatalog`
(production code, shared ownership), the harness measures the **request-count**
metric from the outside: it counts the statements the store must issue by
driving the store through its public paging API and counting *pages*, and it
counts filesystem operations with a small portable interposer in the harness
(` tests/perf/perf_metrics.h`) that is compiled into the test binary only.

Actually: the cleanest outside-the-harness signal for the SQLite stores is that
each `GovernanceStore::query` page issues exactly two statements (COUNT + page) —
this is verifiable with an in-test `sqlite3_trace_v2` install *if* the store
accepts an external handle, otherwise by driving `page()` and counting calls.
**Decision:** drive the public API and count calls; add a
`sqlite3_trace_v2`-based statement counter in the harness only where the store
exposes a raw handle. Where neither is possible, the harness records
`db_query_count: null` with a reason — consistent with O2's honesty rule.

## D6 — `peak_rss_mb` semantics: delta over the workload, not absolute

Absolute RSS of the process is meaningless across runs. The harness records the
**peak-over-baseline** delta, matching `test_execution_benchmarks`'s
`peakRssDeltaMb`, and separately records a wall-clock baseline sample so the
delta is interpretable.

## D7 — Report tool language: Python

`scripts/perf_report.py` is already Python and is the established baseline
tooling. The new report tool follows it (argparse subcommands
`validate` / `merge` / `compare` / `report`), so there is no new build
dependency and it runs on every lane.

## D8 — Report content split: measured / inferred / recommended

Every finding in the generated Markdown must be labelled one of the three, and
each *measured* number must carry the command that produced it. This prevents
the classic failure mode where a performance PR's motivation is a vibe.
