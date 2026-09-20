# DECISIONS — flash-taskcenter-runtime-12

## D1 — Fairness: aging promotion in the ready heap (starvation-free)

**Problem.** The ready heap is strict priority-major `(priority, epoch, taskId, serial)`: a sustained High stream starves Low forever — epoch rotation only shuffles same-priority blocked candidates. `agedPriority()` exists in `task_resource_budget2.h` but is unwired.

**Design.** Per-task `enqueueStamp` (steady_clock) captured at submission. A per-pass *aging sweep* in `processNextQueuedTasks` walks live candidates (bounded by the new pending cap), recomputes `effectivePriority = max(0, base - waited/interval)`; on improvement, re-push with fresh serial (lazy invalidation handles the old entry). Interval: `SICNU_TASK_AGING_MS` (default 5000; 0 disables). Alternative rejected: wall-clock comparator in the heap (breaks strict-weak-ordering invariant); pop-time re-push (adds a pass of delay, harder to reason about).

## D2 — Workload lanes: LatencyClass on the task

`AlgorithmTaskInfo.latencyClass`, resolved at enqueue: explicit request value (new optional `ExecutionRequest.latencyClass`) → else `source`-map (`gui|dialog|toolbox`→Interactive, `agent|mcp`→Interactive, `workflow|pipeline`→Background, `prefetch`→Batch, default Background). Wired into `ResourceRequest.latencyClass` for the interactive-reserve gate (`interactiveReservePercent`, default 25 per existing SchedulerLimits but only meaningful once weight caps are set) and used for observability tags. Not a preemption mechanism.

## D3 — Backpressure: bounded pending task count

`TaskCenter::setMaxPendingTasks(n)`; default `SICNU_TASK_MAX_PENDING` else **4096**; 0 = unbounded (compat escape). Counted = non-terminal tasks in `m_tasks` (the live set: Queued/WaitingResource/Dispatching/Running/Cancelling/Paused). `enqueueTask`/`submitJobImpl`/`submitPipeline` return -1 on refusal (same sentinel as shutdown), emit `TasksRefused` counter + `admission/queue_full` trace; no task record is created (submitPipeline refuses atomically before creating any step). Alternative rejected: block-the-submitter (turns backpressure into deadlock risk for worker-originated submissions; refusal is truthful and composable).

## D4 — Cancelling watchdog (bounded cancel latency)

Entering `Cancelling` stamps `cancelDeadline` (steady clock, `cancelWatchdogMs`; default `SICNU_TASK_CANCEL_TIMEOUT_MS` else 30000; 0 = off). Enforcement: (a) `QTimer::singleShot` on the app instance (same pattern as the RSS re-arm — fires on the TaskCenter thread's event loop when one pumps), (b) opportunistic check at the top of `onJobRecord` and `processNextQueuedTasks`, (c) check inside `waitForTask`/`waitForPipeline` polls via a non-mutating observation — actually enforced by a small helper invoked from those poll loops through a const-safe path? — resolved: watchdog enforcement lives in `enforceCancelDeadlinesLocked()` called from (a)+(b), and `waitForTask`'s poll calls a private non-const `enforceCancelDeadlines()` — `waitForTask` is const, so the helper is a private `const` method mutating through `mutable`… NO: simplest correct — `waitForTask` keeps polling; the QTimer covers app-thread cases; engine-event paths cover the rest. A stranded Cancelling with a dead event loop is unobservable anyway. PLUS `shutdownForTests`/shutdown finalize already force terminal. Document: watchdog needs a pumping app event loop OR any subsequent scheduler event; tests cover both (offscreen QCoreApplication exists in all task tests). Finalization: `Canceled` with reason `Engine` + `cancel_watchdog` trace + `CancelWatchdogFired` counter; late engine records dedup'd by existing terminal guard. `m_taskByJobId` entry kept until the late record arrives (then cleaned by the normal path); a never-arriving record is bounded by the task map.

## D5 — Unified weight admission (WP1 completion)

Extend `AdmissionDims` → `{tempDiskMb, vramMb, cpuThreads, diskReadWeight, diskWriteWeight, networkWeight, ioHeavy}` parsed at enqueue warm (new optional descriptor keys `execution.cpuThreads`, `execution.diskReadWeight`, `execution.diskWriteWeight`, `execution.networkWeight`; absent = 0/no gate). `m_active.usage2` charges them; new TaskCenter setters for `SchedulerLimits` dims (`setCpuThreadLimit`, `setIoWeightLimits`, `setInteractiveReservePercent`). Weight caps default: diskRead/Write/network = 100 (already the SchedulerLimits default) BUT gated only when a task actually declares a weight — wait, no: SchedulerLimits defaults weight caps to 100 already; enabling them by default would gate ioHeavy-declared tasks against an implicit 100 — that's the intended default (weights are 0..100 shares; a task declaring none adds 0). Safe default-on. cpuThreads: default cap 0 (off) — RAM/slot gates already bound; opt-in.

## D6 — Estimator warming under lock (residual #1097)

`enqueueTask`/`submitPipeline` warm `resolvedEstimateMb` lock-free alongside dims (call the injectable resolver outside `m_mutex`, seed `m_estimateMbCache`/`m_resolveMbCache`). `taskEstimateMbLocked` keeps a cold-path fallback but every production path seeds first. `admissionSnapshot` resolves the candidate estimate+dims outside the lock, then evaluates ALL gates (tempDisk/vram/ioHeavy/isolated/weights) against the active set — preflight agrees with the real pass.

## D7 — Telemetry wiring (dead ends closed)

- `TasksSubmitted++` on every accepted submission (enqueueTask/submitJobImpl/submitPipeline per task), `TasksRefused++` on bound refusal.
- `TasksFailed++`/`TasksCanceled++` on terminal transitions (markTaskFailed/markTaskCanceled real path only).
- `QueueWait` event (enqueue→Dispatching), `Dispatched` event at staging, `ExecutionStart`/`ExecutionEnd` bridged from engine records (already emitted by engine as trace; mirror into telemetry with duration), cancel latency event (`cancel` + valueNanos) on Canceled transitions, `RssSample` once per admission pass. All no-cost when telemetry disabled.

## D8 — Fault injection at scheduler seams

Two `SICNU_FAULT_POINT` sites: `taskcenter.dispatch` (drop a staged launch before engine submit — exercises stranded-dispatch paths) and `taskcenter.jobrecord` (drop/swallow one listener record — exercises the watchdog + late-record handling). Test-only arm via `fault_registry`; inert in production.

## D9 — API compatibility

All new TaskCenter setters default to master behavior where feasible: aging ON (interval 5000ms, env kill-switch — scheduling order is not a semantic contract, and starvation-freedom is the track goal); pending cap ON at 4096 (escape 0); cancel watchdog ON at 30s (escape 0); weight dims gated only by declared weights (absent = no gate); cpuThreads cap off by default. `enqueueTask` refusal sentinel = -1 (same as shutdown) — callers already branch on it. `ExecutionRequest` gains optional `latencyClass` (default Background via source map).
