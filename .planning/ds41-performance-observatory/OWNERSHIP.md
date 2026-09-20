# ds41-performance-observatory — OWNERSHIP

Track branch: `agent/ds41-performance-observatory`
Baseline: `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`

## Writable by this track (primary owner)

| Path | Purpose |
|---|---|
| `benchmarks/*.json`, `benchmarks/*.md` | Baseline records, methodology docs, reports |
| `tests/perf/**` | New shared perf-observability harness (header + workloads) |
| `tests/test_perf_observatory*.cpp` | New Catch2 target(s) for the observatory |
| `scripts/bench/**` | Runner + report generator scripts |
| `docs/perf-observatory*.md` | Observatory documentation |
| `.gitignore` (append-only block) | Whitelist `.planning/ds41-performance-observatory/` |

## Read-only (never modified by this track)

Everything else, in particular:
- `src/**` production code. This track **measures** it. A production change is
  allowed only when (a) no parallel owner exists, (b) the change is local, and
  (c) a benchmark run proves it. Otherwise the finding lands as an issue with
  evidence in the PR body.
- `tests/CMakeLists.txt` — touched **only** to register the new target, appended
  next to the sibling benchmark registrations, following the existing comment
  style. No reordering, no reformatting of other blocks.
- `tests/test_execution_benchmarks.cpp`, `tests/test_ui_scale_benchmark.cpp`,
  `tests/test_perf_benchmarks.cpp` — existing harnesses; reused as reference,
  not edited.

## Shared, append-only

- `benchmarks/` — only adds new files (`*.json` baselines, `*.md` reports).
- `scripts/` — only adds `perf_observatory/*.py`.
- `tests/CMakeLists.txt` — one new `if(...)` block + one `sicnu_add_test` call.

## Non-goals (do not do)

- No production optimization without evidence and local scope.
- No dependency on a fixed high-end GPU (all inference workloads must run on CPU).
- No absolute-millisecond CI gates.
- No deleting/loosening existing tests.
