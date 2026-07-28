# 02 — `AtomicAlgorithmRegistry` & `LlmStreamingClient` Async Tool Dispatch Integration

**What to build:**
Integrate `AtomicAlgorithmRegistry::submitToolCall()` and `LlmStreamingClient` tool response handlers with `TaskCenter::enqueueToolCall()`.

**Blocked by:** 01 — `TaskCenter::enqueueToolCall` Queue & Async Execution Seam

**Status:** ready-for-agent

- [ ] Add `submitToolCall(const std::string &jsonToolCall)` to `AtomicAlgorithmRegistry`
- [ ] Connect `LlmStreamingClient` to enqueue tool calls in `TaskCenter`
- [ ] Add Catch2 unit tests in `tests/test_atomic_algorithm_registry.cpp`
