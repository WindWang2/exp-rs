# 01 — TaskPriority & Resource Throttling in TaskCenter

**What to build:** Implement `TaskPriority` enum (`High`, `Normal`, `Low`), priority queue sorting, and `hardware_concurrency() - 1` max active task bounding in `TaskCenter`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Add `TaskPriority` enum to `src/processing/framework/task_center.h`
- [ ] Implement priority sorting in `TaskCenter`
- [ ] Bounded concurrent task execution to `std::thread::hardware_concurrency() - 1`
- [ ] Add unit tests in `tests/test_task_center.cpp`
