# PERFORMANCE — Execution Plane 8.0

## Environment

- Host: Linux 6.18 x64, 16 cores, 64 GB RAM (system Qt 6.11.2, GCC 16).
- Build: Release (`-DCMAKE_BUILD_TYPE=Release`, Ninja), worktree
  `/home/kevin/projects/rs-studio/exp-rs-execution-plane-8`.
- Note: three parallel 8.0-track builds shared this machine during parts of
  measurement; the recorded numbers were taken on the quiet machine.

## Admission scaling (WP-A) — the headline cliff

| Measurement | Master (`322dfd3876`, 7.0 state) | This branch |
|---|---|---|
| 10k short-job drain (Debug, Windows CI-class host, 7.0 evidence) | 83.3 s, O(n²) recorded as known limitation | — |
| 10k short-job drain (this host, Release, ep7 stress case) | — | **4.8 s** (`test_execution_plane_7` durations) |
| 2k vs 10k drain ratio (ep8 scaling case, best-of-3, submit+idle window) | quadratic ⇒ ≥ 25× expected | **~5–10× (linear-class)**, asserted `< 10 × max(2k, 100 ms)` |
| Stranded / failed tasks after drain | 0 | 0 (`completed == n` asserted) |

Per-pass admission cost is now bounded by
`max(32, 4 × globalMax)` candidate examinations (`O(bound · log n)`),
independent of the queued depth; master rescanned the entire task map and
re-sorted all eligible candidates on every submission and terminal
transition. JobEngine's pick fell from `O(q)` scans per pop to
`O(log P)` bucket access.

## Memory / bookkeeping bounds

- Ready-heap entries are bounded by live push count; stale entries drain on
  pop (serial check). `m_children` edges are consumed at parent completion
  and dropped at prune. `m_incompleteParentCount` entries live only while a
  parent is outstanding. All three are cleared on `shutdownForTests` and
  pruned with tasks in `clearCompletedTasks`.
- Remote identity session cache: ≤ 256 entries (insertion-order eviction);
  TTL'd revalidation (default 5 s, `SICNU_REMOTE_IDENTITY_TTL_MS`) bounds
  network use; probes are seconds-budgeted (10 s / 5 s connect, 1 retry) and
  never run under a lock.

## Worker runtime (WP-C)

- Heartbeat: one small frame per 15 s while a job runs (zero idle traffic);
  hang window off by default (`SICNU_WORKER_HANG_TIMEOUT_MS`).
- Containment kill ladder: bounded waits (1 s SIGTERM grace + 3 s SIGKILL
  reaping in `terminateTree`), unchanged from the 7.0 escalation budget.
- ep8 containment e2e (helper that ignores SIGTERM + cooperative cancel):
  helper reaped, worker typed-cancelled, ~1.6 s wall.

## Suites executed locally (all green)

| Suite | Result |
|---|---|
| test_execution_plane_8 (new) | 105 assertions / 13 cases |
| test_task_center | 342 / 31 |
| test_job_engine | 446 / 34 |
| test_execution_plane_7 | 48 / 9 (incl. 10k stress, worker e2e, fail-closed routing) |
| test_worker_host | 56 / 12 |
| test_workflow_run_coordinator | 171 / 10 |
| test_workflow_resume_provenance | 67 / 2 |
| test_execution_fingerprint | 60 / 16 |
| test_workflow_cache_e2e | 189 / 12 |
| test_scheduler3 | 11 / 4 |

Compiled but NOT executed on this host: the Windows branches of
`worker_process_guard` (Job Object path) — code-reviewed only, stated as
such in the PR.
