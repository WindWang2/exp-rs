# 02 — Wave B: AgentContextResolver Dynamic Workspace Context Injection

**What to build:**
`AgentContextResolver` dynamically inspects `DataManager` (active Data Assets, file paths, CRS, band references) and `ActiveViewHost` (selected layer, map canvas extent) before each prompt submission, serializing active assets into a concise JSON workspace context snapshot automatically injected into LLM System Prompts.

**Blocked by:** 01 — Wave A: LlmStreamingClient C++ SSE Client & LlmConfigManager Settings Engine

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] Implement `AgentContextResolver::buildContextSnapshot(DataManager *dataMgr, ActiveViewHost *viewHost)` returning concise JSON context.
- [ ] Serialize active Data Assets in `DataManager` including Asset ID, display name, file path, asset kind (`Raster`/`Vector`), CRS, and band count.
- [ ] Serialize active map view CRS and bounding box extent.
- [ ] Inject workspace context JSON into LLM System Prompt header during `LlmStreamingClient::sendChatCompletion()`.
- [ ] Add Catch2 unit tests in `tests/test_agent_context_resolver.cpp` verifying snapshot generation and context formatting.
