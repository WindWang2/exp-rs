# Ticket TICKET-30: 目标与边界声明 (Scope & Destination Statement)

- **类型**: `grilling`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_cli_pipeline_runner_consolidation.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_cli_pipeline_runner_consolidation.md)

## 问题 (Question)

`RsPipelineRunner` 深化重构的精确目标、架构边界与成功标准是什么？

## 决议 (Resolution)

目标是将 `RsPipelineRunner` 打造为无界面 CLI 与后台批处理的核心管道：
1. **单一入口**：调用方无需手动实例化 `PluginHost`、`DataManager` 或处理复杂循环，只需配置 `addPythonPluginDirectory(...)` 并调用 `runFromFile(...)` 即可。
2. **高局部性与高杠杆**：将插件加载、GDAL 校验、任务调度与资产持久化策略收拢到 `RsPipelineRunner` 内部，使 `main_cli.cpp` 的代码量减少 40% 以上。
3. **完美无界面测试**：测试套件可直接通过 `RsPipelineRunner` 运行包含 Python 算法步骤的复杂 pipeline 文件，提高端到端测试能力。
