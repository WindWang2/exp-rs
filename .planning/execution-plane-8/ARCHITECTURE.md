# ARCHITECTURE — Execution Plane 8.0

## WP-A: admission complexity redesign

### Baseline complexity (master `322dfd3876`)

`TaskCenter::processNextQueuedTasks()` runs under `m_mutex` on EVERY task
enqueue, submit, and terminal transition, and each run costs:

1. O(n) scan of ALL tasks (active-set counters recomputed from scratch);
2. O(n·p) scan of all tasks to collect parent-satisfied candidates
   (p = parents per task), plus O(n log n) sort of the eligible list;
3. per-candidate work repeated every pass even for candidates that cannot
   launch: placeholder substitution over every parameter string
   (`applyPlaceholdersForTask`), dispatch-fingerprint verification, and a
   second O(eligible) loop for the WaitingResource status flip.

With n queued tasks drained through k worker slots the total is O(n²) per
scan component. 7.0 measured 10k short jobs ⇒ 83.3s drain (Debug).

Secondary cliffs on the same path: `JobEngine::tryPickJobLocked` scans the
whole job deque per pick (exclusive scan + priority scan, O(q) each) and
`cancel()` linear-searches the deque; `collectTransitiveDescendantsLocked`
rebuilds a children map over all live tasks per cancel/failure.

### Redesign (this branch)

**TaskCenter — incremental bookkeeping + indexed ready heap.**

1. Active-set counters maintained incrementally by a single transition
   helper (`setTaskStatusLocked`): totalActive, activeMb, activeIsolated,
   activeIoHeavy, activeUsage2 (tempDisk/VRAM), activeByProfile. Membership
   predicate unchanged: {Running, Cancelling, Dispatching, Paused}.
   Estimates/dims keep using the existing per-task caches (#702) so the
   accounting update is O(1).
2. Parent gating via `m_incompleteParentCount[taskId]` (number of
   non-Completed, non-missing parents), decremented when a parent completes
   (the only non-cascade parent exit). Missing parents count as satisfied
   (existing rule).
3. Ready candidates (autoDispatch, no jobId yet, parent-satisfied,
   Queued/WaitingResource) live in a binary min-heap keyed
   `(priority, epoch, taskId)` with lazy invalidation (PRIORITY-MAJOR —
   adversarial-review fix: an epoch-major key starved a previously-blocked
   HIGH-priority candidate behind a steady low-priority arrival stream,
   which the old full sort never did):
   - `priority` is the old full sort's primary key, strictly.
   - `epoch` = pass number in which the candidate was last admitted-blocked
     (0 = never blocked). Within one priority: fresh candidates keep the
     historical `(priority, taskId)` order; blocked candidates rotate FIFO
     across passes (never-starve among same-priority blocked work).
   - Each heap entry carries a serial; a per-task `m_readySerial` detects
     stale/duplicate entries on pop (task canceled / retried / already
     dispatched ⇒ skip and drop). No erase-on-cancel needed.
4. Per-pass scan bound: at most `max(32, 4 × globalMax)` candidates are
   popped per pass. Non-blocked gates (global slots, RSS watermark) end the
   pass immediately (break, popped candidates pushed back, semantics equal
   to today's `break`). Per-candidate-blocked candidates get `epoch = next
   pass` and are pushed back. Bound guarantees O(bound · log n) per pass.
5. Placeholder application + dispatch-fingerprint verification move to
   STAGING time (after all gates pass, exactly once per task, when it
   becomes Dispatching). Same inputs as today (parents are Completed for
   any launch-eligible candidate); strictly less work and the same final
   parameterMap. Non-autoDispatch (manual/QgsTask) tasks keep the legacy
   every-pass placeholder application — bounded by live manual tasks and
   unchanged behavior for the GUI flows.
   The WaitingResource flip is now computed for the popped candidates only
   (status truthfulness preserved: never-popped candidates stay Queued,
   which is also truthful — they were not examined this pass).

**JobEngine — priority-bucketed queue with O(1) exclusive accounting.**

- `m_queue` (deque) is replaced by `std::map<int, std::deque<jobId>>`
  buckets keyed by `request.priority` + `m_queuedExclusive` counter.
  `tryPickJobLocked`: exclusive-due ⇒ take front of the lowest-priority
  exclusive bucket after drain; otherwise take front of the lowest
  non-draining priority bucket. O(log P) per pick (P = distinct priorities
  in queue, ≤ 3 in practice). Submission order preserved within a priority
  (FIFO). `cancel()` removes via a per-job bucket iterator hint (erase by
  value from that priority's deque, O(q_p) worst case but only on cancel,
  never on the pick path).
- Invariant notes: `request.priority` and `request.exclusive` are immutable
  per job (set at submit, never mutated) — verified against job_types.h
  usage; bucket key therefore cannot go stale.

### Invariants proven (tested in test_execution_plane_8)

- I1 priority order: for fresh candidates, launch order is (priority, taskId).
- I2 no stranding: every queued task is either launched, canceled, failed by
  cascade, or finalized by shutdown — never permanently skipped (heap serial
  invalidation cannot lose a live candidate: every path that invalidates a
  heap entry also changes the task's status away from the ready predicate
  or re-promotes it).
- I3 never-starve: an admitted-blocked candidate is re-examined in FIFO
  epoch order among same-priority blocked candidates each pass (subject to
  the bounded scan; fresh same-priority candidates ahead of it mirror the
  old continue-on-block behavior). With its gate open it launches.
- I4 cancel-during-admission: cancel/terminate between pop and staging is
  caught by the pre-submit recheck (existing #799 guard chain).
- I5 counters convergence: active counters are derived, not authoritative;
  a debug-only parity check in tests recomputes them from the task map and
  asserts equality after drain.

## WP-C: worker process containment

New `worker_process_guard`:
- POSIX: the child becomes a session/process-group leader at spawn
  (QProcess CreateNewSession; setpgid fallback below Qt 6.7); escalation
  and teardown signal the whole group (`kill -pid`) with a SIGKILL sweep
  that runs even after a clean leader exit (group survivors cannot hide
  behind the leader's cooperative death). This is teardown/escalation-time
  containment: a host dying without teardown (SIGKILL/crash) does NOT kill
  the POSIX tree — stdin-EOF remains that path (Windows is the platform
  with the host-death guarantee).
- Windows: Job Object created on spawn with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; the child is assigned to the job in
  `armAfterStart` right after `start()+waitForStarted()`. Residual window
  (stated precisely): a worker spawning helpers inside the start→arm
  interval escapes the job — QProcess exposes no post-CreateProcess hook,
  and the CREATE_SUSPENDED + thread-enumeration resume trick was rejected
  as untestable on this host (Linux-only verification). Job handle closed
  in teardown (kills the whole tree even if a grandchild escaped
  stdin-EOF semantics).
- Heartbeat (optional, wire-compatible): worker emits `{"op":"heartbeat"}`
  every 15s from a watchdog thread while a job runs; host treats
  `no frames for heartbeatTimeoutMs` (default 0 = off; configurable) as a
  hang ⇒ normal cancel-escalation ladder. Old hosts ignore the op (already
  coded); old workers simply never send it (feature stays off).

## WP-E: resume identity 3.0

- `StepPlan.operatorImplStamp` (additive optional JSON field): the
  operator's implementation identity at completion (schema JSON hash +
  determinism grade + fingerprint contract version, same recipe as the
  execution-fingerprint implIdentity). Recorded on the Completed transition.
- `resumeRun` gate extension (fail-closed): a served step whose recorded
  stamp differs from the CURRENT operator's identity (or that has no stamp
  but the operator is now resolvable) re-executes. Purely additive: legacy
  checkpoints without the field keep today's behavior EXCEPT they now also
  re-execute when an operator identity cannot be proven stable (documented;
  same conservative direction as #750).
- Moved output: when the recorded path fails the existence gate, resume
  consults the persistent content-addressed pool via the step's recorded
  digest; on a verified pool object the step's output is re-hydrated to the
  declared path (digest re-verified after copy, same materialization rules
  as cache serve) and the step is served. No pool object ⇒ re-execute
  (unchanged).

## WP-F: remote identity into the execution fingerprint

- `execution_identity_resolver` gets a DEFAULT implementation composed at
  host wiring time: remote http(s) paths are probed via
  `RemoteSourceValidator` (RFC 7232). Identity token = strong-ETag match
  confirmed at probe time, fail-closed otherwise: weak ETag,
  Last-Modified only, offline, timeout, or no validator ⇒ empty token ⇒
  input uncacheable (conservative, matching the seam contract). VSI-style
  datasources are NOT probed — they stay uncacheable (conservative).
- Wiring point: `fingerprintInputsForOperatorParams` consults the installed
  resolver for remote-kind candidates that the catalog cannot resolve
  BEFORE failing; an empty resolver verdict keeps today's fail-closed
  behavior (input unresolved ⇒ uncacheable). The probe is bounded and only
  runs when the execution cache is enabled (default off ⇒ zero new I/O).
- LOCK DISCIPLINE (review fix): the probe is network I/O and NEVER runs
  under TaskCenter::m_mutex. TaskCenter warms the session cache on the
  submitting thread BEFORE taking the lock (`warmExecutionIdentityCache`);
  the resolver returns entries younger than a TTL
  (SICNU_REMOTE_IDENTITY_TTL_MS, default 5 s) without any network round
  trip, so the under-lock consult is pure bookkeeping. The cache mutex is
  never held across I/O (P0 fix).

## WP-I: trace events

Emit sites (all `Trace::enabled()`-gated, correlation id = task id /
pipeline id / jobId already carried by TraceEvent fields):
TaskCenter: admitted, held(resource:<dim>), dispatched, retry, cancel,
terminal; worker pool: spawn, handshake, crash, timeout, cancel-ack;
cache: hit/miss/store; resume: served/rehydrated/reexecuted. Bounded by the
existing trace sink contracts.
