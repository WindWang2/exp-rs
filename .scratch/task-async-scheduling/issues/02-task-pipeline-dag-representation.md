# 02 — Task Pipeline DAG Representation & Dependency Gating

**Type:** wayfinder:grilling

## Question

How should parent-child task dependencies (`QList<long> parentTaskIds`) be represented and gated in `TaskCenter` so that child tasks remain in `Queued` state until all parent tasks finish with `Completed` status?

## Blocked by

01 — Priority Queueing & Resource Throttling Strategy

**Status:** closed

### Resolution
- Added `QList<long> parentTaskIds` to `AlgorithmTaskInfo`.
- Child tasks remain `Queued` until all parents reach `Completed`.
- Parent completion unblocks child tasks; parent failure cascades cancel to downstream tasks.
- Documented in [0003-dag-task-pipeline-dependencies.md](../../docs/adr/0003-dag-task-pipeline-dependencies.md).
