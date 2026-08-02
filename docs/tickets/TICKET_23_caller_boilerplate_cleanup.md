# Ticket TICKET-23: 调用方胶水代码清理与单元测试验证

- **类型**: `task`
- **Status**: 已关闭 (Closed)
- **父级地图**: [MAP_tool_call_dispatcher_output_committer.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_tool_call_dispatcher_output_committer.md)

## 问题 (Question)

如何清理各调用方的重复 `OutputCommitterHandler` 样板代码并使用单元测试验证？

## 决议 (Resolution)

1. **`AgentCopilotDockWidget`** (`src/agent/agent_copilot_dock_widget.cpp`): 移除手动的 `OutputCommitterHandler` 闭包设置，直接向 `m_toolCallDispatcher` 注入 `DataManager*`。
2. **`McpServer`** (`src/agent/mcp_server.cpp`): 移除手动的 `OutputCommitterTaskCenter` 包装与逻辑，改为传递 `DataManager*` 资产权威。
3. **单元测试** (`tests/test_tool_call_dispatcher.cpp`): 编写单元测试用例，验证给 `ToolCallDispatcher` 注入 `DataManager*` 后，`dispatchAndAwait` 能够在任务完成后自动完成资产提交并生成正确的派生记录 JSON。
