# Ticket TICKET-22: 内部事务提交与 Payload 自动重写

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_tool_call_dispatcher_output_committer.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_tool_call_dispatcher_output_committer.md)

## 问题 (Question)

如何在 `ToolCallDispatcher::buildTaskResultPayload` 和 `dispatchAndAwait` 内部自动执行资产事务提交并重写输出 JSON？

## 决议 (Resolution)

在 `ToolCallDispatcher::buildTaskResultPayload` 中：
1. 检查任务状态，若任务成功 (`info.status == TaskStatus::Completed`) 且包含输出文件路径：
   - 优先使用已注入的 `DataManager*` 或 `OutputCommitter` 执行 `commitTaskOutput(info)` 事务提交。
   - 若提交成功，将结果 JSON 中的输出路径自动替换为注册后的稳定 Data Asset 路径 (`committedAssetPath`)。
   - 附着对应的 `DerivationRecord` 溯源记录。
2. 若提交失败，在返回的 Payload 中填入错误信息 (`status = "error"`) 并记录诊断日志。
