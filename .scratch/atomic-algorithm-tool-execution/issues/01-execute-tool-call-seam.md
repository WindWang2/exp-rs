# 01 — `AtomicAlgorithmRegistry::executeToolCall` Direct JSON Execution Seam

**What to build:**
Deepen `AtomicAlgorithmRegistry` to accept OpenAI/Qwen JSON tool call payloads (`{"name": "algo_id", "parameters": {...}}`) directly via `executeToolCall(const std::string &jsonToolCall)`, parsing parameters, looking up the registered `AtomicAlgorithmAdapter`, executing it, and returning structured JSON results.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Add `executeToolCall(const std::string &jsonToolCall)` method signature to `AtomicAlgorithmRegistry`
- [ ] Implement parameter extraction, adapter lookup, execution, and JSON result formatting
- [ ] Add Catch2 unit tests in `tests/test_atomic_algorithm_registry.cpp`
