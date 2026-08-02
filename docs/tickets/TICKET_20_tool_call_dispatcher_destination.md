# Ticket TICKET-20: 目标与边界声明 (Scope & Destination Statement)

- **类型**: `grilling`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_tool_call_dispatcher_output_committer.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_tool_call_dispatcher_output_committer.md)

## 问题 (Question)

`ToolCallDispatcher` 吸收 `OutputCommitter` 资产提交重构的精确目标、架构边界与成功标准是什么？

## 决议 (Resolution)

目标是在 `ToolCallDispatcher` 中建立高杠杆、高局部性的深度接口：
1. **资产权威注入**：`ToolCallDispatcher` 允许直接注入 `DataManager*` 资产权威或 `OutputCommitter` 实例。
2. **自动事务提交**：在算法任务成功执行后，dispatcher 在 `buildTaskResultPayload` 内部自动调用 `OutputCommitter` 将输出路径注册为稳定 `DataAsset` 并附着 `DerivationRecord` 派生记录。
3. **消除重复代码**：`AgentCopilotDockWidget`、`McpServer` 和 CLI 运行器不再手动编写 `OutputCommitterHandler` 闭包，直接调用 `dispatchAndAwait()` 即可获得已完成提交的统一 JSON Payload。
