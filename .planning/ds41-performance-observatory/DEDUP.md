# ds41-performance-observatory — DEDUP

## Live overlap check (2026-09-20, at `adf8f9895`)

| Source | Overlap with this track | Verdict |
|---|---|---|
| Open PRs (0) | — | **No conflict possible.** |
| Open issues (0) | — | **No conflict possible.** |
| Recent merged PRs (last 80) | #1009 (per-chunk telemetry), #980 (large-scale engine), #981 (model runtime), #1023 (fabric range cache), #992 (D19 dataset foundry) all *build* capability, none *cross-module benchmark* | No duplication. This track consumes their APIs read-only. |
| Remote `agent/*` branches (12) + `fix/*` (3) | `git diff --name-only origin/master...origin/<b>` filtered on `benchmarks/`, `tests/perf/`, `scripts/bench/`, and the three benchmark test files ⇒ **0 file hits on every branch** | **Zero overlap.** No cherry-pick needed. |

All remote branches are 80 commits behind master and are historical fix/UI
branches (shortcut lifecycle, georef GCP, plugin trust, workflow integrity,
data transactions, CI unblock). Their `ahead` counts (1–12 commits) contain no
perf-harness work.

## Existing in-repo capability that this track reuses rather than rebuilds

| Existing | Reused by this track | New work |
|---|---|---|
| `test_execution_benchmarks` harness (`execution-bench/1`) | Metric *definitions* and workload shapes | — (read as reference) |
| `test_ui_scale_benchmark` structural guards | The idea of non-timed structural gates | Generalized into `tests/perf/` regression rules |
| `scripts/perf_report.py` | diff/merge concept | New report tool with measured/inferred/recommended separation |
| `benchmarks/ui-scale-5.0.md` | Envelope-policy documentation style | `benchmarks/perf-observatory-baseline.md` |
| `Sicnu::Geospatial` I/O test link style | Minimal, Qt-free linking for I/O workloads | New light target |

## Where a new open PR could collide

If a business Track later opens a PR that also adds a shared bench harness, this
track's owner area (`tests/perf/**`, `scripts/bench/**`, `benchmarks/**`) is the
single place to reconcile; the new target name `test_perf_observatory` is
namespaced to avoid collision with `test_execution_benchmarks`,
`test_perf_benchmarks`, `test_ui_scale_benchmark`.
