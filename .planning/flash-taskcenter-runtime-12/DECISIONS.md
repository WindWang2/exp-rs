# DECISIONS — flash-taskcenter-runtime-12

## D1 — Fairness: aging promotion in the ready heap (starvation-free)

**Problem.** The ready heap is strict priority-major `(priority, epoch, taskId, serial)`: a sustained High stream starves Low forever — epoch rotation only shuffles same-priority blocked candidates. `agedPriority()` exists in `task_resource_budget2.h` but is unwired.

**Design.** Per-task `enqueueStamp` (steady_clock) captured at submission. A per-pass *aging sweep* in `processNextQueuedTasks` walks live candidates (bounded by the new pending cap), recomputes `effectivePriority = max(0, base - waited/interval)`; on improvement, re-push with fresh serial (lazy invalidation handles the old entry). Interval: `SICNU_TASK_AGING_MS` (default 5000; 0 disables). Alternative rejected: wall-clock comparator in the heap (breaks strict-weak-ordering invariant); pop-time re-push (adds a pass of delay, harder to reason about).

## D2 — Workload lanes: LatencyClass on the task

`AlgorithmTaskInfo.latencyClass`, resolved at enqueue: explicit request value (new optional `ExecutionRequest.latencyClass`, plumbed `ExecutionPlane::submit` → `enqueueTask(latencyClassOverride)`) → else `source`-map. Shipped map (see `latencyClassForSource`): `gui|ui|dialog|toolbox|task_panel|module|guided_lab|app|agent|mcp|pi`→Interactive, `prefetch|cache|batch|batch_dialog`→Batch, default (workflow/cli/plane/empty) →Background. Wired into `ResourceRequest.latencyClass` for the interactive-reserve gate (`interactiveReservePercent`, default 25 per existing SchedulerLimits but only meaningful once weight caps are set) and used for observability tags. Not a preemption mechanism.

## D3 — Backpressure: bounded pending task count

`TaskCenter::setMaxPendingTasks(n)`; default `SICNU_TASK_MAX_PENDING` else **4096**; 0 = unbounded (compat escape). Counted = non-terminal tasks in `m_tasks` (the live set: Queued/WaitingResource/Dispatching/Running/Cancelling/Paused). `enqueueTask`/`submitJobImpl`/`submitPipeline` return -1 on refusal (same sentinel as shutdown), emit `TasksRefused` counter + `admission/queue_full` trace; no task record is created (submitPipeline refuses atomically before creating any step). Alternative rejected: block-the-submitter (turns backpressure into deadlock risk for worker-originated submissions; refusal is truthful and composable).

## D4 — Cancelling watchdog (bounded cancel latency)

Entering `Cancelling` stamps `cancelDeadline` (steady clock, `cancelWatchdogMs`; default `SICNU_TASK_CANCEL_TIMEOUT_MS` else 30000; 0 = off). Enforcement — SHIPPED as a dedicated watchdog thread (~25 ms tick) PLUS an opportunistic check at the top of `onJobRecord`, NOT the originally-sketched `QTimer::singleShot`. Rationale for the deviation (review): `processNextQueuedTasks` is always invoked with `m_mutex` already held and `enforceCancelDeadlines()` takes `QMutexLocker(&m_mutex)` — a direct call at its top would deadlock on the non-recursive mutex, and a `QTimer` only fires when the TaskCenter thread pumps an event loop, which is exactly the failure mode the watchdog exists to cover (hung worker ↔ wedged event delivery). The dedicated thread enforces deadlines independent of event-loop liveness — strictly stronger bounded-latency than the timer design — while `onJobRecord` shortens the observed latency on the common path. `ensureWatchdogStartedLocked` early-outs during shutdown so cancel-all never respawns it, and enabling the watchdog via `setCancelWatchdogMs` re-arms deadlines already in `Cancelling`. Finalization: `Canceled` preserving the request's typed reason (the first `cancelTask` reason wins for the whole Cancelling episode — resurrection via auto-retry clears the stamp so a re-cancel stamps fresh), `cancel/watchdog_timeout` trace + `CancelWatchdogFired` counter; the job→task mapping is detached BEFORE finalizing so late engine records are foreign no-ops, and the never-arriving record is bounded by the watchdog itself.

## D5 — Unified weight admission (WP1 completion)

Extend `AdmissionDims` → `{tempDiskMb, vramMb, cpuThreads, diskReadWeight, diskWriteWeight, networkWeight, ioHeavy}` parsed at enqueue warm (new optional descriptor keys `execution.cpuThreads`, `execution.diskReadWeight`, `execution.diskWriteWeight`, `execution.networkWeight`; absent = 0/no gate). `m_active.usage2` charges them; new TaskCenter setters for `SchedulerLimits` dims (`setCpuThreadLimit`, `setIoWeightLimits`, `setInteractiveReservePercent`). Weight caps default: diskRead/Write/network = 100 (already the SchedulerLimits default) BUT gated only when a task actually declares a weight — wait, no: SchedulerLimits defaults weight caps to 100 already; enabling them by default would gate ioHeavy-declared tasks against an implicit 100 — that's the intended default (weights are 0..100 shares; a task declaring none adds 0). Safe default-on. cpuThreads: default cap 0 (off) — RAM/slot gates already bound; opt-in.

## D6 — Estimator warming under lock (residual #1097)

`enqueueTask`/`submitPipeline` warm `resolvedEstimateMb` lock-free alongside dims (call the injectable resolver outside `m_mutex`, seed `m_estimateMbCache`/`m_resolveMbCache`). `taskEstimateMbLocked` keeps a cold-path fallback but every production path seeds first. `admissionSnapshot` resolves the candidate estimate+dims outside the lock, then evaluates ALL gates (tempDisk/vram/ioHeavy/isolated/weights) against the active set — preflight agrees with the real pass.

## D7 — Telemetry wiring (dead ends closed)

- `TasksSubmitted++` on every accepted submission (enqueueTask/submitJobImpl/submitPipeline per task), `TasksRefused++` on bound refusal.
- `TasksFailed++`/`TasksCanceled++` on terminal transitions (markTaskFailed/markTaskCanceled real path only).
- `QueueWait` event (enqueue→Dispatching), `Dispatched` event at staging, `ExecutionStart`/`ExecutionEnd` bridged from engine records (already emitted by engine as trace; mirror into telemetry with duration), cancel latency event (`cancel` + valueNanos) on Canceled transitions, `RssSample` once per admission pass that examines candidates (emitted in `processNextQueuedTasks` when the ready heap is non-empty — degenerate empty-heap passes don't spam; taskId −1, detail `admission_pass`). All no-cost when telemetry disabled.

## D8 — Fault injection at scheduler seams

Two `SICNU_FAULT_POINT` sites: `taskcenter.dispatch` (drop a staged launch before engine submit — exercises stranded-dispatch paths) and `taskcenter.jobrecord` (drop/swallow one listener record — exercises the watchdog + late-record handling). Test-only arm via `fault_registry`; inert in production.

## D9 — API compatibility

All new TaskCenter setters default to master behavior where feasible: aging ON (interval 5000ms, env kill-switch — scheduling order is not a semantic contract, and starvation-freedom is the track goal); pending cap ON at 4096 (escape 0); cancel watchdog ON at 30s (escape 0); weight dims gated only by declared weights (absent = no gate); cpuThreads cap off by default. `enqueueTask` refusal sentinel = -1 (same as shutdown) — callers already branch on it. `ExecutionRequest` gains optional `latencyClass` (default Background via source map).
