# 02 — Priority-Aware Resource Profile Scheduler

**What to build:** Enforce strict priority scheduling across resource profile concurrency limits. Higher-priority queued tasks automatically preempt lower-priority tasks for available profile slots.

**Blocked by:** 01 — Asynchronous Event-Driven Task Dispatching

**Status:** ready-for-agent

- [ ] TaskCenter queue ordering respects `TaskPriority` across all resource profiles
- [ ] High-priority tasks take precedence when profile execution slots become available
- [ ] Prevents low-priority background jobs from starving high-priority user-initiated tasks
- [ ] Unit tests verify priority order execution under resource profile slot limits
