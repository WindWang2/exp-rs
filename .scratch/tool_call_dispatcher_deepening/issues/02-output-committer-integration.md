# 02 — ToolCallDispatcher Output Asset Committing (OutputCommitter) Integration

**What to build:** `ToolCallDispatcher` accepts an optional `OutputCommitter*`. Upon task completion, if an output file path exists, it transactionally publishes the asset to a stable path, registers it in `DataManager`, and updates `payload["output"]` with the committed path (or reports `commitError`).

**Blocked by:** 01 — ToolCallDispatcher TaskCenter Completion Watching & Standard Payload

**Status:** ready-for-agent

- [ ] Add `setOutputCommitter(OutputCommitter *committer)` to `ToolCallDispatcher`.
- [ ] Upon task completion, if `OutputCommitter` is configured and task output exists, commit the asset and rewrite `payload["output"]`.
- [ ] On commit refusal diagnostics, set `payload["commitError"]`.
- [ ] Unit tests in `tests/test_tool_call_dispatcher.cpp` verify output path rewriting and commit refusal diagnostics.
