# 01 — `TaskCenter::enqueueToolCall` Queue & Async Execution Seam

**What to build:**
Enable `TaskCenter` to enqueue and execute single-step LLM tool calls asynchronously via `enqueueToolCall(const std::string &jsonToolCall, bool autoLoad)`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Add `enqueueToolCall(const std::string &jsonToolCall, bool autoLoad)` method declaration to `TaskCenter`
- [ ] Implement parameter parsing, algorithm adapter execution on background threads, and status updates
- [ ] Add Catch2 unit tests in `tests/test_task_center.cpp`
