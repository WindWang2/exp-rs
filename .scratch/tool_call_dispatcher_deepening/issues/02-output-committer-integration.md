# 02 — ToolCallDispatcher Output Asset Committing (OutputCommitter) Integration

**What to build:** `ToolCallDispatcher` accepts an optional `OutputCommitter*`. Upon task completion, if an output file path exists, it transactionally publishes the asset to a stable path, registers it in `DataManager`, and updates `payload["output"]` with the committed path (or reports `commitError`).

**Blocked by:** 01 — ToolCallDispatcher TaskCenter Completion Watching & Standard Payload

**Status:** completed

- [x] Add `setOutputCommitterHandler` to `ToolCallDispatcher`.
- [x] Upon task completion, if `OutputCommitterHandler` is configured and task output exists, commit the asset and rewrite `payload["output"]`.
- [x] On commit refusal diagnostics, set `payload["commitError"]`.
- [x] Unit tests in `tests/test_tool_call_dispatcher.cpp` verify output path rewriting and commit refusal diagnostics.
