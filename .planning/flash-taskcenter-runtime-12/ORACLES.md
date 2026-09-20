# ORACLES — flash-taskcenter-runtime-12

Objective completion criteria. Each oracle names its reproducible evidence. All tests run with `QT_QPA_PLATFORM=offscreen`, `ctest -j1` inside `build-dev` (or direct exe).

| # | Oracle | Evidence (test / command) | Status |
|---|---|---|---|
| O1 | Priority is observable AND low priority never starves: under a sustained High stream with 1 worker slot, a Low task completes within a bounded wait (aging). | `test_taskcenter_runtime_12.exe "[fairness]"` — `SICNU_TASK_AGING_MS` small; assert Low completes while High stream active; assert order is priority-major when aging off. | pending |
| O2 | queue/RAM/in-flight hard bounds + stress: non-terminal task count ≤ `maxPendingTasks` (submission refused, not dropped, when full); in-flight ≤ globalMax; RAM budget holds. | `test_taskcenter_runtime_12.exe "[backpressure]"` — setMaxPendingTasks(N), submit N+M, M refused with `queue_full` evidence (telemetry counter + trace); drain releases admission. `test_taskcenter_runtime_12.exe "[stress]"` — high-concurrency small-task drain converges, counters consistent. | pending |
| O3 | Cancel reaches a unique terminal outcome in every state: queued, admission-held, running, retry-pending, cascade child, shutdown; no duplicate completion callbacks. | `test_taskcenter_runtime_12.exe "[cancel]"` — per-state matrix incl. Cancelling watchdog finalize (executor that ignores cancel + short `SICNU_TASK_CANCEL_TIMEOUT_MS`), exactly-once callback count == 1, late engine record ignored. | pending |
| O4 | Provider crash/retry: old job record never pollutes the retry job; transient error → in-place retry with fresh jobId; exhausted budget → Failed once. | `test_taskcenter_runtime_12.exe "[retry]"` — executor emitting `worker crashed:` transient error; assert fresh jobId, autoRetryAttempts, single terminal callback; late stale record dropped. Existing `test_task_center`/`test_execution_plane_8` retry cases must stay green. | pending |
| O5 | Core TaskCenter/execution tests pass twice consecutively; no data-race design gaps (all shared state under m_mutex or lock-free staging; no engine calls under m_mutex). | `ctest -R "test_task_center|test_taskcenter_runtime_12|test_scheduler3|test_task_resource_budget|test_execution_plane|test_job_engine|test_workflow_cancel|test_execution_telemetry_11" -j1` ×2 clean runs; reviewer sign-off on lock discipline. | pending |

Secondary (WP completeness, not gating):
- O6 observability: `TasksSubmitted/TasksFailed/TasksCanceled` counters increment on every path; QueueWait/ResourceWait/cancel-latency events emitted when `SICNU_TELEMETRY=1`. → `test_taskcenter_runtime_12.exe "[telemetry]"`.
- O7 admission weight dims: `execution.cpuThreads`/weight keys honored by admission; `admissionSnapshot` agrees with the real pass. → `test_taskcenter_runtime_12.exe "[admission]"`.
- O8 estimator never runs under `m_mutex` (enqueued tasks carry a warm estimate). → code-review assertion + regression test where resolver panics if called under lock is impractical; instead assert warm-cache behavior via `admissionSnapshot` consistency + targeted unit test.

Gate rule: O1–O5 must pass twice consecutively before PR; independent review P0/P1 = 0.
