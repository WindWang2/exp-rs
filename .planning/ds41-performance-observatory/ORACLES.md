# ds41-performance-observatory — ORACLES

Every oracle below is objectively checkable. Commands are run from the track
worktree with the shared Visual Shell environment.

## Environment contract (all runs)

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CTEST_PARALLEL_LEVEL=1
set CMAKE_BUILD_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
```

Build (never more than `-j2`; prefer `-j1`):

```
cmake --build <wt>/build-obs -j1
ctest --test-dir <wt>/build-obs -j1 --output-on-failure -R test_perf_observatory
```

## O1 — Workload catalog: offline, fixed seed, deterministic inputs

- [x] The catalog ships small/mid/scale variants selected by
      `SICNU_OBS_SCALE=small|mid|scale` (default `small`, ctest friendly).
- [x] Every synthetic raster uses a fixed LCG seed baked into the workload
      definition; no wall-clock, no RNG device, no network.
- [x] All fixtures are created in a `QTemporaryDir` under the system temp dir;
      nothing is written into the repository or the user's data directories.
- [x] `ctest -R test_perf_observatory` passes with the machine offline
      (`SICNU_FORCE_OFFLINE=1`) and the working tree clean.

**Check:** `git status --short` in the worktree after a full run shows no new
untracked file outside the declared owner paths.

## O2 — Machine-readable metrics

- [x] One record per workload carrying, as JSON numbers (not prose):
      `measurement` (`wall_ms`, `cpu_ms`, `peak_rss_mb` — null with a reason
      when the watermark never rose —, `read_bytes`/`write_bytes` — null with a
      reason on platforms without a process IO counter), `counts`
      (`tasks_dispatched`, `pages_requested` as the store/DB request count,
      `cache_hits`, `cache_misses`, `files_written`, `rows_materialized`),
      the workload's own `extra` (where queue-wait figures live), plus a
      structured `scale` object (`kind`, `items`, `probes`).
- [x] Unavailable metrics are recorded as an explicit `null` with a
      `"reason"` field, never silently as `0`.
- [x] `python tools/../scripts/bench/perf_observatory_report.py validate <dir>`
      exits 0 and reports the schema version of every record.

## O3 — Two runs agree on structural indicators

- [x] Run the catalog twice into two directories, `A` then `B`.
- [x] These indicators are **identical** across runs:
      workload set, `scale.kind`/`scale.items`/`scale.probes`,
      `counts.tasks_dispatched`, `counts.pages_requested`,
      `counts.cache_hits`, `counts.cache_misses`, `counts.files_written`,
      `counts.rows_materialized`, and `measurement.io.available`.
- [x] Wall time, cpu time and RSS are reported but are never asserted equal
      across runs (machine-relative by contract).

**Check:** `perf_observatory_report.py compare A B` exits 0 and prints
`STRUCTURAL: N identical, 0 divergent`.

## O4 — Baseline + evidence-backed hotspots

- [x] A cross-module baseline record set is committed under `benchmarks/`
      (per-workload JSON, schema `sicnu-perf-observatory/1`).
- [x] A `benchmarks/<...>.md` document separates **measured** (numbers from the
      harness), **inferred** (mechanism from source lines), and
      **recommended** (candidate fix + expected effect).
- [x] At least one hotspot is demonstrated with a complexity measurement, i.e.
      the harness *measures* cost at two sizes and reports the fitted exponent.

## O5 — Regression rules are low-flake

- [x] Every assertion in the workload suite is one of:
      (a) a **count** ceiling (filesystem operations, DB statements, tasks
      dispatched), (b) a **complexity** assertion (cost at 2N within a factor of
      the cost at N), (c) a **memory upper bound** expressed in pages/rows, or
      (d) a **semantic** assertion (result identical to a reference).
- [x] **No** assertion compares an absolute millisecond value against a literal
      budget (except a generous sanity ceiling used only to catch a hang).
- [x] The suite passes 5 consecutive runs (flake watch) with no assertion
      failure.

**Check:** `grep` over `tests/perf/**` and `tests/test_perf_observatory.cpp` for
numeric millisecond literals inside `REQUIRE(... ms` returns nothing.

## O6 — Bench failures never pollute

- [x] A workload that fails mid-way still leaves no file in the worktree.
- [x] Bench output goes to a caller-supplied directory (`SICNU_OBS_OUT`) or a
      temp dir; never to the repo.
- [x] A forced failure (a workload self-reporting failure) leaves
      `git status --short` clean.

**Check:** `git diff --check` and `git status --short` after the failure run.

## O7 — Two consecutive green runs

- [x] Full validation script run twice end to end; both exit 0.

## Gate script

`scripts/bench/perf_observatory_verify.sh` (or `.cmd`) runs O1/O3/O5/O6 and
prints one line per oracle: `O1 PASS ...`. The loop stops only when every line
reads PASS on two consecutive invocations.
