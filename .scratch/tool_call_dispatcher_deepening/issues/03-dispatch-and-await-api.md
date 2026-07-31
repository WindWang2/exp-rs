# 03 — Synchronous dispatchAndAwait API & Caller Migration

**What to build:** Introduce `Json::Value dispatchAndAwait(const Json::Value &envelope, std::chrono::milliseconds timeout)` as the primary synchronous API for headless runners, tests, and agents. `submitBlocking` delegates directly to `dispatchAndAwait`, ensuring identical result JSON schema formatting across synchronous and asynchronous paths.

**Blocked by:** 02 — ToolCallDispatcher Output Asset Committing (OutputCommitter) Integration

**Status:** completed

- [x] Implement `dispatchAndAwait(envelope, timeout)` on `ToolCallDispatcher`.
- [x] Refactor `submitBlocking` to delegate directly to `dispatchAndAwait`.
- [x] Migrate callers (`MCPServer`, `AgentCopilotDockWidget`, `AgentWorkflowExecutor`) to consume the unified API.
- [x] Unit tests in `tests/test_tool_call_dispatcher.cpp` verify synchronous execution, timeout handling, and payload consistency.
