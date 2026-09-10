# PERFORMANCE — evidence for new hot paths

Environment: Linux 6.18, 16 CPUs / 62 GB RAM (shared with concurrent track
builds — all commands run at CMAKE_BUILD_PARALLEL_LEVEL=2, tests -j1),
GCC 16.2.1, Qt 6, Ninja, ccache, Release, USE_PRECOMPILED_HEADERS=OFF
(GCC 16 PCH flakiness under host memory pressure — supported option).

## Hot-path analysis (by design, before measurement)

The bridge adds ZERO work to the execution path when disabled (no monitor
exists, no subscription). When enabled, per-tracked-run work is:

- one queued signal delivery per persisted state transition (≤4 per run);
- per delivery: one WorkflowRun snapshot read (in-memory, mutex-guarded)
  + a bounded JSON conversion (≤256 step summaries, ≤512 members);
- per transition: one checked SQLite transaction via ExperimentRunRecorder
  (existing 7.0 cost profile).

The only scan-shaped addition is `ExperimentStore::runIdsByExecutionRef`
(bounded paged JSON scan) — deliberately a cold path (bridge restart /
reconciliation), never per-event (the bridge keeps an in-process
ref→runId map; the map is authoritative during a session).

`reconcileStale` inspects only non-terminal records (paged listRuns) and
performs ≤1 flock probe + ≤1 checkpoint file read per stale record.

## Measurements

## Measured (2026-09-10, host load average 16–22 from concurrent track builds)

- `test_mlops8_scale`: seeds 20 000 runs (one checked transaction each) and
  reconciles 19 900 stale, evidence-less records — **13.4 s wall / 9.5 s
  user total** for seed + reconcile + assertions; reconciliation is a
  report-only paged scan (no writes, no fabricated closes), linear in
  stored runs.
- `test_mlops8_bridge` (150 assertions incl. full lifecycle + stale
  decisions): single-digit seconds.
- `test_mlops8_e2e` (real tracked pipelines): each case < 0.1 s of compute;
  wall time is dominated by executor sleep polling (bounded per case) and
  host load. All six cases pass individually and in full-suite runs when
  the host permits; under extreme load (other agents' -j12 builds) a
  whole-binary run can exceed generous timeouts while every case remains
  green when executed — environmental, documented in TEST_MATRIX.md.

Commands (all in the worktree, Ninja Release, -j1 test execution):

    ./build/tests/test_mlops8_bridge
    ./build/tests/test_mlops8_scale
    ./build/tests/test_mlops8_e2e            # or per TEST_CASE filter
    ./build/tests/test_dataset_core          # regression sweep below
    ./build/tests/test_split_leakage
    ./build/tests/test_experiment_evaluation
    ./build/tests/test_platform7_library
    ./build/tests/test_data_platform_surface
    ./build/tests/test_workflow_run_coordinator
    ./build/tests/test_dataset_e2e_examples
    ./build/tests/test_mcp_server
; targets:
test_mlops8_bridge, test_mlops8_e2e, and a 100k-run recording-scale probe
if warranted — expected contract: listRuns paging + reconcile stay bounded
and linear in stored runs.)
