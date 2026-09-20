## Summary

Track 12 (`flash-taskcenter-runtime-12`) — wire the previously library-only
`TaskResourceBudget2` capabilities into `TaskCenter` so the execution plane is
predictable, cancelable, rate-limited, and observable, **without changing
high-level workflow/DAG semantics**.

- Baseline: `origin/master = adf8f9895` (fetched at track start; 0 open PRs,
  0 open issues at that time).
- Dedup: residual `agent/*`/`fix/*` remote branches are superseded fix waves
  (#1100–#1115 merged equivalents); nothing cherry-picked. No open PR overlaps
  this file set.

### Scope
- `src/processing/framework/task_center.{h,cpp}` — scheduler implementation.
- `src/runtime/observability/execution_telemetry.{h,cpp}` — `EventKind::Cancelled`,
  `Counter::{TasksRefused,TasksAged,CancelWatchdogFired}`.
- `tests/test_task_center_12.cpp` + `tests/CMakeLists.txt` (RUN_SERIAL).
- `.gitignore` — planning-doc whitelist for `.planning/flash-taskcenter-runtime-12/`.

### Non-scope
- No second scheduler; no `PipelineRunCoordinator`/workflow changes; no
  preemption; `TaskResourceBudget2` itself unchanged.

### Design (see `.planning/flash-taskcenter-runtime-12/DECISIONS.md`)
1. **Aging fairness** — per-task `enqueueSteadyStamp` + `effectivePriority`;
   `applyAgingSweepLocked()` promotes long-waiting candidates via
   `sicnu::agedPriority` at the head of every admission pass (env
   `SICNU_TASK_AGING_MS`, default 5000, 0=off). Retry preserves the stamp.
2. **Latency classes** — `latencyClassForSource` maps submission tags to
   Interactive/Background/Batch; feeds `ResourceRequest.latencyClass` so the
   interactive reserve protects UI/agent work. Admission-only, no preemption.
3. **Bounded pending** — `m_liveTaskCount` counts non-terminal tasks;
   `enqueueTask`/`submitJobImpl`/`submitPipeline` refuse `-1` at the cap
   (`SICNU_TASK_MAX_PENDING`, default 4096, 0=unbounded); pipeline refusal is
   atomic before any step is created; `TasksRefused` counter.
4. **Cancel watchdog** — `cancelDeadline` armed before the Cancelling
   transition; a lazily-started watchdog thread (25 ms tick) finalizes
   stranded Cancelling tasks as Canceled/Engine after
   `SICNU_TASK_CANCEL_TIMEOUT_MS` (default 30000, 0=off). Late worker records
   become foreign-job no-ops (job mapping detached first).
5. **Weight admission** — descriptor `execution.{temporaryDiskBytes,
   estimatedVramBytes,cpuThreads,diskReadWeight,diskWriteWeight,networkWeight}`
   parsed lock-free at submission into `m_admissionDimsCache`; active sums in
   `m_active.usage2` maintained at the `setTaskStatusLocked` seam;
   `m_budget2.canLaunch` gate (never-starve: skipped when nothing runs and
   for bounded transient children).
6. **Lock-free warming** — adapter lookup, descriptor dims, resource profile,
   and RAM estimate resolve outside `m_mutex` (residual #1097); warmed values
   seeded into the task record.
7. **Telemetry funnel** — all terminal transitions pass `setTaskStatusLocked`
   exactly once → exact `TasksCompleted/Failed/Canceled`; QueueWait/Dispatched
   on Dispatching, ExecutionStart on first Running, ExecutionEnd +
   `Cancelled` (latency + typed reason) at terminal. Off by default.
8. **Fault injection** — `taskcenter.dispatch` (skips engine submit → real
   refused-submit rollback) and `taskcenter.jobrecord` (drops records →
   lost-record/watchdog path). ~1 relaxed atomic load when unarmed.

### Oracle → test mapping
| Oracle | Test(s) |
|---|---|
| Priority observable, no starvation | `[fairness]` |
| Queue/RAM/in-flight hard bounds + stress | `[backpressure]`, `[backpressure][pipeline]` |
| Cancel → unique terminal in queued/running/shutdown | `[cancel]`, `[fault]` (lost record) |
| Stale job record cannot pollute new job | `[fault]` foreign-record assertion + ep7/ep8 suites |
| Core tests pass twice, no data-race design gap | gate runs ×2 |
| Observability (queue wait, run, cancel latency, counters) | `[telemetry]` |
| Weight admission + interactive reserve | `[weights]`, `[snapshot]` |

### Test commands & results
```
cmake --build build-dev --target test_task_center_12 -j1
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R test_task_center_12 -j1 --output-on-failure
```
- Run 1: (pending)
- Run 2: (pending)

### Resource limits honored
`-j1` build / `-j1` ctest on a contended shared host (≥91% RAM at build time);
vcpkg manifest mode bypassed via the populated `vcpkg_installed` (no lock wait).

### Review disposition
(Independent review pending — P0/P1 cleared, see review notes.)

### Known limitations
- `admissionSnapshot` evaluates the weight gate with the declared lane only —
  a preflight probe has no queue stamp, so aged-promotion effects are not
  simulated there (real passes apply them).
- The cancel watchdog bounds *stranded* Cancelling; cooperative cancellation
  latency still depends on executor cancel-flag checks (unchanged semantics).

### Conflict hotspots
- `task_center.{h,cpp}` — single owner for this track; recently touched by
  #1113/#1115 (retry/admission) — this branch builds on top of that state.
- `tests/CMakeLists.txt`, `.gitignore` — append-only edits, low conflict risk.

Generated with [Devin](https://devin.ai)
