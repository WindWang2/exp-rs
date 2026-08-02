# Ticket TICKET-21: DataManager 资产权威注入

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_tool_call_dispatcher_output_committer.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_tool_call_dispatcher_output_committer.md)

## 问题 (Question)

如何在 `ToolCallDispatcher` 中注入 `DataManager` 权威并自动构造/绑定 `OutputCommitter`？

## 决议 (Resolution)

在 `ToolCallDispatcher` (`src/processing/framework/tool_call_dispatcher.h`) 中：
1. 增加 `setDataManager(sicnu::data::DataManager *dataManager)` 方法。
2. 内部自动实例化或绑定 `OutputCommitter`，无需调用者手动构造 `OutputCommitterHandler` lambda。
3. 保持现有 `setOutputCommitterHandler` 方法兼容单元测试中的 stub/mock 测试用例。
