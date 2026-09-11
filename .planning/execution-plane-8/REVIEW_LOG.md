# REVIEW LOG — Execution Plane 8.0

## Self-review findings during implementation (pre-adversarial)

| # | Finding | Class | Disposition |
|---|---|---|---|
| S1 | Guard pimpl (`unique_ptr<Impl>`) broke pool compilation (deleter instantiation on incomplete type via in-place Worker construction) | P0 (build) | Fixed: plain `void*` platform handle member; no pimpl |
| S2 | `execution_fingerprint.h` referenced `PoolObject` without its definition | P0 (build) | Fixed: include `artifact_object_pool.h` |
| S3 | `makeImplementationIdentity().toHex()` wrapped in `QString::fromUtf8` (type error) | P0 (build) | Fixed: direct QString |
| S4 | Catch2 chained-comparison in ep8 dynamic-availability test | P0 (build) | Fixed: parenthesized |
| S5 | Global-hold parity flip initially keyed off `m_readySerial.constBegin()` (lowest task id) instead of the heap head | P1 (behavior) | Fixed: peek `m_readyHeap.top()` + serial validation |
| S6 | Moved-output re-hydration followed by the stat-identity gate would always fail (restored file has fresh mtime) — and a SECOND resume would re-execute a servable step | P1 (correctness) | Fixed: converged stat identity in memory AND in the persisted plan after a verified restore; digest re-verification remains the actual proof |
| S7 | `remote_identity_resolver` initially used QString/QMutex in the Qt-free geospatial layer | P1 (layering) | Fixed: std::string/std::mutex; TaskCenter adapts to the data-layer type |
| S8 | Per-candidate "held" trace at 5 gate sites = duplication noise; single emit at the WaitingResource flip covers all examined-and-held candidates | P3 | Fixed by consolidation |
| S9 | Engine exclusive pick's defensive branch returns nullopt after decrementing `m_queuedCount` — consistent (id was not queued) but worth review attention | P3 | Documented; unreachable under current invariants (cancel mutates record+buckets in one critical section) |
| S10 | Manual (non-autoDispatch) tasks keep the legacy per-pass placeholder pass via `m_manualQueued` — list membership must stay exact | P2 (verify in review) | Centralized in `setTaskStatusLocked` + `promoteChildrenOfLocked`; ep7 GUI-flow regressions cover |

## Adversarial review (two independent read-only reviewers, post-freeze)

Both reviewers verified the diff line-by-line against the worktree. Merged
triage of their findings:

| ID | Finding (class) | Disposition |
|---|---|---|
| P0-1 (A+B) | Remote identity resolver held the session-cache mutex across probe/revalidate network I/O and re-locked it in the Changed branch — guaranteed self-deadlock on the feature's primary scenario, taking TaskCenter::m_mutex down | **FIXED**: mutex now guards only the map; network runs unlocked; results published under the lock; TTL entry added |
| P1-1 (A+B) | Resolver consult ran under TaskCenter::m_mutex (seconds-scale network in the scheduler lock) | **FIXED**: TaskCenter warms the session cache lock-free on the submitting thread (`warmExecutionIdentityCache`) before the mutex section; TTL (5 s default) makes the under-lock consult pure bookkeeping |
| P1-2 (B) | `currentJobId` in sicnu_worker written by the main loop while the heartbeat thread read it under stdoutMutex (no happens-before) | **FIXED**: publish under stdoutMutex |
| P1-3 (A+B) | Cache-hit/miss trace lines touched `m_tasks` outside m_mutex (phantom insert + data race) | **FIXED**: `PendingLaunch.algorithmId` captured at staging; traces use it |
| P1-4 (A) | `m_manualQueued` leaked on every submitJob/dispatched retry (arming flips autoDispatch without removing the manual-list entry) — reintroduced quadratic per-pass placeholder cost | **FIXED**: `m_manualQueued.removeOne(taskId)` at arming |
| P1-5 (A+B) | Epoch-major heap key starved a previously-blocked HIGH-priority candidate behind fresh lower-priority arrivals (regression vs the old full re-sort) | **FIXED**: key is priority-major `(priority, epoch, taskId)`; ARCHITECTURE.md invariant text corrected |
| P1-6 (B) | ARCHITECTURE.md described a CREATE_SUSPENDED assign-resume Windows flow that was not implemented | **FIXED (docs)**: precise wording — assignment happens post-start; the start→arm escape window is documented; the suspend trick rejected as untestable on this host |
| P2-1 (A) | Resume swap-merge dropped `operatorImplStamp` → next resume re-executed a provably-valid step | **FIXED**: stamp copied with the other identity fields |
| P2-2 (A+B) | Estimate/dims caches cleared while tasks active → unsigned counter underflow wedging admission | **FIXED**: `purgeAdmissionCachesForIdleTasksLocked` keeps active tasks' charged entries |
| P2-3 (B) | terminateTree returned after clean leader exit without the group SIGKILL → SIGTERM-immune helpers survived | **FIXED**: group SIGKILL fires even after successful reap (pid-recycle window documented) |
| P2-4 (B) | Header overclaimed POSIX host-death containment | **FIXED**: wording scoped (POSIX = teardown/escalation-only; Windows = kill-on-close host-death guarantee) |
| P2-5 (B) | Scaling test self-disabled on fast machines and counted Failed as terminal | **FIXED**: always-on ratio with floor-clamped denominator + completed==n + absolute bound + best-of-3 sampling |
| P2-6 (B) | Tree containment had no test exercising a tree | **FIXED**: `__spawn_helper__` worker hook + ep8 e2e (SIGTERM-immune helper reaped by group SIGKILL) |
| P3 | 8 nits: dead `rehydrated` variable; hang-timeout error missing stderr tail; heartbeat capability not recorded by the pool; engine defensive stale-entry handling made drop-and-continue; degenerate test StepConnection + dead call; no-op limit resets in test teardown; untracked-pipeline identity hashing in onTaskUpdated; resolver header TTL wording; CAPABILITY_MATRIX vsi wording | **FIXED** (all) |

### Post-review regression found in local verification (self)

| ID | Finding | Class | Disposition |
|---|---|---|---|
| V1 | Full-suite run: require-mode retry test left the task in WaitingResource forever. Root cause: staging set `isolatedRoute` AFTER the status transition — the enter-transition charged `m_active.isolated` from a still-false flag while the leave-transition decremented for a true one (unsigned underflow → routed tasks blocked permanently) | P0 | **FIXED**: `isolatedRoute` set before `setTaskStatusLocked(Dispatching)`; isolated-flag accounting verified by the ep7 fail-closed + route e2e cases (passing) |
| V2 | One ep7 full-suite run under machine load ~20 (three parallel track builds + a partially-stale test binary from concurrent relinking) showed ±1 retry-run-count anomalies (flakyRuns 3/2, 2/3). Not reproduced in 9+ subsequent full-suite runs incl. three under deliberate synthetic CPU load, with task-log dump instrumentation active; every semantic assertion (autoRetryAttempts, status, DAG wiring) passed in all clean runs. Instrumentation left in place for future diagnosis | P3 (open, unreproduced, load-correlated) | Documented; monitoring via ep7 suite |

### Reviewer-verified clean (record)

Status-transition seam completeness, heap serial invalidation, engine bucket
count symmetric mutation, exclusive FIFO order, fingerprint additive-ness
(`;rid=` byte-compat), resume gate provider-step compatibility, checkpoint
JSON both-direction compat, kill-site inventory (no double-kill, no missed
tree), worker exit paths, hang-window mechanics, Qt 6.7+ API floor, CMake
target wiring.

### Verdict after remediation

All P0/P1 fixed and verified by the local suites; all P2 fixed; every P3
fixed. Reviewer condition "not mergeable as-is" is resolved.
