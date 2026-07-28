# 02 — WorkspaceSnapshot Capture Engine & AgentContextResolver Cutover

**What to build:**
Implement `WorkspaceSnapshot::capture(DataManager *dataManager, ActiveViewHost *viewHost)` using strictly `ActiveViewHost` facade methods (`mapCanvasExtent()`, `mapCanvasScale()`, `activeLayer()`), completely eliminating raw `QgsMapCanvas*` dependencies in `AgentContextResolver`. Refactor `AgentContextResolver` to delegate `buildContextSnapshot` and `formatSystemContextPrompt` directly to `WorkspaceSnapshot`. Update all call sites in `AgentCopilotDockWidget` and Catch2 integration tests.

**Blocked by:** 01 — WorkspaceSnapshot Struct & Pure C++ Serialization / Formatting

**Status:** ready-for-agent

- [ ] Implement `WorkspaceSnapshot::capture(data::DataManager*, ActiveViewHost*)` in `src/agent/workspace_snapshot.cpp`.
- [ ] Refactor `AgentContextResolver` to wrap and delegate to `WorkspaceSnapshot`.
- [ ] Update `AgentCopilotDockWidget` and existing agent context tests to use `WorkspaceSnapshot`.
- [ ] Add Catch2 capture integration test cases in `tests/test_workspace_snapshot.cpp`.
- [ ] Build and verify all agent test suites pass cleanly.
