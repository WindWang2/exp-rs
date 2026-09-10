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

(to be filled after targeted builds complete; targets:
test_mlops8_bridge, test_mlops8_e2e, and a 100k-run recording-scale probe
if warranted — expected contract: listRuns paging + reconcile stay bounded
and linear in stored runs.)
