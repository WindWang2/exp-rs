# ADR 0052: JobEngine Record Retention Policy (Prune Completed Jobs)

## Status
Accepted

## Context
`JobEngine` retained every `JobRecord` — full `logLines` + `result` JSON — for
the process lifetime; only `shutdownForTests()` cleared `m_jobs`, so production
grew without bound. `TaskCenter::clearCompletedTasks` did not propagate to the
engine.

## Decision
1. **Add `JobEngine::pruneCompleted(maxKeep)`**: removes the oldest terminal
   records beyond `maxKeep` ("oldest" = the record's own timestamps:
   `finishedAtMs`, then `createdAtMs`, then `id`), returning the count
   removed; queued/running records are never touched. `clearCompleted()` is
   `pruneCompleted(0)`.
2. **Add `JobEngine::removeCompleted(jobIds)`**: exact-set terminal-record
   eviction so TaskCenter prunes only the cleared tasks' records and
   untracked engine jobs (direct submissions) survive.
3. **`TaskCenter::clearCompletedTasks` propagates**: after clearing its own
   map it drops the cleared tasks' dispatch/dedup state and removes exactly
   their engine records; straggler terminal records for pruned jobs no-op.

## Consequences
- **Bounded retention**: production no longer grows without limit; `list()`
  gains a production caller as the inspection seam.
- **Final notifications never lost**: records are pruned only after their
  terminal state was set under `m_mutex`, and the listener receives a copy.
- **Consistent semantics**: after `clearCompletedTasks`, neither layer
  retains the cleared records; `m_taskByJobId` / delta maps stay leak-free.
