# 01 — Priority Queueing & Resource Throttling Strategy

**Type:** wayfinder:grilling

## Question

How should task priority levels (High, Normal, Low) and CPU thread throttling be structured in `TaskCenter` to prevent system starvation during heavy raster processing?

## Blocked by

None — can start immediately.

**Status:** closed

### Resolution
- Introduced `TaskPriority` enum (`High`, `Normal`, `Low`).
- Queued tasks are ordered by Priority (High > Normal > Low) and timestamp.
- Concurrent background execution is auto-capped to `std::thread::hardware_concurrency() - 1`.
- Documented in [0002-task-priority-and-resource-throttling.md](../../docs/adr/0002-task-priority-and-resource-throttling.md).
