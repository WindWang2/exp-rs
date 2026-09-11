# ADR 0144: Execution Plane 8.0 — Indexed Admission, Worker Containment, Fail-Closed Resume & Cache Identity

- Status: Accepted (Execution Plane / Worker Runtime / Admission / Cache /
  Recovery 8.0 goal)
- Context: Execution Plane 7.0 shipped worker routing, multi-dimension
  admission, bounded auto-retry and the cache identity seam, and recorded six
  known limitations. The 8.0 goal removes the algorithmic scheduling cliff and
  closes the identity/lifecycle gaps without introducing a second scheduler:
  the chain `WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor`
  stays the only scheduling path.
- Decision:
  1. **Incremental admission, identical semantics.** TaskCenter keeps
     incremental active-set counters (single status-transition seam,
     `setTaskStatusLocked`) and a launch-ready binary heap keyed
     `(epoch, priority, taskId, serial)`. Fresh candidates keep the historical
     strict `(priority, taskId)` launch order; candidates held by
     per-candidate gates rotate FIFO across passes via the epoch (never-starve
     under steady arrivals, same relative outcome as the old
     `continue`-on-block); a serial lazily invalidates superseded entries
     without erase-on-cancel. A per-pass scan bound
     (`max(32, 4 × globalMax)` examined candidates) makes a pass
     O(bound · log n) even when every candidate is blocked. Global-hold
     observability is preserved by an O(1) WaitingResource flip of the heap
     head. Placeholder substitution and dispatch-fingerprint verification run
     once at staging (gates never read parameters). JobEngine's queue becomes
     priority buckets + an exclusive FIFO: pick O(log P), historical
     exclusive drain-then-alone order preserved.
  2. **Worker process-tree containment as an OS contract.** Each worker is
     bound to a kill-on-close construct at spawn (`worker_process_guard`):
     POSIX `setsid` process group with group-wide SIGTERM→SIGKILL ladders;
     Windows Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) assigned right
     after the synchronous CreateProcess (documented first-millisecond
     assignment window). A hung operator's helper processes can no longer
     outlive the host. Liveness frames (`heartbeat`, wire-compatible optional
     op) let a host distinguish "silent operator" from "dead process"; the
     host-side hang window is opt-in (`SICNU_WORKER_HANG_TIMEOUT_MS`, default
     off) so legitimately silent long operators are never killed by default.
  3. **Resume identity is fail-closed and implementation-aware.** A completed
     step's checkpoint records the operator's implementation identity (schema
     + determinism grade + fingerprint contract + platform version). Resume
     serves the step only when the current operator identity equals the
     recorded stamp; a mismatch, an unresolvable operator, or a legacy
     checkpoint with a now-resolvable operator re-executes (same conservative
     direction as the #750 output-identity gate). A moved output whose
     checkpoint recorded a content digest is re-hydrated from the
     content-addressed pool only after the restored bytes re-prove the digest;
     the stat identity then converges so later resumes take the normal path.
  4. **Remote input identity rides the existing fingerprint.** The 7.0
     `execution_identity_resolver` seam is activated with a default resolver
     (geospatial layer, strong-ETag-only, bounded session cache) installed by
     TaskCenter unless a host wired its own; the collector consults it before
     failing a remote input. The confirmed strong ETag is carried as an
     additive canonical field (`remoteIdentity` / `;rid=`) in the V2
     fingerprint: same ETag ⇒ same identity, changed ETag ⇒ guaranteed miss,
     weak/inconclusive/offline ⇒ uncacheable. Fingerprint contract version is
     unchanged: inputs that never used the field serialize byte-identically.
  5. **Dynamic availability and evidence.** All resource-limit setters re-run
     admission (gates only ever delay launches, so lowering a limit never
     preempts running work). The transient-retry classifier is a public
     documented seam; retry attempts, classes and budget exhaustion land in
     the task log and the unified trace.
- Consequences: the 10k short-job drain loses its quadratic term (per-pass
  cost is bounded and independent of the queue depth); worker crash/hang can
  no longer strand processes; resume and cache identity are scientifically
  trustworthy (served bytes are always digest- and implementation-proven);
  all new checkpoint/wire surface is optional and read fail-closed.
