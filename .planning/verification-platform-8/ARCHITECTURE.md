# ARCHITECTURE — verification platform 8.0

## Design principles

1. **Extend the 7.0 observability core, never fork it.** All new probes go
   through `SICNU_FAULT_POINT` (fault) and `trace::TraceEvent` + `Trace`
   (trace) in `src/runtime/observability/**`. Hot-path contract preserved:
   disabled trace = one relaxed atomic load; disarmed fault probe = one
   relaxed atomic load; string formatting only behind `Trace::enabled()`.
2. **Probes at existing broadcast points only.** Trace adapters are inserted
   where a seam already mutates or broadcasts state (TaskCenter status
   transitions, OutputCommitter publish/rollback returns, store transaction
   commits) — no new event bus, no polling, no second architecture.
3. **First-party GDAL compat gets one narrow seam**: `src/geospatial/util/
   gdal_compat.h`. Vendored QGIS (`src/core`, `src/gui`) is untouched.
4. **Verification tooling is Python 3 stdlib + CTest/CMaketargets** — no new
   third-party deps, no online services.
5. **Every claim machine-checkable**: runner emits JSON; readiness report
   distinguishes compiled / executed-passed / executed-failed / skipped.

## Component map (new)

```
scripts/verification_ladder.py      L0..L7 lanes, resumable, JSON out
scripts/collect_readiness.py        aggregates ctest JSON + bench JSON ->
                                    docs/verification/READINESS.{md,json}
src/geospatial/util/gdal_compat.h   GDAL 3.8..3.13 macro seam (first-party)
tests/test_portability_contract.cpp compile-time probes: header self-
                                    containment (F1), gdal_compat branches
tests/test_fault_matrix_8.cpp       store/recorder/model/task fault contracts
tests/test_trace_chain_8.cpp        end-to-end correlation across the
                                    Workflow->TaskCenter->JobEngine->
                                    OutputCommitter->Dataset chain
tests/test_contract_fuzz_ops.cpp    operator schemas + model manifests +
                                    JSON migrations + path containment
tests/test_contract_fuzz_ipc.cpp    worker protocol envelopes + split
                                    manifests + STAC pagination state
tests/test_known_answer_corpus_8.cpp grid ops, split/leakage formulas,
                                    cartographic structural invariants
tests/benchmark_scale8.cpp          scheduling 1k/10k/100k, dataset 100k
                                    metadata, trace overhead (Linux evidence)
```

## Trace correlation design (WP-F)

`TraceContext` (header-only) carries run/task/job/worker/op/artifact ids.
Missing links get adapters:

- **WorkflowRunCoordinator / workflow run lifecycle**: emit `run` events
  (start/end/error) at existing run state transitions.
- **TaskCenter**: emit submit / dispatch / terminal events mapped from the
  SAME places that already update task records and emit telemetry — no
  parallel bookkeeping. Task id goes into `task`; pipeline id into `run`
  namespace as `pipeline-<id>` when present.
- **OutputCommitter**: emit `artifact` events on publish success/rollback
  with the registered AssetId.
- **DatasetStore / ExperimentStore**: emit `artifact` registration events
  inside the existing commit paths.

All gated by `Trace::enabled()`; string building only when enabled.

## Fault points design (WP-E)

New `SICNU_FAULT_POINT` sites (each: one relaxed load when disarmed):

- `dataset_store.commit` — after a successful transaction commit branch is
  chosen, before it executes (NextN makes the commit path report failure).
- `experiment_store.commit` — same.
- `run_recorder.append` — ExperimentRunRecorder append.
- `model_provider.acquire` — model runtime session acquire boundary.
- `task_center.mark_terminal` — exercises truthful terminal bookkeeping
  under injected failure of the result payload write.

Each site: fail-truthfully (typed error), no partial state, rollback paths
re-entry-safe (NextN semantics from the 7.0 registry apply unchanged).

## Portability matrix design (WP-A)

- `gdal_compat.h` centralizes the version ladder used by first-party geo
  code; `range_cache.cpp` migrates onto it without behavior change.
- `test_portability_contract.cpp` proves header self-containment by
  including the new header first (no transitive includes) and asserts the
  selected compat macros form a coherent configuration on this host.
- `scripts/verification_ladder.py` lane L0 builds a small "compile canary"
  target with the host's secondary compiler (Clang when GCC is primary and
  vice versa) — the practical local guard against F1/F3 classes; Windows/
  macOS stay documented-but-not-executed locally, and the readiness report
  marks them `not-executed-here`.

## Resource bounds

- Fuzzers: fixed seeds, ≤ 512-byte inputs, few hundred iterations per seed.
- Stress/benchmarks: explicit iteration caps; 100k-class workloads operate
  on in-memory/synthetic structures, never 100GB data; RUN_SERIAL where the
  7.0 suites set precedent.
- Ladder: `CMAKE_BUILD_PARALLEL_LEVEL`/ninja `-j` capped (default 8, env
  override), tests run `-j1` by default (matching local discipline).

## Cross-track reconciliation (observed 2026-09-11)

Concurrent 8.0 tracks are actively building on this host:
`exp-rs-execution-plane-8` (targets: test_execution_plane_8/7,
test_task_center, test_job_engine, test_workflow_run_coordinator — shares
the TaskCenter/JobEngine seams this track instruments) and
`exp-rs-geospatial-data-fabric-8` (temporal workspace / CLI). Dispositions:

- This track's edits to shared seams are strict adapters (5–15-line
  insertions at existing broadcast funnels); conflicts, if any, are trivial
  context merges.
- Host contention from those builds is the leading explanation for the
  flaky GCC diagnostic-path segfaults (PLAN.md D1); benchmark numbers from
  this host carry a contention caveat (PERFORMANCE.md).
- test_task_center appears in BOTH tracks' plans; this track deliberately
  does NOT edit it — the TaskCenter chain proof lives in
  tests/test_trace_chain_8.cpp.
