# CAPABILITY MATRIX — post-implementation state (Execution Plane 8.0)

Legend: Implemented / Partial / Stub / Refused-by-contract / Missing / Duplicated / Unverified

## Work package outcomes

| WP | Requirement | State | Evidence / location |
|---|---|---|---|
| A | O(n²)-free admission (TaskCenter) | **Implemented** | incremental active counters (`setTaskStatusLocked`), ready heap (`(epoch, priority, taskId, serial)`), bounded per-pass scan (`kAdmissionScanFloor`, `4×globalMax`), staging-time placeholder/fingerprint work. task_center.{h,cpp} |
| A | Engine queue pick O(log P) | **Implemented** | priority buckets + exclusive FIFO; `tryPickJobLocked` / `enqueueJobLocked` / `dequeueJobLocked` (job_engine.{h,cpp}) |
| A | Priority / fairness / never-starve preserved | **Implemented** | fresh candidates keep (priority, taskId) order; blocked candidates rotate FIFO by epoch; never-starve gates unchanged. Tests: ep8 priority / cancel / scaling |
| B | Resource-aware admission dims (RAM/RSS/tempDisk/VRAM/ioHeavy/isolated) | **Implemented** (7.0 base) + dynamic availability (8.0) | all resource setters re-run admission + flush (task_center.cpp) |
| B | Reservations & release | **Implemented** | Dispatching enters the active set (reservation); terminal transitions release it — incrementally maintained |
| B | Cancel while queued (incl. resource-held) | **Implemented** | lazy heap invalidation + ep8 test |
| B | Explicit unknown estimates | **Partial (unchanged)** | estimate 0 (unknown) never blocks (pre-existing registry fallback semantics). A "refuse unknown estimates when a budget is set" policy would risk global stalls and is a scheduler-policy decision for a future track — documented, not changed |
| B | Stale task cleanup | **Implemented** | `forgetDerivedTaskStateLocked` on prune; derived state reset in shutdownForTests |
| C | Process-tree containment | **Implemented** (POSIX verified, Windows compiled-only on this host) | worker_process_guard.{h,cpp}: POSIX setsid process group + group kill ladder; Windows kill-on-close Job Object assigned right after start. Windows branch NOT compiled locally (Linux host) — code-reviewed only, stated honestly |
| C | Hang detection | **Implemented** (opt-in) | worker heartbeat frames (15s while a job runs, wire-compatible) + `SICNU_WORKER_HANG_TIMEOUT_MS` host window (default 0 = off) |
| C | Shutdown with in-flight work | **Implemented** (7.0 base, regression-guarded) | ep7 shutdown case + pool destructor drain |
| C | Owner-thread marshal | **Refused-by-contract (documented)** | JobEngine worker threads are plain std::thread without an event loop — a Qt marshal target does not exist. Force-retire cross-thread is bounded and mutex-guarded (7.0 review-accepted); the OS-level kill now makes foreign-thread teardown tree-wide |
| D | Retry classification formalized | **Implemented** | `isTransientExecutionError` public documented seam; budget clamp 0..3; per-attempt log + trace event; budget-exhausted evidence line on final failure |
| D | No retry after cancel / no silent in-process fallback | **Implemented** (7.0 base, preserved) | routing re-engages on retry (`!jobExecutor && shouldRunIsolated`); cancel never retried |
| E | Completion identity (stat+digest) | **Implemented** (#750 base) + **operator implementation stamp** (8.0) | `StepPlan.operatorImplStamp` recorded at the Completed fold; resume compares current identity (schema+grade+contract+platform) |
| E | Moved output recovery | **Implemented** | digest-verified re-hydration from the content-addressed pool (`rehydrateMovedOutput` + `ArtifactObjectPool::objectByDigest` + `ExecutionResultCache::pooledObjectByDigest`); stat identity converges post-restore |
| E | Legacy checkpoints | **Implemented (fail-closed)** | no stamp + operator resolvable now ⇒ re-execute (same conservative direction as #750); unresolvable-both-sides keeps serving (pre-8.0 behavior) |
| E | Remote ETag/validator in identity | **Implemented** | see F |
| F | Remote identity → execution fingerprint | **Implemented** | `TaggedDerivationInput.remoteIdentity` (additive canonical field `;rid=`); collector consults `executionIdentityResolver` before failing remote inputs; default resolver = strong-ETag-only probing of remote http(s) URLs (`remote_identity_resolver.{h,cpp}`, geospatial layer, Qt-free; VSI-style datasources stay uncacheable), installed by TaskCenter unless a host wired its own; probes run lock-free (warm-before-scheduler-lock + TTL) |
| F | Model digest in identity | **Partial** | model-bearing operators participate through the same operator identity + determinism gate; a dedicated model-artifact digest field would belong to the model-runtime track's descriptors (cross-track seam recorded in ARCHITECTURE) |
| F | GC/reference safety | **Implemented** (verified, pre-existing) | ArtifactGC protected-provider + pool eviction unreferenced-object semantics unchanged |
| G | Committer convergence | **Implemented on current master** (7.0 limitation #4 already resolved by intervening work) | `runImageFusion` direct function has NO production callers (fusion dialog → `rs:image_fusion` operator → TaskCenter; CLI runner registers outputs with derivation); MCP commits via `buildCommittedResultPayload`; agent plans register per-step; workflow session controller commits |
| G | GUI dialog outputs registered as assets | **Partial — documented, not changed** | `RasterProcessingDialogBase::runOperatorTask` → TaskCenter completes without governed registration (outputs shown as layers). Registration of interactive experiment outputs is workbench governance policy (professional-workbench track); changing it silently here would alter GUI product behavior across ~30 dialogs. Recorded as cross-track follow-up |
| H | Locking audit | **Completed for this track's surface** (see REVIEW_LOG) | TaskCenter: network I/O NEVER under m_mutex (identity probes warmed lock-free before the scheduler lock, TTL-covered); file I/O under m_mutex re-checked (cache store stats — bounded, local); new admission state is m_mutex-only; guard kills are lock-free; engine buckets m_mutex-only |
| I | Observability | **Implemented** | trace.h events: admitted / held / retry / cancel / terminal / cache (TaskCenter), resume served/rehydrated/operator_changed (coordinator), worker spawn/crash/timeout telemetry (pool, 7.0) — all gated by one relaxed atomic load |

## Duplicated / refused work (overlap discipline)

- No second scheduler/queue/registry introduced. The heap and buckets are
  internal structures of the two existing schedulers.
- Windows Job Object / process-group logic lives in ONE new helper consumed
  by both worker hosts (pool + one-shot).
- Resume stamp recipe shared via `makeImplementationIdentity` — TaskCenter's
  fingerprint versionHash refactored onto it byte-identically (no cache
  invalidation).
- Remote identity probe reuses RemoteSourceValidator (I/O 7.0) — no new
  HTTP/ETag code.

## Cross-track seams (recorded per track discipline)

1. professional-workbench track: GUI dialog output registration policy (G).
2. model-runtime track: model artifact digest in execution identity (F).
3. verification track: owns trace sinks/tests — this track adds emit sites
   only.
