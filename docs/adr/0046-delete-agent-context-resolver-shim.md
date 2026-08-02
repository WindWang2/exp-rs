# ADR 0046: Delete AgentContextResolver Pass-Through Shim

## Status
Accepted

## Context
After the WorkspaceSnapshot deepening, `AgentContextResolver` had collapsed
into a pass-through: `buildContextSnapshot` forwarded one line to
`WorkspaceSnapshot::capture().toJson()`, and the QJsonObject overload of
`formatSystemContextPrompt` hand-duplicated `toSystemPromptHeader()` with a
60-line formatter that had to be kept in sync. The only production caller,
`AgentCopilotDockWidget::sendPrompt`, used the JSON round-trip branch; the
struct branch was exercised only by tests. `WorkspaceSnapshot::toJson()`
had no production consumers beyond the shim.

## Decision
1. **Delete `AgentContextResolver`**: remove the class, its unit test, and
   its registrations in `src/agent/CMakeLists.txt` and `tests/CMakeLists.txt`.

2. **Call the seam directly**: `sendPrompt` now invokes
   `WorkspaceSnapshot::capture( m_dataManager, m_viewHost ).toSystemPromptHeader()`.

3. **Delete `WorkspaceSnapshot::toJson()`**: with the shim gone there are no
   consumers; unique prompt-content coverage moved onto `toSystemPromptHeader()`.

## Consequences
- **Single formatting path**: the hand-synced JSON formatter is gone; prompt
  rendering lives only on `WorkspaceSnapshot`.
- **Smaller API surface**: `WorkspaceSnapshot` no longer carries a dead JSON
  serialization.
- **Tests pin the surviving seam**: assertions moved to
  `tests/test_workspace_snapshot.cpp`.
