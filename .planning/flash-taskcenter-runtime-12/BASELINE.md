# BASELINE — flash-taskcenter-runtime-12 (Phase 0, 2026-09-20)

## Git facts (freshly fetched)

- `git fetch` required the system proxy (`-c http.proxy=http://127.0.0.1:7890`); github.com:443 direct is unreachable, api.github.com works via `gh`.
- `origin/master` = **`adf8f98952422fe9c386c56d64d5fb6a4a6642f1`** — same as local master (no drift).
- Recent master: `adf8f9895` docs(agents); `fe7da0622` StepFun preset; `2761a6857` merge #1115 (deep-review wave-2, #1097); `08911c838` merge #1113; `8a52c5b47` #1112; `4faa6a4a5` #1111; `9a9d2e811` #1110; `5ed2d11a4` #1109; `10a904915` #1070 (execution publication gate).
- **Open PRs: 0. Open issues: 0.** (gh, 2026-09-20)
- Remote residual branches (all superseded history; their PRs #1059/#1069/#1071/#1072/#1073 closed unmerged, issues re-fixed by the #1100–#1115 wave): `agent/ds41-http-fetch-strict`, `agent/ds41-pipeline-drag-lifetime`, `agent/flash-{data-transaction-integrity,geo-fabric-integrity,lab-foundry-determinism,mcp-containment-routing,processing-atomic-errors,workflow-integrity}`, `agent/glm53-{desktop-lifecycle,plugin-sdk-trust}`, `fix/{ci-master-unblock,r2-ci-protobuf-multimode,review-issues-1033-1056}`. Not used as a base; no cherry-picks.
- Local worktrees (parallel tracks, all at adf8f9895): `ds41-{build-portability,dev-worktree-tooling,fuzz-boundaries}`, `glm53-{mission-workbench-12,scientific-verification-12}` (locked = active sessions). Their domains (workbench UI / scientific verification) do not overlap `src/processing/framework` scheduler core; `tests/CMakeLists.txt` is the shared append point.

## Relevant merged history (read)

- **#1115** `28d3ffcb9` — task_center.cpp: stale job-record rebind guard (clientTag recovery refused when task tracks a different jobId), `admissionDimsLocked` never lazy-constructs under `m_mutex` (dims warmed lock-free at enqueue/submitPipeline), `stagedPathFor` O_EXCL.
- **#1113** `08911c838` — workflow resume/cancel wedges, worker-pool restart timer UAF, SHM params.
- **#1104** — processing write-result checks, postProcess-once on adapter failures.
- **#1070** — verification-rollback publication gate contract tests.
- **#1009** — execution runtime convergence 11.0 (governor, chunk contract, worker lease, telemetry).
- **#980** — large-scale execution engine 10.0 (memory planner, env pins).

## Capability census conclusion

TaskCenter/JobEngine already implement: incremental priority-major admission heap with epoch rotation, RAM budget + RSS watermark + per-profile/global/isolated/ioHeavy/tempDisk/vram gates, structured-concurrency ownership (I9 join), bounded transient children + transient worker allowance, `Cancelling`/`WaitingResource`/`Dispatching` states, typed `TaskCancelReason`, in-place bounded auto-retry with fresh job ids, stale-record guards, sticky shutdown, execution fingerprints/cache, telemetry + exp.trace.v1, fault registry, tile-level backpressure (BoundedChunkQueue/BoundedWriteGate/ExecutionGovernor).

**Real gaps this track owns:**
1. `TaskResourceBudget2` weight dims (cpuThreads/disk/network), `LatencyClass`, `interactiveReserve`, `agedPriority` are library-only — never wired into TaskCenter admission. Strict priority-major heap starves Low under a sustained High stream.
2. No bound on queued/pending tasks (`m_readyHeap`, `m_tasks`, engine `m_queueBuckets` unbounded) — no submission backpressure.
3. `Cancelling` has no watchdog — a silent worker strands the state until shutdown.
4. Dead telemetry: `TasksFailed`/`TasksCanceled` never incremented; `TasksSubmitted` only on submitPipeline; wait/run/cancel durations unrecorded; `ScopedTelemetrySpan` unused.
5. `taskEstimateMbLocked`/`estimateMbForAlgorithmLocked` still run the registry resolver under `m_mutex` on first resolve (residual #1097/#930 hazard — dims were warmed, estimates were not).
6. `admissionSnapshot` skips tempDisk/vram/ioHeavy/isolated gates — preflight `wouldAdmit` can disagree with the real pass.
7. No fault-injection sites inside the scheduler; no task-level stress harness (tile-level only).

## Build environment

- MSVC 2022 (cl 14.38), Qt 6.8.0 msvc2022_64 (`C:\deps\Qt`), Ninja `C:\Qt\Tools\Ninja`, CMake `C:\Qt\Tools\CMake_64\bin`.
- No compiler cache; worktree build = one full configure + build. Build helper: `build.cmd` (vcvars64 + PATH). `-j1` default, `-j2` max.
- Tests: `QT_QPA_PLATFORM=offscreen`, `ctest -R <name> -j1` inside `build-dev`.
