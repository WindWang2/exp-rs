# Ticket TICKET-32: DataManager 惰性接管与资产注册

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_cli_pipeline_runner_consolidation.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_cli_pipeline_runner_consolidation.md)

## 问题 (Question)

`RsPipelineRunner` 如何自动管理 `DataManager` 的生命周期与流水线步骤资产注册？

## 决议 (Resolution)

1. 在 `RsPipelineRunner` 中支持自管理 `std::unique_ptr<sicnu::data::DataManager>`。
2. 若调用方显式调用 `setAssetRegistry(dataManager)`，则借用外部 `DataManager`；若未显式指定且配置了 Python 插件或启用资产注册，则内部自动创建默认 `DataManager`。
3. 在流水线所有步骤执行完成后，内部自动调用 `registerStepOutputs(pipelineId)` 将步骤输出注册为 `TaskTemporary` 资产。
