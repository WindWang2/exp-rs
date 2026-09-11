# BASELINE — Execution Plane 8.0 (audit of origin/master `322dfd3876`)

Audit date: 2026-09-10. Method: remote fetch/prune, PR/issue sweep, code read
of the execution-plane surface, test inventory, 7.0 planning-archive review.

## 1. Repository state

- Latest master: `322dfd3876` (merge of #835). No open PRs, no open issues.
- Latest 10 merged PRs (#823–#835) all merged 2026-09-09/10; none is an
  8.0-track execution-plane branch (no competing in-flight work to reconcile).
- Surviving remote `origin/feat/*` branches are all merged (heads equal to
  merge commits of #818–#832). `origin/itk-upstream/*` belongs to a different
  upstream remote, untouched by this track.
- Local worktrees for older tracks exist under `/home/kevin/projects/rs-studio/`;
  none is active upstream. This track uses a fresh worktree
  `exp-rs-execution-plane-8`.

## 2. Ownership map of the execution plane (current master)

| Concern | Authoritative file(s) | Notes |
|---|---|---|
| Task admission / DAG scheduling | `src/processing/framework/task_center.{h,cpp}` (3392 lines) | Singleton, QMutex + queued signals, completion callbacks |
| Job execution pool | `src/jobs/job_engine.{h,cpp}` | Qt-free std::thread pool; single listener slot (ADR 0051) |
| Workflow run lifecycle / resume | `src/workflow/workflow_run_coordinator.{h,cpp}`, `workflow_run.{h,cpp}`, `workflow_checkpoint.*` | StepPlan carries completion identity (#750) |
| Isolated worker selection seam | `src/processing/framework/worker_execution_route.{h,cpp}` | off/auto/require; pool started at flush |
| Warm worker pool | `src/processing/framework/local_worker_pool.{h,cpp}`, `local_worker_host.{h,cpp}`, `worker_process_io.h` | per-worker thread affinity; typed error family |
| Worker binary/protocol | `src/cli/sicnu_worker_main.cpp`, `src/runtime/worker/worker_protocol.h` | wire v1, optional caps/code/outputs/ack |
| Resource admission | `task_resource_budget.{h,cpp}` (RAM), `task_resource_budget2.h` (multi-dim), `resource_monitor.*` (RSS) | gates only delay; never-starve |
| Execution cache identity | `src/data/execution_fingerprint.{h,cpp}` (fingerprint + ExecutionResultCache), `artifact_object_pool.*` (persistent tier), `execution_identity_resolver.{h,cpp}` (SEAM ONLY) | in-memory + content-addressed tier |
| Input fingerprint collection | `src/processing/algorithms/temporal/temporal_workspace.cpp` (`fingerprintInputsForOperatorParams`) | catalog revision + chained producer + stat/digest binding |
| Remote source identity | `src/geospatial/remote/remote_source_validator.h` (RFC 7232 ETag/Last-Modified) | exists since I/O 7.0; NOT wired to cache identity |
| Output commitment | `src/processing/framework/output_committer.{h,cpp}`, `output_committer_task_center.h`, `execution_plane.cpp` (`buildCommittedResultPayload`), `tool_call_dispatcher_task_center.cpp` | commit-once cache, thread-marshaled |
| Telemetry / trace | `src/runtime/observability/execution_telemetry.{h,cpp}`, `trace.{h,cpp}` | off-by-default; bounded |
| Agent plan execution | agent_workflow_executor (per-step registration, 7.0 P1-E1) | registers governed assets per completed step |

## 3. Capability matrix (this track's scope)

| Requirement | State | Evidence |
|---|---|---|
| A. O(n²)-free admission | **Partial** | `processNextQueuedTasks` (task_center.cpp:1226) rescans ALL tasks + re-sorts eligible per call; called from enqueueTask, submitJobImpl, every terminal transition. `JobEngine::tryPickJobLocked` (job_engine.cpp:616) scans the full deque per pick (exclusive scan + priority scan); `cancel` does `std::find` over the deque. `collectTransitiveDescendantsLocked` rebuilds a children map over all live tasks per cancel. 10k drain measured 83.3s Debug (7.0 evidence) — reproduced baseline below. |
| A. Priority / fairness / never-starve / exclusive | **Implemented** (semantics to preserve) | priority sort (val, taskId); never-starve (empty-active admits); exclusive drain-then-alone in engine. |
| B. Resource admission dims | **Implemented** | RAM budget, RSS watermark, temp-disk/VRAM (budget2), ioHeavy, isolated slots, profile limits. |
| B. Dynamic availability updates | **Missing** | No API to notify admission that availability changed (e.g. budget raised): re-evaluation only on task transitions. |
| B. Stale task cleanup | **Partial** | Queued-cancel path exists; queued tasks held by resource gates are re-checked per transition; no periodic stale sweep (accepted: gates only delay; terminal transitions re-evaluate). |
| C. Worker containment | **Missing** | No Windows Job Object, no POSIX process group; host death relies on stdin-EOF; stuck worker can orphan. |
| C. Hang detection | **Partial** | 30-min job timeout only; no heartbeat liveness. |
| C. stderr ring / malformed / crash / escalation | **Implemented** (7.0) | local_worker_pool + worker_process_io diagnostics ring. |
| C. Shutdown with in-flight | **Implemented** | destructor drain + TaskCenter::shutdown finalization; tested (ep7). |
| C. Owner-thread marshal | **Partial** | Foreign idle workers force-retired cross-thread (documented, bounded). Full marshal impossible without an owner event loop (JobEngine threads are plain std::thread) — keep bounded-wait, strengthen with process-group kill. |
| D. Retry classes | **Implemented** (7.0) | `isTransientExecutionError` prefix classes; budget clamp 0..3; no retry after cancel; resurrect-in-place; typed telemetry. Gaps: evidence in result payload, formalized classification docs. |
| E. Resume stat/digest gate | **Implemented** (#750) | workflow_run_coordinator.cpp resumeRun: size+mtime+optional digest, fail-closed. |
| E. Operator implementation identity at resume | **Missing** | StepPlan has no operator impl stamp; an operator change between run and resume is invisible to the gate. |
| E. Moved output | **Missing** | Recorded path failing the gate always re-executes; no digest-addressed re-hydration (the pool supports it). |
| E. Remote ETag/validator in identity | **Missing** | resolver seam exists; nothing installs or consults it. |
| F. Execution fingerprint serve/store | **Implemented** | in-memory + persistent content-addressed tier; TOCTOU guards; path ownership. |
| F. Model digest in identity | **Unverified→Partial** | model executions flow through the same operator fingerprint only when the model op declares deterministic; model artifact digest is not part of identity (check model runtime seam before claiming). |
| G. Committer convergence | **Partial** | MCP/tool-call + workflow session controller + agent plans converge on OutputCommitter / buildCommittedResultPayload; fusion dialog routes via runOperatorTask (7.0 note may be stale — verify per path). |
| H. Locking audit | **Partial** | 7.0 fixed pool destructor race + lock-out spawn; TaskCenter listener dispatch outside mutex; remaining risks enumerated in PLAN. |
| I. Observability | **Partial** | trace events: execution_start/end (JobEngine), execution_plane submit/terminal. Missing: admission/retry/cancel/worker/cache/resume trace events with correlation ids. |

## 4. Overlap map vs recently merged PRs

- #828 (execution 7.0): all of its deliverables are in master; this track must
  NOT redo worker routing, admission dims, retry, protocol extension. It builds
  on them (A scales its admission; C contains its processes; E/F close its
  declared SEAM ONLY + resume gaps).
- #831 (verification 7.0): owns fault injection + trace + benchmarks. WP-I
  reuses `trace.h` (its surface), adds emit sites only in execution-plane code.
- #823 (cloud I/O 7.0): owns `remote_source_validator.h`; WP-F consumes it
  without modifying geospatial files (narrow adapter in the fingerprint
  collector layer instead).
- #824/#825 (dataset/model 7.0): model runtime seams — WP-F model identity
  must go through `IModelRuntime`/descriptor metadata, not a new registry.
- #832 (cartography 7.0), #827 (harness 7.0), #826 (workbench 7.0): no file
  overlap with this track except possibly help/catalog docs; shared-file edits
  minimized.

## 5. Known-issues verified against code

1. `processNextQueuedTasks` applies placeholders for every eligible candidate
   on every pass (task_center.cpp:1308) — `substituteVariantRecursive` walks
   every string param per pass; also re-applies for non-autoDispatch tasks.
   Verified: cost is per-pass per-candidate, COW only avoids deep copies.
2. `WaitingResource` flip loop iterates all eligible per pass (line 1486).
3. Engine `tryPickJobLocked`: two full deque scans per pick (exclusive + best
   priority). With engine queue kept short by admission this is secondary but
   real for direct-submit floods and exclusive jobs.
4. `cancel()` linear `std::find` over engine queue.
5. RSS watermark timer re-arm schedules `processNextQueuedTasks` via
   `QTimer::singleShot` on the QCoreApplication thread (locked) — fine.
6. `markTaskFailed` auto-retry path stages re-dispatch without re-running
   resource gates? — verified: it sets status=Queued then calls
   `processNextQueuedTasks()`, so gates re-engage. OK.
7. Cache `storeExecutionResultLocked` stats files under `m_mutex` (task_center
   lock held during stat calls) — file I/O under the TaskCenter lock; flagged
   for WP-H evaluation (bounded: few paths, local FS).
8. `serveFromExecutionCache` runs outside m_mutex. Good.

## 6. Test inventory (baseline, local)

- `tests/test_execution_plane_7.cpp` (579 lines, 9 cases incl. 10k stress)
- `tests/test_worker_host.cpp` (12 cases), `tests/test_job_engine.cpp`
  (1618 lines), `tests/test_task_center.cpp` (1607 lines),
  `tests/test_workflow_resume_provenance.cpp`, `tests/test_execution_fingerprint.cpp`,
  `tests/test_workflow_cache_e2e.cpp`, `tests/test_execution_benchmarks.cpp`.
- Baseline results to be captured in PERFORMANCE.md after the first local build.
