# 02 — `LlmStreamingClient` Direct Tool Call Dispatch Integration

**What to build:**
Integrate `LlmStreamingClient` tool call handling directly with `AtomicAlgorithmRegistry::executeToolCall()`, allowing function call chunks from LLMs to execute registered GIS algorithms directly.

**Blocked by:** 01 — `AtomicAlgorithmRegistry::executeToolCall` Direct JSON Execution Seam

**Status:** ready-for-agent

- [ ] Connect `LlmStreamingClient::executeToolCall()` to `AtomicAlgorithmRegistry::instance().executeToolCall()`
- [ ] Add Catch2 unit tests verifying streaming tool execution in `tests/test_llm_streaming_client.cpp`
