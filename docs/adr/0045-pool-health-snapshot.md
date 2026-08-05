# ADR 0045: Add PythonWorkerProcessPool Health Snapshot

## Status
Accepted

## Context
`PythonWorkerProcessPool` exposed `activeWorkerCount()` and
`availableWorkerCount()` as separate queries. Callers needing a composite
readiness check had to call both and combine the results. There was no
single-query snapshot that also surfaced cumulative crash-restart counts.

## Decision
1. **Introduce `PoolHealthSnapshot` struct** with `total`, `active`,
   `available`, `totalRestarts`, and a convenience `isHealthy()` predicate.

2. **Add `poolHealth() const`** to `PythonWorkerProcessPool` returning a
   `PoolHealthSnapshot` — a single deep-module query that traverses the
   node list once.

## Consequences
- **Callers get a single snapshot** instead of two separate counts.
- **Crash history surfaced**: `totalRestarts` gives diagnostic visibility
  without exposing internal `WorkerNode` pointers.
- **Existing `activeWorkerCount()` / `availableWorkerCount()` preserved**
  for backward compatibility.
