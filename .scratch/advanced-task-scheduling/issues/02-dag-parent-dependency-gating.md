# 02 — DAG Parent Dependency Gating & Cascade Cancellation

**What to build:** Add `parentTaskIds` to `AlgorithmTaskInfo`, implement dependency gating for queued child tasks, and cascade failure cancellations.

**Blocked by:** 01 — TaskPriority & Resource Throttling in TaskCenter

**Status:** ready-for-agent

- [ ] Add `parentTaskIds` to `AlgorithmTaskInfo` in `task_center.h`
- [ ] Implement dependency evaluation logic in `TaskCenter`
- [ ] Implement cascade cancellation when a parent task fails or is canceled
- [ ] Add unit tests in `tests/test_task_center.cpp`
