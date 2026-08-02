# Wayfinder Map: Absorb Output Asset Committing into ToolCallDispatcher

## 目标 (Destination)

深化 `ToolCallDispatcher` (`src/processing/framework/tool_call_dispatcher.h`)，使其直接接收 `DataManager*` 资产权威（或 `OutputCommitter` 实例），在任务完成 payload 构建（`buildTaskResultPayload`）与同步/异步调度（`dispatchAndAwait`）内部自动完成输出资产事务提交 (`OutputCommitter`) 与派生记录 (`DerivationRecord`) 附着，彻底消除 `AgentCopilotDockWidget`、`McpServer` 和 CLI 运行器中重复的 `OutputCommitterHandler` lambda 胶水代码。

## 说明 (Notes)

- **领域词汇**: 参考 [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md) 中的 Tool Call Dispatcher, Output Committer, Data Manager, Data Asset, Task Center 术语。
- **架构词汇**: 运用 `/codebase-design` 深度模块术语 (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**)。
- **相关 ADR**: ADR 0009 (Data Asset/Display Layer 分离), ADR 0021 (ToolCallDispatcher GUI-Free Seam).

## 决策记录 (Decisions so far)

- [TICKET-20: 目标与边界声明](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_20_tool_call_dispatcher_destination.md) — 确定 `ToolCallDispatcher` 吸收 `OutputCommitter` 资产提交的目标与边界。
- [TICKET-21: DataManager 资产权威注入](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_21_data_manager_authority_injection.md) — 为 `ToolCallDispatcher` 增加 `setDataManager(DataManager*)` / `setOutputCommitter(OutputCommitter*)` 深度 Seam。
- [TICKET-22: 内部事务提交与 Payload 自动重写](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_22_internal_transactional_asset_commit.md) — 将资产提交与派生记录记录下沉至 `buildTaskResultPayload`。
- [TICKET-23: 调用方胶水代码清理](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_23_caller_boilerplate_cleanup.md) — 清理 `AgentCopilotDockWidget`、`McpServer` 和 CLI 运行器中的重复 `OutputCommitterHandler` 样板代码。

## 待确定事项 (Not yet specified)

- 针对 Virtual Raster / 临时内存段中间结果的延迟 Commit 策略优化。

## 超出范围 (Out of scope)

- 修改 `TaskCenter` 任务调度队列逻辑。
- 改变 MCP 协议 JSON-RPC stdo 序列化格式。
