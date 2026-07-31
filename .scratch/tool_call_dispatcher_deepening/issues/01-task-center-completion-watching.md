# 01 — ToolCallDispatcher TaskCenter Completion Watching & Standard Payload

**What to build:** `ToolCallDispatcher` automatically hooks `TaskCenter` task completion watching and formats standardized Tool Result JSON payloads (`{"status": "completed"|"error", "errorMessage": ..., "output": ..., "execution_time_ms": ...}`), eliminating duplicated task status polling across UI and agent callers.

**Blocked by:** None — can start immediately

**Status:** completed

- [x] `ToolCallDispatcher` encapsulates `TaskCenter` completion watching and task status transitions.
- [x] `ToolCallDispatcher` formats standardized completion and failure result JSON payloads.
- [x] Unit tests in `tests/test_tool_call_dispatcher.cpp` verify task completion payload generation.
