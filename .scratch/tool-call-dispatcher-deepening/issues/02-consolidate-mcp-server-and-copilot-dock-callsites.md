# 02 — Consolidate MCP Server & Copilot Dock Call sites

**What to build:** Update production callers (`McpServer` and `AgentCopilotDockWidget`) to use the zero-boilerplate default `ToolCallDispatcher` constructor, removing redundant lambda bridge code.

**Blocked by:** 01 — Default Construction & Direct TaskCenter Submission

**Status:** ready-for-agent

- [ ] `McpServer` instantiates `ToolCallDispatcher` without manually wrapping `TaskCenter` lambdas
- [ ] `AgentCopilotDockWidget` instantiates `ToolCallDispatcher` using default direct dispatching
- [ ] Codebase clean build succeeds without warnings
- [ ] Existing MCP and Copilot unit tests pass cleanly
