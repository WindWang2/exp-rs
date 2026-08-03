# 01 — Asynchronous Event-Driven Task Dispatching

**What to build:** Replace polling/sleep intervals in TaskCenter dispatching with event-driven signal/condition variable notifications. Workers wake up reactively when tasks are queued or slot limits free up.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] TaskCenter worker dispatch loop uses `QWaitCondition` signal wakeups instead of polling sleeps
- [ ] Queueing a new task immediately signals the dispatch thread
- [ ] Task completion or slot release signals pending worker threads without latency
- [ ] Unit test verifies sub-millisecond wakeup latency when tasks are queued
